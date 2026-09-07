#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_SystemPlatformClock.hpp>
#include <ESPressio_Time.hpp>

#include "ESPressio_RadioControl.hpp"
#include "ESPressio_RadioTransport.hpp"

namespace ESPressio::Radio {

/// <summary>Runtime scheduling configuration for the radio inbound worker.</summary>
struct RadioWorkerConfiguration {
    /// <summary>
    /// Maximum idle interval between inbound-service passes. This allows providers without an asynchronous RX signal,
    /// such as an nRF24 implementation without an IRQ integration, to be serviced without application polling.
    /// Async providers wake this worker immediately and therefore do not normally wait for this cadence.
    /// </summary>
    uint32_t IterationPeriodMilliseconds = 10;

    /// <summary>Desired execution budget for one inbound-service pass.</summary>
    uint32_t DesiredExecutionBudgetMilliseconds = 2;

    /// <summary>
    /// Maximum provider packets serviced per interface in one worker pass. This is a cooperative execution quantum, not
    /// a queue-capacity limit. A zero value asks each provider to use its own finite quantum.
    /// </summary>
    std::size_t IngressQuantumPackets = 8U;
};

/// <summary>
/// ESPressio PrecisionThread worker responsible only for servicing standard physical Radio ingress and advancing
/// Radio-owned direct-link logical reassembly.
/// </summary>
/// <remarks>
/// Latency-critical Radio control traffic may be removed before this path by an IRadioPrioritizedIngress provider and
/// RadioControlWorker. Async provider work is handled through PrecisionThread's dedicated work-wake path rather than by
/// moving the periodic schedule. Queue-backed providers are serviced in a finite packet quantum so one burst cannot turn
/// a worker pass into a drain-until-empty spin. The periodic iteration remains only the fallback for unsignalled providers
/// and for any bounded backlog left after a wake quantum.
/// </remarks>
class RadioWorker final
    : public Threads::PrecisionThread<
          Units::NanoSeconds<uint64_t>,
          Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>
      >,
      public IRadioReceiver,
      public IRadioWorkSignal {
public:
    using Time = Units::NanoSeconds<uint64_t>;
    using Base = Threads::PrecisionThread<Time, Threads::PrecisionThreadTraits<Time>>;

private:
    static void UpdateMinimum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept {
        auto current = target.load(std::memory_order_relaxed);
        while ((current == 0U || value < current) &&
               !target.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    static void UpdateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current &&
               !target.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    void RecordServiceLatency(const RadioPacketView& packet) noexcept {
        _packetsServiced.fetch_add(1U, std::memory_order_relaxed);
        if (packet.ReceiveTimestampNanoseconds == 0U) return;
        const auto now = System::Clock::Monotonic().NowNanoseconds();
        if (now < packet.ReceiveTimestampNanoseconds) return;
        const auto latency = now - packet.ReceiveTimestampNanoseconds;
        _timestampedPackets.fetch_add(1U, std::memory_order_relaxed);
        _totalServiceLatencyNanoseconds.fetch_add(latency, std::memory_order_relaxed);
        UpdateMinimum(_minimumServiceLatencyNanoseconds, latency);
        UpdateMaximum(_maximumServiceLatencyNanoseconds, latency);
    }

    void RecordProcessingDuration(std::uint64_t started, std::uint64_t completed) noexcept {
        if (completed < started) return;
        const auto duration = completed - started;
        _processingSamples.fetch_add(1U, std::memory_order_relaxed);
        _totalProcessingDurationNanoseconds.fetch_add(duration, std::memory_order_relaxed);
        UpdateMinimum(_minimumProcessingDurationNanoseconds, duration);
        UpdateMaximum(_maximumProcessingDurationNanoseconds, duration);
    }

    void ServiceInboundQuantum() {
        RadioWorkerConfiguration configuration;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
            configuration = _configuration;
        }
        for (IRadio* radio : _radios) {
            if (radio == nullptr || !radio->IsStarted()) continue;
            (void)radio->ServiceInbound(configuration.IngressQuantumPackets);
        }
    }

public:
    explicit RadioWorker(
        RadioTransport& transport,
        RadioWorkerConfiguration configuration = {}
    ) : _transport(transport), _configuration(configuration) {
        SetStartOnInitialize(false);
        ApplyRuntimeConfiguration(configuration);
    }

    ~RadioWorker() override {
        Shutdown();
        for (IRadio* radio : _radios) {
            if (radio == nullptr) continue;
            radio->SetWorkSignal(nullptr);
            radio->SetReceiver(nullptr);
        }
    }

    RadioWorker(const RadioWorker&) = delete;
    RadioWorker& operator=(const RadioWorker&) = delete;
    RadioWorker(RadioWorker&&) = delete;
    RadioWorker& operator=(RadioWorker&&) = delete;

    /// <summary>
    /// Registers a Radio with RadioTransport and makes this worker the sole standard inbound-service path.
    /// </summary>
    bool AddInterface(IRadio& radio) noexcept {
        for (IRadio* existing : _radios) {
            if (existing == &radio) {
                if (!_transport.AddInterface(radio)) return false;
                radio.SetReceiver(this);
                radio.SetWorkSignal(this);
                return true;
            }
        }

        for (IRadio*& slot : _radios) {
            if (slot != nullptr) continue;
            if (!_transport.AddInterface(radio)) return false;
            slot = &radio;
            radio.SetReceiver(this);
            radio.SetWorkSignal(this);
            return true;
        }
        return false;
    }

    RadioWorkerConfiguration Configuration() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
        return _configuration;
    }

    void Configure(RadioWorkerConfiguration configuration) {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
            _configuration = configuration;
        }
        ApplyRuntimeConfiguration(configuration);
        WakeForWork();
    }

