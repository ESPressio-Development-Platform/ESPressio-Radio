#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_SystemPlatformClock.hpp>
#include <ESPressio_Time.hpp>

#include "ESPressio_RadioControl.hpp"

#ifndef ESPRESSIO_RADIO_CONTROL_MAX_INTERFACES
#define ESPRESSIO_RADIO_CONTROL_MAX_INTERFACES 4
#endif

#ifndef ESPRESSIO_RADIO_CONTROL_MAX_PROTOCOLS
#define ESPRESSIO_RADIO_CONTROL_MAX_PROTOCOLS 8
#endif

namespace ESPressio::Radio {

/// <summary>Runtime scheduling configuration for the independent Radio control-plane lifecycle.</summary>
struct RadioControlWorkerConfiguration final {
    /// <summary>Fallback service cadence when no asynchronous control packet wake occurs.</summary>
    std::uint32_t IterationPeriodMilliseconds{1U};
    std::uint32_t DesiredExecutionBudgetMilliseconds{1U};
    unsigned int Priority{4U};
    int CoreId{-1};
};

/// <summary>
/// High-priority lifecycle for latency-critical Radio-owned control protocols, independent of ordinary RadioTransport.
/// </summary>
/// <remarks>
/// The worker is intentionally both the provider ingress classifier and the control packet receiver. Matching performed
/// from a driver callback is bounded to a fixed protocol array. Actual protocol work, clock discipline and response TX
/// execute only on this PrecisionThread. Registrations are configuration-time operations and must be completed before
/// the attached Radio is started; they are then immutable for that running Radio lifetime.
/// </remarks>
class RadioControlWorker final
    : public Threads::PrecisionThread<
          Units::NanoSeconds<std::uint64_t>,
          Threads::PrecisionThreadTraits<Units::NanoSeconds<std::uint64_t>>
      >,
      public IRadioReceiver,
      public IRadioWorkSignal,
      public IRadioIngressClassifier {
public:
    using Time = Units::NanoSeconds<std::uint64_t>;
    using Base = Threads::PrecisionThread<Time, Threads::PrecisionThreadTraits<Time>>;

private:
    struct InterfaceBinding final {
        IRadio* Radio{nullptr};
        IRadioPrioritizedIngress* Ingress{nullptr};
    };

    struct ProtocolBinding final {
        IRadio* Radio{nullptr};
        IRadioControlProtocol* Protocol{nullptr};
    };

    std::array<InterfaceBinding, ESPRESSIO_RADIO_CONTROL_MAX_INTERFACES> _interfaces{};
    std::array<ProtocolBinding, ESPRESSIO_RADIO_CONTROL_MAX_PROTOCOLS> _protocols{};
    RadioControlWorkerConfiguration _configuration{};

    std::atomic<std::uint64_t> _packetsServiced{0U};
    std::atomic<std::uint64_t> _timestampedPackets{0U};
    std::atomic<std::uint64_t> _totalServiceLatencyNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumServiceLatencyNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumServiceLatencyNanoseconds{0U};
    // OnRadioWorkAvailable may execute directly in a provider driver callback. Keep this diagnostic counter at a
    // naturally lock-free width on 32-bit targets; the public statistics snapshot widens it to uint64_t.
    std::atomic<std::uint32_t> _workSignals{0U};
    std::atomic<std::uint64_t> _iterations{0U};
    std::atomic<std::uint64_t> _processingSamples{0U};
    std::atomic<std::uint64_t> _totalProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _unmatchedControlPackets{0U};

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

public:
    explicit RadioControlWorker(RadioControlWorkerConfiguration configuration = {})
        : _configuration(configuration) {
        SetStartOnInitialize(false);
        SetIterationPeriod(Units::MilliSeconds<std::uint32_t>(configuration.IterationPeriodMilliseconds));
        SetDesiredIterationPeriod(Units::MilliSeconds<std::uint32_t>(configuration.DesiredExecutionBudgetMilliseconds));
        SetPriority(configuration.Priority);
        if (configuration.CoreId >= 0) SetCoreID(configuration.CoreId);
    }

    ~RadioControlWorker() override {
        Shutdown();
        for (auto& binding : _interfaces) {
            if (binding.Ingress == nullptr) continue;
            binding.Ingress->SetControlWorkSignal(nullptr);
            binding.Ingress->SetControlReceiver(nullptr);
            binding.Ingress->SetIngressClassifier(nullptr);
        }
    }

    RadioControlWorker(const RadioControlWorker&) = delete;
    RadioControlWorker& operator=(const RadioControlWorker&) = delete;
    RadioControlWorker(RadioControlWorker&&) = delete;
    RadioControlWorker& operator=(RadioControlWorker&&) = delete;

    bool AddInterface(IRadio& radio, IRadioPrioritizedIngress& ingress) noexcept {
        if (radio.IsStarted()) return false;
        for (const auto& existing : _interfaces) {
            if (existing.Radio == &radio) return existing.Ingress == &ingress;
        }
        for (auto& slot : _interfaces) {
            if (slot.Radio != nullptr) continue;
            slot = {&radio, &ingress};
            ingress.SetIngressClassifier(this);
            ingress.SetControlReceiver(this);
            ingress.SetControlWorkSignal(this);
            return true;
        }
        return false;
    }

    bool RegisterProtocol(IRadio& radio, IRadioControlProtocol& protocol) noexcept {
        if (radio.IsStarted()) return false;
        bool interfaceRegistered = false;
        for (const auto& interface : _interfaces) {
            if (interface.Radio == &radio) {
                interfaceRegistered = true;
                break;
            }
        }
        if (!interfaceRegistered) return false;
        for (const auto& existing : _protocols) {
            if (existing.Protocol == &protocol) return existing.Radio == &radio;
        }
        for (auto& slot : _protocols) {
            if (slot.Protocol != nullptr) continue;
            slot = {&radio, &protocol};
            return true;
        }
        return false;
    }

    RadioIngressClass ClassifyInbound(
        IRadio& radio,
        const std::uint8_t* payload,
        std::size_t payloadBytes
    ) const noexcept override {
        if (payload == nullptr || payloadBytes == 0U) return RadioIngressClass::Standard;
        for (const auto& binding : _protocols) {
            if (binding.Radio != &radio || binding.Protocol == nullptr) continue;
            if (binding.Protocol->MatchesControlFrame(radio, payload, payloadBytes)) {
                return RadioIngressClass::Control;
            }
        }
        return RadioIngressClass::Standard;
    }

    void OnRadioWorkAvailable(IRadio&) noexcept override {
        _workSignals.fetch_add(1U, std::memory_order_relaxed);
        try {
            Bump();
        } catch (...) {
            // Driver/task callback boundaries must not observe scheduler exceptions.
        }
    }

    void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) override {
        RecordServiceLatency(packet);
        for (const auto& binding : _protocols) {
            if (binding.Radio != &radio || binding.Protocol == nullptr) continue;
            if (!binding.Protocol->MatchesControlFrame(radio, packet.Payload, packet.PayloadSize)) continue;
            const auto started = System::Clock::Monotonic().NowNanoseconds();
            binding.Protocol->ProcessControlPacket(radio, packet);
            RecordProcessingDuration(started, System::Clock::Monotonic().NowNanoseconds());
            return;
        }
        _unmatchedControlPackets.fetch_add(1U, std::memory_order_relaxed);
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

    std::uint64_t UnmatchedControlPackets() const noexcept {
        return _unmatchedControlPackets.load(std::memory_order_relaxed);
    }

protected:
    void Iterate(Time, Time, Threads::SkippedIterationCount) override {
        _iterations.fetch_add(1U, std::memory_order_relaxed);

        // Drain first so a received request can be answered before periodic request-generation work executes.
        for (auto& binding : _interfaces) {
            if (binding.Radio != nullptr && binding.Ingress != nullptr && binding.Radio->IsStarted()) {
                binding.Ingress->DrainControlInbound();
            }
        }

        for (auto& binding : _protocols) {
            if (binding.Radio != nullptr && binding.Protocol != nullptr && binding.Radio->IsStarted()) {
                binding.Protocol->ServiceControl();
            }
        }
    }
};

} // namespace ESPressio::Radio
