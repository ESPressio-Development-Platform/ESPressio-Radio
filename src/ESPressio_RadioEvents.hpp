#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_Event.hpp>
#include <ESPressio_Memory.hpp>

#include "ESPressio_RadioTransport.hpp"

namespace ESPressio::Event {

using RadioEventPayload = System::Memory::ByteVector<
    System::Memory::MemoryPolicy::ExternalPreferred
>;

/// <summary>Base snapshot shared by concrete-radio lifecycle events.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - LocalAddress (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - Capabilities (Radio::RadioCapabilities): 12 bytes [0 bytes dynamic allocation]
 * Total Memory: 24 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RadioEventSourceSnapshot {
    Radio::RadioAddress LocalAddress{};
    Radio::RadioCapabilities Capabilities{};

    explicit RadioEventSourceSnapshot(Radio::IRadio& radio) noexcept
        : LocalAddress(radio.LocalAddress()), Capabilities(radio.Capabilities()) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * Total Memory: 48 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioStartedEvent final : public TypedEvent<RadioStartedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStartedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * Total Memory: 48 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioStoppedEvent final : public TypedEvent<RadioStoppedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStoppedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted for a physical/link packet observed after RadioTransport has synchronously consumed it.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * - Source (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - Destination (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - RssiDbm (int16_t): 2 bytes [0 bytes dynamic allocation]
 * - ReceiveTimestampNanoseconds (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - Flags (Radio::RadioPacketFlag): 1 bytes [0 bytes dynamic allocation]
 * - Payload (RadioEventPayload): 12 bytes [Capacity * (1 bytes) element storage]
 * Total Memory: 92 bytes [Payload: Capacity * (1 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class RadioPacketReceivedEvent final : public TypedEvent<RadioPacketReceivedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress Source;
    const Radio::RadioAddress Destination;
    const int16_t RssiDbm;
    const uint64_t ReceiveTimestampNanoseconds;
    const Radio::RadioPacketFlag Flags;
    const RadioEventPayload Payload;

    RadioPacketReceivedEvent(Radio::IRadio& radio, const Radio::RadioPacketView& packet)
        : Radio(radio),
          Source(packet.Source),
          Destination(packet.Destination),
          RssiDbm(packet.RssiDbm),
          ReceiveTimestampNanoseconds(packet.ReceiveTimestampNanoseconds),
          Flags(packet.Flags),
          Payload(CopyPayload(packet.Payload, packet.PayloadSize)) {}

private:
    static RadioEventPayload CopyPayload(const uint8_t* payload, std::size_t size) {
        RadioEventPayload result;
        if (payload != nullptr && size != 0) result.assign(payload, payload + size);
        return result;
    }
};

/// <summary>Event emitted after one concrete-radio Send attempt returns.</summary>
/// <remarks>
/// This event is not a transmission-completion event. `Result.Evidence` contains any stronger direct-link fact the
/// provider could synchronously prove; technologies without such proof leave completion/acknowledgement unknown.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * - Destination (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - PayloadSize (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Result (Radio::RadioSendResult): 16 bytes [0 bytes dynamic allocation]
 * Total Memory: 80 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioSendAttemptedEvent final : public TypedEvent<RadioSendAttemptedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress Destination;
    const std::size_t PayloadSize;
    const Radio::RadioSendResult Result;

    RadioSendAttemptedEvent(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioSendResult& result
    ) noexcept : Radio(radio), Destination(destination), PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Event emitted after RadioTransport starts successfully.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 24 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioTransportStartedEvent final : public TypedEvent<RadioTransportStartedEvent> {};

/// <summary>Event emitted after RadioTransport stops.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 24 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioTransportStoppedEvent final : public TypedEvent<RadioTransportStoppedEvent> {};

/// <summary>Event emitted when a physical/link Radio interface is registered with RadioTransport.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * Total Memory: 48 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioInterfaceAddedEvent final : public TypedEvent<RadioInterfaceAddedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioInterfaceAddedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted when a physical/link Radio interface is removed from RadioTransport.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * Total Memory: 48 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioInterfaceRemovedEvent final : public TypedEvent<RadioInterfaceRemovedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioInterfaceRemovedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted after RadioTransport attempts a complete direct-link logical transfer.</summary>
/// <remarks>
/// The result is the synchronous transport-attempt result. `Result.LinkResult.Evidence` must be inspected before
/// interpreting transmission completion or peer acknowledgement.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * - Destination (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - PayloadSize (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Result (Radio::RadioTransportSendResult): 24 bytes [0 bytes dynamic allocation]
 * Total Memory: 88 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioTransportSendAttemptedEvent final : public TypedEvent<RadioTransportSendAttemptedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress Destination;
    const std::size_t PayloadSize;
    const Radio::RadioTransportSendResult Result;

    RadioTransportSendAttemptedEvent(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioTransportSendResult& result
    ) noexcept : Radio(radio), Destination(destination), PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Owned asynchronous snapshot of one complete opaque direct-link Radio transfer.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - Source (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - Destination (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - TransferId (Radio::RadioTransferId): 2 bytes [0 bytes dynamic allocation]
 * - Flags (Radio::RadioPacketFlag): 1 bytes [0 bytes dynamic allocation]
 * - Payload (RadioEventPayload): 12 bytes [Capacity * (1 bytes) element storage]
 * Total Memory: 36 bytes [Payload: Capacity * (1 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
struct RadioTransportMessageEventSnapshot {
    Radio::RadioAddress Source{};
    Radio::RadioAddress Destination{};
    Radio::RadioTransferId TransferId = 0;
    Radio::RadioPacketFlag Flags = Radio::RadioPacketFlag::None;
    RadioEventPayload Payload{};

    explicit RadioTransportMessageEventSnapshot(const Radio::RadioTransportMessageView& message)
        : Source(message.Source),
          Destination(message.Destination),
          TransferId(message.TransferId),
          Flags(message.Flags) {
        if (message.Payload != nullptr && message.PayloadSize != 0) {
            Payload.assign(message.Payload, message.Payload + message.PayloadSize);
        }
    }
};

/// <summary>Event emitted after a complete direct-link logical transfer is delivered by RadioTransport.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Radio (RadioEventSourceSnapshot): 24 bytes [0 bytes dynamic allocation]
 * - Message (RadioTransportMessageEventSnapshot): 36 bytes [Payload: Capacity * (1 bytes) element storage]
 * Total Memory: 84 bytes [Message: Payload: Capacity * (1 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class RadioTransportMessageReceivedEvent final : public TypedEvent<RadioTransportMessageReceivedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const RadioTransportMessageEventSnapshot Message;

    RadioTransportMessageReceivedEvent(
        Radio::IRadio& radio,
        const Radio::RadioTransportMessageView& message
    ) : Radio(radio), Message(message) {}
};

} // namespace ESPressio::Event
