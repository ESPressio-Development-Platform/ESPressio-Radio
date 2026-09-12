#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Radio {

/// <summary>Wire-stable neutral Radio service classes used by R1/Q1/R3.</summary>
enum class RadioServiceClass : std::uint8_t {
    Invalid = 0,
    Infrastructure = 1,
    Clock = 2,
    Critical = 3,
    Responsive = 4,
    Convergent = 5,
    BestEffort = 6
};

static constexpr std::size_t RadioServiceClassCount = 6;

constexpr bool IsValidRadioServiceClass(RadioServiceClass service) noexcept {
    const auto value = static_cast<std::uint8_t>(service);
    return value >= static_cast<std::uint8_t>(RadioServiceClass::Infrastructure) &&
           value <= static_cast<std::uint8_t>(RadioServiceClass::BestEffort);
}

/// <summary>Controls whether an expiry is only terminal or may participate in bounded EDF promotion.</summary>
enum class RadioDeadlineTreatment : std::uint8_t {
    ExpiryOnly = 0,
    Promotable = 1
};

/// <summary>Minimum direct-link evidence required before a physical fragment is terminally satisfied.</summary>
enum class RadioDirectLinkEvidenceRequirement : std::uint8_t {
    TransmissionCompletion = 0,
    PeerAcknowledgement = 1
};

/// <summary>Non-zero identity for one physical contention domain shared by providers which serialize airtime.</summary>
struct RadioContentionDomainId final {
    std::uint32_t Value{0};
    constexpr explicit operator bool() const noexcept { return Value != 0; }
    constexpr bool operator==(RadioContentionDomainId other) const noexcept { return Value == other.Value; }
    constexpr bool operator!=(RadioContentionDomainId other) const noexcept { return !(*this == other); }
};
static_assert(sizeof(RadioContentionDomainId) == 4, "RadioContentionDomainId must remain a compact scalar");

/// <summary>Quality classification for a provider's conservative per-frame transmission-cost model.</summary>
enum class RadioCostEstimateQuality : std::uint8_t {
    Invalid = 0,
    RelativeOnly = 1,
    ConservativeAirtime = 2
};

/// <summary>Finite R2 cost for one candidate physical frame.</summary>
struct RadioTransmissionCost final {
    std::uint64_t FairnessCostUnits{0};
    std::uint64_t ConservativeAirtimeNanoseconds{0};
    RadioCostEstimateQuality Quality{RadioCostEstimateQuality::Invalid};

    constexpr bool IsValid() const noexcept {
        return FairnessCostUnits != 0 && Quality != RadioCostEstimateQuality::Invalid;
    }
    constexpr bool SupportsPromotableDeadline() const noexcept {
        return IsValid() && Quality == RadioCostEstimateQuality::ConservativeAirtime &&
               ConservativeAirtimeNanoseconds != 0;
    }
};

/// <summary>Frozen R1 service semantics independent of one transfer's local monotonic timestamps.</summary>
struct RadioServiceProfile final {
    RadioServiceClass Class{RadioServiceClass::Invalid};
    RadioDeadlineTreatment DeadlineTreatment{RadioDeadlineTreatment::ExpiryOnly};
    RadioDirectLinkEvidenceRequirement RequiredDirectLinkEvidence{
        RadioDirectLinkEvidenceRequirement::TransmissionCompletion};

    constexpr bool IsValid() const noexcept { return IsValidRadioServiceClass(Class); }
};

/// <summary>Per-transfer local monotonic timing facts consumed by R3; neither field is System Time.</summary>
struct RadioTransferTiming final {
    std::uint64_t ExpiryNanoseconds{0};
    std::uint64_t ServiceDeadlineNanoseconds{0};

    constexpr bool IsValidFor(const RadioServiceProfile& profile) const noexcept {
        if (!profile.IsValid() || ExpiryNanoseconds == 0) return false;
        if (profile.DeadlineTreatment == RadioDeadlineTreatment::ExpiryOnly)
            return ServiceDeadlineNanoseconds == 0;
        return ServiceDeadlineNanoseconds != 0 && ServiceDeadlineNanoseconds <= ExpiryNanoseconds;
    }
};

} // namespace ESPressio::Radio
