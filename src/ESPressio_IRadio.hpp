#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Receives complete link-layer packets from a concrete radio provider.</summary>
/// <remarks>The packet payload is borrowed and is valid only for the duration of the callback.</remarks>
class IRadioReceiver {
public:
    virtual ~IRadioReceiver() = default;
    virtual void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) = 0;
};

/// <summary>Hardware-neutral bounded-packet radio contract.</summary>
/// <remarks>
/// Implementations transport opaque bytes only. They do not understand ESPressio primitives, application routing,
/// authentication, serialization, or message semantics.
/// </remarks>
class IRadio {
public:
    virtual ~IRadio() = default;

    virtual bool Start() = 0;
    virtual void Stop() noexcept = 0;
    virtual bool IsStarted() const noexcept = 0;

    virtual RadioCapabilities Capabilities() const noexcept = 0;
    virtual RadioAddress LocalAddress() const noexcept = 0;

    virtual RadioSendResult Send(
        const RadioAddress& destination,
        const uint8_t* payload,
        std::size_t payloadSize
    ) = 0;

    virtual void SetReceiver(IRadioReceiver* receiver) noexcept = 0;

    /// <summary>Allows polling-based providers to advance receive/TX work; interrupt-driven providers may no-op.</summary>
    virtual void Poll() {}
};

} // namespace ESPressio::Radio
