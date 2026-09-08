#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_IRadio.hpp"

namespace ESPressio::Radio {

/// <summary>Provider-level ingress class. It expresses scheduling importance, never application semantics.</summary>
/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class RadioIngressClass : std::uint8_t {
    Standard = 0U,
    Control = 1U
};

/// <summary>Snapshot of one bounded provider ingress queue.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - AcceptedPackets (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - DroppedPackets (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - CurrentDepth (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - HighWatermark (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - Capacity (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 28 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RadioIngressQueueStatistics final {
    std::uint64_t AcceptedPackets{0U};
    std::uint64_t DroppedPackets{0U};
    std::uint32_t CurrentDepth{0U};
    std::uint32_t HighWatermark{0U};
    std::uint32_t Capacity{0U};
};

/// <summary>Non-blocking callback used by a capable Radio provider to route opaque inbound bytes by urgency.</summary>
/// <remarks>
/// This callback may execute in an ISR/driver callback context. Implementations MUST be bounded, allocation-free,
/// non-blocking and noexcept. Concrete Radio providers do not interpret the meaning of a control protocol; they merely
/// ask this classifier whether opaque bytes require the dedicated control lifecycle.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioIngressClassifier {
public:
    virtual ~IRadioIngressClassifier() = default;
    virtual RadioIngressClass ClassifyInbound(
        IRadio& radio,
        const std::uint8_t* payload,
        std::size_t payloadBytes
    ) const noexcept = 0;
};

/// <summary>Optional provider extension for physically separate control and standard ingress queues.</summary>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioPrioritizedIngress {
public:
    virtual ~IRadioPrioritizedIngress() = default;

    virtual void SetIngressClassifier(IRadioIngressClassifier* classifier) noexcept = 0;
    virtual void SetControlReceiver(IRadioReceiver* receiver) noexcept = 0;
    virtual void SetControlWorkSignal(IRadioWorkSignal* signal) noexcept = 0;

    /// <summary>Legacy control-ingress drain hook retained for source compatibility.</summary>
    virtual void DrainControlInbound() = 0;

    /// <summary>
    /// Services one finite control-ingress quantum and reports whether queued work remains.
    /// </summary>
    /// <remarks>
    /// Queue-backed providers should override this rather than spin until empty. The compatibility implementation calls
    /// DrainControlInbound() once. A zero maximum asks the provider to apply its own finite service quantum.
    /// </remarks>
    virtual RadioIngressServiceResult ServiceControlInbound(std::size_t maximumPackets = 0U) {
        (void)maximumPackets;
        DrainControlInbound();
        return {};
    }

    virtual RadioIngressQueueStatistics StandardIngressStatistics() const noexcept = 0;
    virtual RadioIngressQueueStatistics ControlIngressStatistics() const noexcept = 0;
};

/// <summary>One Radio-owned latency-critical control protocol serviced outside ordinary RadioTransport traffic.</summary>
/// <remarks>
/// MatchesControlFrame may be called from a driver callback through IRadioIngressClassifier and is therefore subject to
/// the same strict bounded/noexcept constraints. ProcessControlPacket and ServiceControl execute on RadioControlWorker.
/// A protocol owns its own cadence/timeouts and must not assume that Mesh/application workers execute it.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioControlProtocol {
public:
    virtual ~IRadioControlProtocol() = default;

    virtual bool MatchesControlFrame(
        const IRadio& radio,
        const std::uint8_t* payload,
        std::size_t payloadBytes
    ) const noexcept = 0;

    virtual void ProcessControlPacket(
        IRadio& radio,
        const RadioPacketView& packet
    ) noexcept = 0;

    virtual void ServiceControl() noexcept = 0;
};

/// <summary>Scheduling and per-packet processing latency snapshot for one Radio worker lifecycle.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - PacketsServiced (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - TimestampedPackets (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - TotalServiceLatencyNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MinimumServiceLatencyNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MaximumServiceLatencyNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - WorkSignals (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - Iterations (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - ProcessingSamples (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - TotalProcessingDurationNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MinimumProcessingDurationNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MaximumProcessingDurationNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 88 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RadioWorkerLatencyStatistics final {
    std::uint64_t PacketsServiced{0U};
    std::uint64_t TimestampedPackets{0U};
    std::uint64_t TotalServiceLatencyNanoseconds{0U};
    std::uint64_t MinimumServiceLatencyNanoseconds{0U};
    std::uint64_t MaximumServiceLatencyNanoseconds{0U};
    std::uint64_t WorkSignals{0U};
    std::uint64_t Iterations{0U};
    std::uint64_t ProcessingSamples{0U};
    std::uint64_t TotalProcessingDurationNanoseconds{0U};
    std::uint64_t MinimumProcessingDurationNanoseconds{0U};
    std::uint64_t MaximumProcessingDurationNanoseconds{0U};

    double MeanServiceLatencyNanoseconds() const noexcept {
        return TimestampedPackets == 0U
            ? 0.0
            : static_cast<double>(TotalServiceLatencyNanoseconds) /
              static_cast<double>(TimestampedPackets);
    }

    double MeanProcessingDurationNanoseconds() const noexcept {
        return ProcessingSamples == 0U
            ? 0.0
            : static_cast<double>(TotalProcessingDurationNanoseconds) /
              static_cast<double>(ProcessingSamples);
    }
};

} // namespace ESPressio::Radio
