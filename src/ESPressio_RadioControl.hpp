#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_IRadio.hpp"

namespace ESPressio::Radio {

/// <summary>Provider-level ingress class. It expresses scheduling importance, never application semantics.</summary>

enum class RadioIngressClass : std::uint8_t {
    Standard = 0U,
    Control = 1U
};

/// <summary>Snapshot of one bounded provider ingress queue.</summary>

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
