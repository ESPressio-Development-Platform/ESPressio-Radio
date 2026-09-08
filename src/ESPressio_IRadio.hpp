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
/// the receiver so link callbacks/driver queues are serviced on the ESPressio worker thread before RadioTransport sees them.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioReceiver {
public:
    virtual ~IRadioReceiver() = default;
    virtual void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) = 0;
};

/// <summary>Receives a lightweight task-context signal that a radio has queued inbound work.</summary>
/// <remarks>
/// A concrete provider may invoke this after placing data in bounded provider-owned storage when already running in a
/// task/driver-callback context that may safely wake an ESPressio worker. A hardware ISR must not invoke this contract
/// directly; ISR-backed providers must defer the wake into an ISR-safe handoff/task context first. The signal itself
/// must remain non-blocking and must never perform packet parsing, routing, authentication, or Foundation-Type work.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioWorkSignal {
public:
    virtual ~IRadioWorkSignal() = default;
    virtual void OnRadioWorkAvailable(IRadio& radio) noexcept = 0;
};

/// <summary>Result of one bounded provider-ingress service quantum.</summary>
/// <remarks>
/// WorkRemaining describes provider-owned ingress that was already queued when the service quantum completed. It lets
/// a worker schedule a continuation without requiring a provider to spin until empty. PacketsProcessed is diagnostic
/// evidence only and need not equal the number of native frames inspected by a provider.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members:
 * - PacketsProcessed (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - WorkRemaining (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RadioIngressServiceResult final {
    std::uint32_t PacketsProcessed{0U};
    bool WorkRemaining{false};
};

/// <summary>Hardware-neutral bounded-packet radio contract.</summary>
/// <remarks>
/// Implementations transport opaque bytes only. They do not understand ESPressio primitives, application routing,
/// authentication, serialization, or message semantics. Inbound processing is owned by RadioWorker: providers queue
/// callback-driven traffic where necessary and expose that queued/hardware traffic only through the worker service API.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
    /// Legacy provider ingress hook. New queued providers should override ServiceInbound() with a bounded service quantum.
    /// </summary>
    virtual void DrainInbound() = 0;

    /// <summary>
    /// Services at most <paramref name="maximumPackets"/> queued inbound packets and reports whether work remains.
    /// </summary>
    /// <remarks>
    /// This is an internal RadioWorker operation, not an application polling API. The default compatibility path calls
    /// DrainInbound() once for providers that pre-date bounded ingress service. Queue-backed providers should override
    /// this method so one worker invocation can never become an unbounded drain-until-empty loop. A value of zero asks
    /// the provider to use its own finite service quantum; it never means unbounded work.
    /// </remarks>
    virtual RadioIngressServiceResult ServiceInbound(std::size_t maximumPackets = 0U) {
        (void)maximumPackets;
        DrainInbound();
        return {};
    }

    /// <summary>Gets the ESPressio Observable callback-subscription surface for this radio.</summary>
    virtual RadioObserverSubscriptions& Observers() noexcept = 0;
};

} // namespace ESPressio::Radio
