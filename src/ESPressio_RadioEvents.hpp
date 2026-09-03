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
struct RadioEventSourceSnapshot {
    Radio::RadioAddress LocalAddress{};
    Radio::RadioCapabilities Capabilities{};

    explicit RadioEventSourceSnapshot(Radio::IRadio& radio) noexcept
        : LocalAddress(radio.LocalAddress()), Capabilities(radio.Capabilities()) {}
};

class RadioStartedEvent final : public TypedEvent<RadioStartedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStartedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

class RadioStoppedEvent final : public TypedEvent<RadioStoppedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStoppedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted for a physical/link packet observed after RadioTransport has synchronously consumed it.</summary>
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

class RadioSendCompletedEvent final : public TypedEvent<RadioSendCompletedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress Destination;
    const std::size_t PayloadSize;
    const Radio::RadioSendResult Result;

    RadioSendCompletedEvent(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioSendResult& result
    ) noexcept : Radio(radio), Destination(destination), PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Event emitted after RadioTransport starts successfully.</summary>
class RadioTransportStartedEvent final : public TypedEvent<RadioTransportStartedEvent> {};

/// <summary>Event emitted after RadioTransport stops.</summary>
class RadioTransportStoppedEvent final : public TypedEvent<RadioTransportStoppedEvent> {};

/// <summary>Event emitted when a physical/link Radio interface is registered with RadioTransport.</summary>
class RadioInterfaceAddedEvent final : public TypedEvent<RadioInterfaceAddedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioInterfaceAddedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted when a physical/link Radio interface is removed from RadioTransport.</summary>
class RadioInterfaceRemovedEvent final : public TypedEvent<RadioInterfaceRemovedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioInterfaceRemovedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted after RadioTransport attempts a complete direct-link logical transfer.</summary>
class RadioTransportSendCompletedEvent final : public TypedEvent<RadioTransportSendCompletedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress Destination;
    const std::size_t PayloadSize;
    const Radio::RadioTransportSendResult Result;

    RadioTransportSendCompletedEvent(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioTransportSendResult& result
    ) noexcept : Radio(radio), Destination(destination), PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Owned asynchronous snapshot of one complete opaque direct-link Radio transfer.</summary>
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
