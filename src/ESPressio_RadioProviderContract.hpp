#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RadioServiceProfile.hpp"
#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Provider-proximate receive-capture source classification.</summary>
enum class RadioTimestampCaptureSource : std::uint8_t {
    Unknown = 0,
    Hardware = 1,
    Driver = 2,
    ProviderCallback = 3,
    ServiceContext = 4
};

/// <summary>Whether receive-capture uncertainty is suitable for certified K1/K2 use.</summary>
enum class RadioTimestampQuality : std::uint8_t {
    Unknown = 0,
    Unbounded = 1,
    Estimated = 2,
    FiniteBounded = 3
};

/// <summary>Complete receive timestamp evidence expressed in the local monotonic coordinate.</summary>
struct RadioReceiveTimestampEvidence final {
    std::uint64_t ProviderCaptureCoordinate{0};
    std::uint64_t MonotonicNanoseconds{0};
    std::uint64_t ConservativeUncertaintyNanoseconds{0};
    std::uint32_t ContinuityGeneration{0};
    RadioTimestampCaptureSource Source{RadioTimestampCaptureSource::Unknown};
    RadioTimestampQuality Quality{RadioTimestampQuality::Unknown};

    constexpr bool HasFiniteBound() const noexcept {
        return MonotonicNanoseconds != 0 && ContinuityGeneration != 0 &&
               Quality == RadioTimestampQuality::FiniteBounded;
    }
    constexpr bool IsCertifiedCandidate() const noexcept {
        return HasFiniteBound() && Source != RadioTimestampCaptureSource::Unknown;
    }
};

/// <summary>Finite provider-owned resources which influence Radio readiness/cost guarantees.</summary>
struct RadioProviderResourceProfile final {
    std::uint16_t MaximumQueuedInboundPackets{0};
    std::uint16_t MaximumInboundPacketsPerService{0};
    std::uint16_t MaximumQueuedTransmitPackets{0};
    std::uint16_t MaximumProviderRetryAttempts{0};

    constexpr bool HasFiniteIngressService() const noexcept {
        return MaximumInboundPacketsPerService != 0;
    }
};

/// <summary>One bounded provider-ingress service quantum.</summary>
struct ManagedRadioIngressServiceResult final {
    std::uint32_t PacketsProcessed{0};
    bool WorkRemaining{false};
};

/// <summary>Fixed non-owning runtime sink installed on a managed provider.</summary>
/// <remarks>Provider callbacks may coalesce these signals. The owning Radio domain runtime always rechecks actual
/// provider/scheduler state after waking. No method invokes Primitive-family or application callbacks.</remarks>
class IRadioRuntimeSink {
public:
    virtual ~IRadioRuntimeSink() = default;
    virtual void InboundAvailable(IRadio& provider) noexcept = 0;
    virtual void TransmissionResolved(
        IRadio& provider,
        RadioTransmissionHandle handle,
        const RadioDirectLinkEvidence& terminalEvidence) noexcept = 0;
    virtual void TransmitReadinessChanged(IRadio& provider) noexcept = 0;
    virtual void LifecycleAvailabilityChanged(IRadio& provider) noexcept = 0;
};

/// <summary>Returns true only when evidence satisfies the frozen R1 direct-link requirement.</summary>
constexpr bool SatisfiesDirectLinkEvidence(
    const RadioDirectLinkEvidence& evidence,
    RadioDirectLinkEvidenceRequirement requirement) noexcept {
    if (!evidence.TransmissionCompleted()) return false;
    if (requirement == RadioDirectLinkEvidenceRequirement::TransmissionCompletion) return true;
    return evidence.PeerAcknowledged();
}

} // namespace ESPressio::Radio
