#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RadioProviderContract.hpp"
#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Receives one borrowed physical/link packet from a managed provider service quantum.</summary>
/// <remarks>The payload and timestamp evidence are valid only for the callback. This sink is owned by Radio Runtime;
/// providers never invoke Primitive-family or arbitrary application callbacks directly.</remarks>
class IRadioReceiver {
public:
    virtual ~IRadioReceiver() = default;
    virtual void OnRadioPacket(
        IRadio& radio,
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp) noexcept = 0;
};

/// <summary>Hardware-neutral managed physical-radio provider contract used by the R3 domain scheduler.</summary>
/// <remarks>
/// Implementations transport opaque bytes only. Every provider exposes finite ingress service, one non-zero physical
/// contention-domain identity, a finite positive R2 transmission cost, and terminal TransmissionCompletion either
/// synchronously or through one generation-safe deferred handle. The runtime sink is fixed/non-owning and may only wake
/// Radio infrastructure. No compatibility drain-until-empty or Observable callback surface exists in this contract.
/// </remarks>
class IRadio {
public:
    virtual ~IRadio() = default;

    virtual bool Start() = 0;
    virtual void Stop() noexcept = 0;
    virtual bool IsStarted() const noexcept = 0;

    virtual RadioCapabilities Capabilities() const noexcept = 0;
    virtual RadioAddress LocalAddress() const noexcept = 0;
    virtual RadioContentionDomainId ContentionDomain() const noexcept = 0;
    virtual RadioProviderResourceProfile ProviderResources() const noexcept = 0;

    /// <summary>True only when a non-blocking physical submission may currently be attempted.</summary>
    virtual bool IsTransmitReady() const noexcept = 0;

    /// <summary>Returns a finite non-zero R2 cost for one candidate physical packet.</summary>
    virtual RadioTransmissionCost EstimateTransmissionCost(
        const RadioAddress& destination,
        std::size_t payloadBytes,
        const RadioServiceProfile& profile) const noexcept = 0;

    /// <summary>
    /// Attempts one non-blocking physical packet submission.
    /// </summary>
    /// <remarks>
    /// Accepted must either carry terminal TransmissionCompletion evidence or a valid DeferredTransmission handle which
    /// is resolved exactly once through IRadioRuntimeSink::TransmissionResolved(). Link acknowledgement is reported only
    /// where the physical technology actually proves it.
    /// </remarks>
    virtual RadioSendResult Send(
        const RadioAddress& destination,
        const std::uint8_t* payload,
        std::size_t payloadSize) noexcept = 0;

    /// <summary>Installs the Radio-owned packet sink used only from finite provider service quanta.</summary>
    virtual void SetReceiver(IRadioReceiver* receiver) noexcept = 0;

    /// <summary>Installs the fixed non-owning infrastructure wake/completion sink.</summary>
    virtual void SetRuntimeSink(IRadioRuntimeSink* sink) noexcept = 0;

    /// <summary>Services at most a finite provider-defined/requested number of queued inbound packets.</summary>
    /// <remarks>A zero maximum asks the provider to use its own declared finite quantum; zero never means unbounded.</remarks>
    virtual ManagedRadioIngressServiceResult ServiceInbound(std::size_t maximumPackets = 0U) noexcept = 0;
};

} // namespace ESPressio::Radio
