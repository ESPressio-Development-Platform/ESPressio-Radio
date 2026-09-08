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
/**
 * ESPressio Memory Audit
 * Members:
 * - IterationPeriodMilliseconds (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - DesiredExecutionBudgetMilliseconds (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - IngressQuantumPackets (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 12 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
/// a worker pass into a drain-until-empty spin. If bounded provider work remains, another independent work wake is queued;
/// the periodic iteration remains only the fallback for unsignalled providers.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 364 bytes [PrecisionThread: Thread: _taskExited: owned object: 4 bytes; PrecisionThread: Thread: _taskStartGate: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _stateTransitionMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _stateTransitionMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _callbackMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _callbackMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _onDestroy: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitialize: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStart: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onPause: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminate: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminated: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitializationFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onExecutionFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStateChange: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: _iterationObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _scheduleSignal: owned object: 4 bytes; PrecisionThread: _timingMutex: _owned: owned object: 4 bytes; PrecisionThread: _timingMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationSamples: implementation blocks containing N * (8 bytes) plus block-map pointers]
 * Requires Stack/Heap Preallocation
 * Members:
 * - _transport (RadioTransport&): 4 bytes [0 bytes dynamic allocation]
 * - _radios (std::array<IRadio*, ESPRESSIO_RADIO_MAX_INTERFACES>): 4 bytes [0 bytes dynamic allocation]
 * - _configurationMutex (System::Synchronization::Mutex): 20 bytes [_owned: owned object: 4 bytes; _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * - _configuration (RadioWorkerConfiguration): 12 bytes [0 bytes dynamic allocation]
 * - _packetsServiced (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _timestampedPackets (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _totalServiceLatencyNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _minimumServiceLatencyNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _maximumServiceLatencyNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _workSignals (std::atomic<std::uint32_t>): 4 bytes [0 bytes dynamic allocation]
 * - _iterations (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _workWakePasses (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _continuationWakes (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _processingSamples (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _totalProcessingDurationNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _minimumProcessingDurationNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _maximumProcessingDurationNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 504 bytes [PrecisionThread: Thread: _taskExited: owned object: 4 bytes; PrecisionThread: Thread: _taskStartGate: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _taskConfigurationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _stateTransitionMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _stateTransitionMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _lifecycleObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _callbackMutex: _owned: owned object: 4 bytes; PrecisionThread: Thread: _callbackMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: Thread: _onDestroy: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitialize: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStart: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onPause: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminate: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onTerminated: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onInitializationFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onExecutionFailed: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: Thread: _onStateChange: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 4 bytes; PrecisionThread: _iterationObservable: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 96 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _registrations: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: Observable: _bindings: Capacity * (12 bytes) element storage; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _mutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _owned: owned object: 4 bytes; PrecisionThread: _iterationObservable: pointee: ThreadSafeObservable: _notificationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _scheduleSignal: owned object: 4 bytes; PrecisionThread: _timingMutex: _owned: owned object: 4 bytes; PrecisionThread: _timingMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily; PrecisionThread: _iterationSamples: implementation blocks containing N * (8 bytes) plus block-map pointers; _configurationMutex: _owned: owned object: 4 bytes; _configurationMutex: _fallback: _mutex: native synchronization state may allocate platform resources lazily]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
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

    bool ServiceInboundQuantum() {
        RadioWorkerConfiguration configuration;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
            configuration = _configuration;
        }
        bool workRemaining = false;
        for (IRadio* radio : _radios) {
            if (radio == nullptr || !radio->IsStarted()) continue;
            const auto serviced = radio->ServiceInbound(configuration.IngressQuantumPackets);
            workRemaining = workRemaining || serviced.WorkRemaining;
        }
        return workRemaining;
    }

    void ContinueIfRequired(bool workRemaining) {
        if (!workRemaining) return;
        _continuationWakes.fetch_add(1U, std::memory_order_relaxed);
        // Queue another finite service pass without moving the periodic fallback schedule.
        WakeForWork();
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
        ContinueIfRequired(ServiceInboundQuantum());
    }

    void Iterate(Time, Time, Threads::SkippedIterationCount) override {
        _iterations.fetch_add(1U, std::memory_order_relaxed);
        ContinueIfRequired(ServiceInboundQuantum());
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
    std::atomic<std::uint32_t> _workSignals{0U};
    std::atomic<std::uint64_t> _iterations{0U};
    std::atomic<std::uint64_t> _workWakePasses{0U};
    std::atomic<std::uint64_t> _continuationWakes{0U};
    std::atomic<std::uint64_t> _processingSamples{0U};
    std::atomic<std::uint64_t> _totalProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumProcessingDurationNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumProcessingDurationNanoseconds{0U};
};

} // namespace ESPressio::Radio
