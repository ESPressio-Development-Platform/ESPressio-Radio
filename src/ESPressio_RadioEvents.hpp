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

/// <summary>Event emitted after a concrete radio starts successfully.</summary>
class RadioStartedEvent final : public TypedEvent<RadioStartedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStartedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted after a concrete radio stops.</summary>
class RadioStoppedEvent final : public TypedEvent<RadioStoppedEvent> {
public:
    const RadioEventSourceSnapshot Radio;
    explicit RadioStoppedEvent(Radio::IRadio& radio) noexcept : Radio(radio) {}
};

/// <summary>Event emitted for a link packet observed after RadioTransport has synchronously consumed it.</summary>
/// <remarks>The observer payload is borrowed, so this asynchronous event takes one required owned snapshot in externally preferred memory.</remarks>
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

/// <summary>Event emitted after a concrete-radio send attempt completes synchronously.</summary>
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
    ) noexcept :
        Radio(radio), Destination(destination), PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Event emitted after RadioTransport starts successfully.</summary>
class RadioTransportStartedEvent final : public TypedEvent<RadioTransportStartedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    explicit RadioTransportStartedEvent(Radio::RadioTransport& transport) noexcept
        : LocalNode(transport.LocalNode()) {}
};

/// <summary>Event emitted after RadioTransport stops.</summary>
class RadioTransportStoppedEvent final : public TypedEvent<RadioTransportStoppedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    explicit RadioTransportStoppedEvent(Radio::RadioTransport& transport) noexcept
        : LocalNode(transport.LocalNode()) {}
};

/// <summary>Event emitted when a radio interface is attached/configured on RadioTransport.</summary>
class RadioInterfaceConfiguredEvent final : public TypedEvent<RadioInterfaceConfiguredEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const RadioEventSourceSnapshot Radio;
    const bool DefaultRoute;

    RadioInterfaceConfiguredEvent(
        Radio::RadioTransport& transport,
        Radio::IRadio& radio,
        bool defaultRoute
    ) noexcept : LocalNode(transport.LocalNode()), Radio(radio), DefaultRoute(defaultRoute) {}
};

/// <summary>Event emitted when a logical destination route is configured.</summary>
class RadioRouteConfiguredEvent final : public TypedEvent<RadioRouteConfiguredEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const Radio::RadioNodeId DestinationNode;
    const RadioEventSourceSnapshot Radio;
    const Radio::RadioAddress NextHop;

    RadioRouteConfiguredEvent(
        Radio::RadioTransport& transport,
        Radio::RadioNodeId destination,
        Radio::IRadio& radio,
        const Radio::RadioAddress& nextHop
    ) noexcept :
        LocalNode(transport.LocalNode()), DestinationNode(destination), Radio(radio), NextHop(nextHop) {}
};

/// <summary>Event emitted when a logical destination route is removed.</summary>
class RadioRouteRemovedEvent final : public TypedEvent<RadioRouteRemovedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const Radio::RadioNodeId DestinationNode;

    RadioRouteRemovedEvent(Radio::RadioTransport& transport, Radio::RadioNodeId destination) noexcept
        : LocalNode(transport.LocalNode()), DestinationNode(destination) {}
};

/// <summary>Event emitted after RadioTransport attempts an outbound logical-message send.</summary>
class RadioTransportSendCompletedEvent final : public TypedEvent<RadioTransportSendCompletedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const Radio::RadioNodeId DestinationNode;
    const Radio::RadioChannel Channel;
    const std::size_t PayloadSize;
    const Radio::RadioTransportSendResult Result;

    RadioTransportSendCompletedEvent(
        Radio::RadioTransport& transport,
        Radio::RadioNodeId destination,
        Radio::RadioChannel channel,
        std::size_t payloadSize,
        const Radio::RadioTransportSendResult& result
    ) noexcept :
        LocalNode(transport.LocalNode()), DestinationNode(destination), Channel(channel),
        PayloadSize(payloadSize), Result(result) {}
};

/// <summary>Owned asynchronous snapshot of one complete opaque RadioTransport message.</summary>
struct RadioTransportMessageEventSnapshot {
    Radio::RadioNodeId SourceNode = 0;
    Radio::RadioNodeId DestinationNode = 0;
    Radio::RadioChannel Channel = 0;
    Radio::RadioMessageId MessageId = 0;
    RadioEventPayload Payload{};

    explicit RadioTransportMessageEventSnapshot(const Radio::RadioTransportMessageView& message)
        : SourceNode(message.SourceNode),
          DestinationNode(message.DestinationNode),
          Channel(message.Channel),
          MessageId(message.MessageId) {
        if (message.Payload != nullptr && message.PayloadSize != 0) {
            Payload.assign(message.Payload, message.Payload + message.PayloadSize);
        }
    }
};

/// <summary>Event emitted after a complete logical message is delivered locally by RadioTransport.</summary>
class RadioTransportMessageReceivedEvent final : public TypedEvent<RadioTransportMessageReceivedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const RadioTransportMessageEventSnapshot Message;

    RadioTransportMessageReceivedEvent(
        Radio::RadioTransport& transport,
        const Radio::RadioTransportMessageView& message
    ) : LocalNode(transport.LocalNode()), Message(message) {}
};

/// <summary>Event emitted after RadioTransport attempts to forward a complete logical message.</summary>
class RadioTransportMessageForwardedEvent final : public TypedEvent<RadioTransportMessageForwardedEvent> {
public:
    const Radio::RadioNodeId LocalNode;
    const RadioTransportMessageEventSnapshot Message;
    const Radio::RadioTransportSendResult Result;

    RadioTransportMessageForwardedEvent(
        Radio::RadioTransport& transport,
        const Radio::RadioTransportMessageView& message,
        const Radio::RadioTransportSendResult& result
    ) : LocalNode(transport.LocalNode()), Message(message), Result(result) {}
};

} // namespace ESPressio::Event
