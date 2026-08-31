#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_RadioTypes.hpp"
#include "ESPressio_RadioObservers.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Receives complete link-layer packets from a concrete radio provider.</summary>
/// <remarks>
/// The packet payload is borrowed and is valid only for the duration of the callback. RadioWorker installs itself as
/// the receiver so link callbacks/driver queues are drained on the ESPressio worker thread before RadioTransport sees them.
/// </remarks>
class IRadioReceiver {
public:
    virtual ~IRadioReceiver() = default;
    virtual void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) = 0;
};

/// <summary>Receives a lightweight task-context signal that a radio has queued inbound work.</summary>
/// <remarks>
/// A concrete provider may invoke this after placing data in bounded provider-owned storage when already running in a
/// task/driver-callback context that may safely wake an ESPressio PrecisionThread. A hardware ISR must not invoke this
/// contract directly; ISR-backed providers must defer the wake into an ISR-safe handoff/task context first. The signal
/// itself must remain non-blocking and must never perform packet parsing, routing, authentication, or Foundation-Type work.
/// </remarks>
class IRadioWorkSignal {
public:
    virtual ~IRadioWorkSignal() = default;
    virtual void OnRadioWorkAvailable(IRadio& radio) noexcept = 0;
};

/// <summary>Hardware-neutral bounded-packet radio contract.</summary>
/// <remarks>
/// Implementations transport opaque bytes only. They do not understand ESPressio primitives, application routing,
/// authentication, serialization, or message semantics. Inbound processing is owned by RadioWorker: providers queue
/// callback-driven traffic where necessary and expose that queued/hardware traffic through ProcessInbound().
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

    /// <summary>Installs the worker-owned inbound packet receiver.</summary>
    virtual void SetReceiver(IRadioReceiver* receiver) noexcept = 0;

    /// <summary>Installs the task-context worker wake target used when asynchronous driver callbacks queue inbound work.</summary>
    virtual void SetWorkSignal(IRadioWorkSignal* signal) noexcept = 0;

    /// <summary>
    /// Drains currently available inbound link packets and delivers them to the installed receiver.
    /// This is an internal worker-facing operation and must not create a second scheduling or semantic layer.
    /// </summary>
    virtual void ProcessInbound() = 0;

    /// <summary>Gets the ESPressio Observable callback-subscription surface for this radio.</summary>
    virtual RadioObserverSubscriptions& Observers() noexcept = 0;
};

} // namespace ESPressio::Radio
