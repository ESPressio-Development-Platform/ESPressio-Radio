#pragma once

#include <cstdint>

namespace ESPressio::Radio {

/// <summary>Wake target installed by the owning Radio contention-domain runtime.</summary>
struct RadioDomainServiceWakeTarget final {
    void* Context{nullptr};
    void (*Wake)(void*) noexcept{nullptr};
};

/// <summary>One bounded optional service-extension quantum sharing the existing Radio domain Task.</summary>
struct RadioDomainExtensionServiceResult final {
    bool ImmediateWorkRemaining{false};
    std::uint64_t EarliestDeadlineNanoseconds{0};
};

/// <summary>
/// Fixed non-owning extension point for bounded Radio-domain infrastructure such as Clock orchestration.
/// </summary>
/// <remarks>
/// An extension never owns another Task and never performs arbitrary application callbacks. The owning
/// RadioDomainRuntime invokes exactly one bounded Service() quantum per loop beside provider ingress and R3 service,
/// then arms its existing signal wait to the earliest scheduler/extension deadline. A wake is only a coalescing hint;
/// the extension must always re-read its retained state when serviced.
/// </remarks>
class IRadioDomainServiceExtension {
public:
    virtual ~IRadioDomainServiceExtension() = default;
    virtual void SetDomainWakeTarget(RadioDomainServiceWakeTarget target) noexcept = 0;
    virtual RadioDomainExtensionServiceResult ServiceDomain(std::uint64_t nowNanoseconds) noexcept = 0;
};

} // namespace ESPressio::Radio