    void OnRadioWorkAvailable(IRadio&) noexcept override {
        _workSignals.fetch_add(1U, std::memory_order_relaxed);
        try {
            // Async ingress is independent work, not a request to move the periodic fallback schedule.
            WakeForWork();
        } catch (...) {
            // A provider work signal must never let scheduler failures escape into a driver callback context.
        }
    }

    void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) override {
        RecordServiceLatency(packet);
        const auto started = System::Clock::Monotonic().NowNanoseconds();
        _transport.ProcessInboundPacket(radio, packet);
        radio.Observers().NotifyPacketReceived(radio, packet);
        RecordProcessingDuration(started, System::Clock::Monotonic().NowNanoseconds());
    }

    RadioWorkerLatencyStatistics GetStatistics() const noexcept {
        return {
            _packetsServiced.load(std::memory_order_relaxed),
            _timestampedPackets.load(std::memory_order_relaxed),
            _totalServiceLatencyNanoseconds.load(std::memory_order_relaxed),
            _minimumServiceLatencyNanoseconds.load(std::memory_order_relaxed),
            _maximumServiceLatencyNanoseconds.load(std::memory_order_relaxed),
            static_cast<std::uint64_t>(_workSignals.load(std::memory_order_relaxed)),
            _iterations.load(std::memory_order_relaxed),
            _processingSamples.load(std::memory_order_relaxed),
            _totalProcessingDurationNanoseconds.load(std::memory_order_relaxed),
            _minimumProcessingDurationNanoseconds.load(std::memory_order_relaxed),
            _maximumProcessingDurationNanoseconds.load(std::memory_order_relaxed)
        };
    }

protected:
    void OnWorkWake() override {
        _workWakePasses.fetch_add(1U, std::memory_order_relaxed);
        ServiceInboundQuantum();
    }

    void Iterate(Time, Time, Threads::SkippedIterationCount) override {
        _iterations.fetch_add(1U, std::memory_order_relaxed);
        ServiceInboundQuantum();
    }

private:
    void ApplyRuntimeConfiguration(const RadioWorkerConfiguration& configuration) {
        SetIterationPeriod(
            Units::MilliSeconds<uint32_t>(configuration.IterationPeriodMilliseconds));
        SetDesiredIterationPeriod(
            Units::MilliSeconds<uint32_t>(configuration.DesiredExecutionBudgetMilliseconds));
    }

    RadioTransport& _transport;
    std::array<IRadio*, ESPRESSIO_RADIO_MAX_INTERFACES> _radios{};
    mutable System::Synchronization::Mutex _configurationMutex;
    RadioWorkerConfiguration _configuration{};

    std::atomic<std::uint64_t> _packetsServiced{0U};
    std::atomic<std::uint64_t> _timestampedPackets{0U};
    std::atomic<std::uint64_t> _totalServiceLatencyNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumServiceLatencyNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumServiceLatencyNanoseconds{0U};
    // OnRadioWorkAvailable may execute directly in a provider driver callback. Keep this diagnostic counter at a
    // naturally lock-free width on 32-bit targets; the public statistics snapshot widens it to uint64_t.
    std::atomic<std::uint32_t> _workSignals{0U};
    std::atomic<std::uint64_t> _iterations{0U};
    std::atomic<std::uint64_t> _workWakePasses{0U};
    std::atomic<std::uint64_t> _processingSamples{0U};
    std::atomic<std::uint64_t> _totalProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumProcessingDurationNanoseconds{0U};
};

} // namespace ESPressio::Radio
