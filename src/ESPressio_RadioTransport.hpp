#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_Memory.hpp>

#include "ESPressio_IRadio.hpp"

#ifndef ESPRESSIO_RADIO_MAX_INTERFACES
#define ESPRESSIO_RADIO_MAX_INTERFACES 4
#endif
#ifndef ESPRESSIO_RADIO_MAX_ROUTES
#define ESPRESSIO_RADIO_MAX_ROUTES 32
#endif
#ifndef ESPRESSIO_RADIO_MAX_REASSEMBLIES
#define ESPRESSIO_RADIO_MAX_REASSEMBLIES 4
#endif
#ifndef ESPRESSIO_RADIO_MAX_RECENT_MESSAGES
#define ESPRESSIO_RADIO_MAX_RECENT_MESSAGES 32
#endif
#ifndef ESPRESSIO_RADIO_MAX_MESSAGE_BYTES
#define ESPRESSIO_RADIO_MAX_MESSAGE_BYTES 4096
#endif

namespace ESPressio::Radio {

/// <summary>Borrowed complete logical message reconstructed by the radio onward-transport layer.</summary>
struct RadioTransportMessageView {
    RadioNodeId SourceNode = 0;
    RadioNodeId DestinationNode = 0;
    RadioChannel Channel = 0;
    RadioMessageId MessageId = 0;
    const uint8_t* Payload = nullptr;
    std::size_t PayloadSize = 0;
};

/// <summary>Consumes complete opaque messages after radio transport/reassembly.</summary>
class IRadioTransportReceiver {
public:
    virtual ~IRadioTransportReceiver() = default;
    virtual void OnRadioTransportMessage(const RadioTransportMessageView& message) = 0;
};

enum class RadioTransportSendStatus : uint8_t {
    Accepted,
    InvalidDestination,
    InvalidPayload,
    NoRoute,
    InterfaceUnavailable,
    MessageTooLarge,
    RadioRejected
};

struct RadioTransportSendResult {
    RadioTransportSendStatus Status = RadioTransportSendStatus::RadioRejected;
    RadioSendResult LinkResult{};

    constexpr explicit operator bool() const noexcept {
        return Status == RadioTransportSendStatus::Accepted;
    }
};

/// <summary>
/// Hardware-neutral radio onward transport. It fragments/reassembles opaque bytes and resolves a final logical node
/// to a radio interface/next-hop address. It deliberately does not inspect primitive payloads or establish trust.
/// </summary>
class RadioTransport final : public IRadioReceiver {
private:
    static constexpr uint8_t WireMagic0 = 0xE5u;
    static constexpr uint8_t WireMagic1 = 0x52u;
    static constexpr uint8_t WireVersion = 1u;
    static constexpr std::size_t WireHeaderBytes = 15u;

    struct InterfaceRecord {
        IRadio* Radio = nullptr;
        bool DefaultRoute = false;
    };

    struct RouteRecord {
        bool Used = false;
        RadioNodeId Destination = 0;
        IRadio* Radio = nullptr;
        RadioAddress NextHop{};
    };

    struct WireHeader {
        RadioChannel Channel = 0;
        RadioNodeId Source = 0;
        RadioNodeId Destination = 0;
        RadioMessageId MessageId = 0;
        uint8_t FragmentIndex = 0;
        uint8_t FragmentCount = 0;
        uint8_t ChunkBytes = 0;
        uint8_t PayloadBytes = 0;
        uint8_t HopLimit = 0;
    };

    using ByteBuffer = System::Memory::ByteVector<System::Memory::MemoryPolicy::ExternalPreferred>;

    struct ReassemblyRecord {
        bool Used = false;
        RadioNodeId Source = 0;
        RadioNodeId Destination = 0;
        RadioChannel Channel = 0;
        RadioMessageId MessageId = 0;
        uint8_t FragmentCount = 0;
        uint8_t ChunkBytes = 0;
        uint8_t HopLimit = 0;
        uint8_t LastPayloadBytes = 0;
        uint16_t ReceivedCount = 0;
        uint32_t Touch = 0;
        ByteBuffer Buffer{};
        ByteBuffer Received{};

        void Reset() {
            Used = false;
            Source = Destination = 0;
            Channel = 0;
            MessageId = 0;
            FragmentCount = ChunkBytes = HopLimit = LastPayloadBytes = 0;
            ReceivedCount = 0;
            Touch = 0;
            Buffer.clear();
            Received.clear();
        }
    };

    struct RecentMessageRecord {
        bool Used = false;
        RadioNodeId Source = 0;
        RadioNodeId Destination = 0;
        RadioChannel Channel = 0;
        RadioMessageId MessageId = 0;
    };

    RadioNodeId _localNode = 0;
    uint8_t _defaultHopLimit = 4;
    RadioMessageId _nextMessageId = 1;
    IRadioTransportReceiver* _receiver = nullptr;
    std::array<InterfaceRecord, ESPRESSIO_RADIO_MAX_INTERFACES> _interfaces{};
    std::array<RouteRecord, ESPRESSIO_RADIO_MAX_ROUTES> _routes{};
    std::array<ReassemblyRecord, ESPRESSIO_RADIO_MAX_REASSEMBLIES> _reassemblies{};
    std::array<RecentMessageRecord, ESPRESSIO_RADIO_MAX_RECENT_MESSAGES> _recent{};
    std::size_t _recentCursor = 0;
    uint32_t _touchCounter = 0;

    static uint16_t ReadU16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
    }
    static void WriteU16(uint8_t* p, uint16_t value) noexcept {
        p[0] = static_cast<uint8_t>(value & 0xFFu);
        p[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    static bool DecodeHeader(const uint8_t* data, std::size_t size, WireHeader& h) noexcept {
        if (data == nullptr || size < WireHeaderBytes) return false;
        if (data[0] != WireMagic0 || data[1] != WireMagic1 || data[2] != WireVersion) return false;
        h.Channel = data[3];
        h.Source = ReadU16(data + 4);
        h.Destination = ReadU16(data + 6);
        h.MessageId = ReadU16(data + 8);
        h.FragmentIndex = data[10];
        h.FragmentCount = data[11];
        h.ChunkBytes = data[12];
        h.PayloadBytes = data[13];
        h.HopLimit = data[14];
        if (h.FragmentCount == 0 || h.FragmentIndex >= h.FragmentCount || h.ChunkBytes == 0) return false;
        if (h.PayloadBytes > h.ChunkBytes || WireHeaderBytes + h.PayloadBytes != size) return false;
        return true;
    }

    static void EncodeHeader(uint8_t* data, const WireHeader& h) noexcept {
        data[0] = WireMagic0;
        data[1] = WireMagic1;
        data[2] = WireVersion;
        data[3] = h.Channel;
        WriteU16(data + 4, h.Source);
        WriteU16(data + 6, h.Destination);
        WriteU16(data + 8, h.MessageId);
        data[10] = h.FragmentIndex;
        data[11] = h.FragmentCount;
        data[12] = h.ChunkBytes;
        data[13] = h.PayloadBytes;
        data[14] = h.HopLimit;
    }

    InterfaceRecord* FindInterface(IRadio* radio) noexcept {
        for (auto& record : _interfaces) if (record.Radio == radio) return &record;
        return nullptr;
    }

    const RouteRecord* FindRoute(RadioNodeId destination) const noexcept {
        for (const auto& route : _routes) {
            if (route.Used && route.Destination == destination && route.Radio != nullptr) return &route;
        }
        return nullptr;
    }

    const InterfaceRecord* FindDefaultInterface() const noexcept {
        for (const auto& record : _interfaces) if (record.Radio != nullptr && record.DefaultRoute) return &record;
        return nullptr;
    }

    static std::size_t FragmentPayloadBytes(const IRadio& radio) noexcept {
        const std::size_t mtu = radio.Capabilities().MaximumPayloadBytes;
        if (mtu <= WireHeaderBytes) return 0;
        return std::min<std::size_t>(255u, mtu - WireHeaderBytes);
    }

    RadioTransportSendResult SendVia(
        IRadio& radio,
        const RadioAddress& nextHop,
        RadioNodeId source,
        RadioNodeId destination,
        RadioChannel channel,
        RadioMessageId messageId,
        uint8_t hopLimit,
        const uint8_t* payload,
        std::size_t payloadSize
    ) {
        if (!radio.IsStarted()) return {RadioTransportSendStatus::InterfaceUnavailable, {RadioSendStatus::NotStarted, 0}};
        const std::size_t chunk = FragmentPayloadBytes(radio);
        if (chunk == 0) return {RadioTransportSendStatus::InterfaceUnavailable, {RadioSendStatus::Unsupported, 0}};
        if (payloadSize > ESPRESSIO_RADIO_MAX_MESSAGE_BYTES) return {RadioTransportSendStatus::MessageTooLarge, {}};
        const std::size_t fragmentCountValue = payloadSize == 0 ? 1u : ((payloadSize + chunk - 1u) / chunk);
        if (fragmentCountValue > 255u) return {RadioTransportSendStatus::MessageTooLarge, {}};

        std::array<uint8_t, WireHeaderBytes + 255u> frame{};
        const uint8_t fragmentCount = static_cast<uint8_t>(fragmentCountValue);
        for (uint8_t index = 0; index < fragmentCount; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * chunk;
            const std::size_t remaining = payloadSize > offset ? payloadSize - offset : 0;
            const uint8_t fragmentPayload = static_cast<uint8_t>(std::min<std::size_t>(chunk, remaining));
            WireHeader header;
            header.Channel = channel;
            header.Source = source;
            header.Destination = destination;
            header.MessageId = messageId;
            header.FragmentIndex = index;
            header.FragmentCount = fragmentCount;
            header.ChunkBytes = static_cast<uint8_t>(chunk);
            header.PayloadBytes = fragmentPayload;
            header.HopLimit = hopLimit;
            EncodeHeader(frame.data(), header);
            if (fragmentPayload != 0) std::memcpy(frame.data() + WireHeaderBytes, payload + offset, fragmentPayload);
            const auto result = radio.Send(nextHop, frame.data(), WireHeaderBytes + fragmentPayload);
            if (!result) return {RadioTransportSendStatus::RadioRejected, result};
        }
        return {RadioTransportSendStatus::Accepted, RadioSendResult::Accepted()};
    }

    RadioTransportSendResult SendFramed(
        RadioNodeId source,
        RadioNodeId destination,
        RadioChannel channel,
        RadioMessageId messageId,
        uint8_t hopLimit,
        const uint8_t* payload,
        std::size_t payloadSize
    ) {
        if (destination == BroadcastNode) {
            bool sent = false;
            RadioTransportSendResult last{RadioTransportSendStatus::NoRoute, {}};
            for (auto& record : _interfaces) {
                if (record.Radio == nullptr || !record.Radio->Capabilities().Has(RadioCapability::Broadcast)) continue;
                const auto address = RadioAddress::Broadcast(record.Radio->Capabilities().AddressBytes);
                const auto result = SendVia(*record.Radio, address, source, destination, channel, messageId, hopLimit, payload, payloadSize);
                if (result) sent = true;
                else last = result;
            }
            return sent ? RadioTransportSendResult{RadioTransportSendStatus::Accepted, RadioSendResult::Accepted()} : last;
        }

        if (const auto* route = FindRoute(destination)) {
            return SendVia(*route->Radio, route->NextHop, source, destination, channel, messageId, hopLimit, payload, payloadSize);
        }
        if (const auto* fallback = FindDefaultInterface()) {
            const auto address = RadioAddress::Broadcast(fallback->Radio->Capabilities().AddressBytes);
            return SendVia(*fallback->Radio, address, source, destination, channel, messageId, hopLimit, payload, payloadSize);
        }
        return {RadioTransportSendStatus::NoRoute, {}};
    }

    bool IsRecent(const WireHeader& h) const noexcept {
        for (const auto& recent : _recent) {
            if (recent.Used && recent.Source == h.Source && recent.Destination == h.Destination &&
                recent.Channel == h.Channel && recent.MessageId == h.MessageId) return true;
        }
        return false;
    }

    void Remember(const WireHeader& h) noexcept {
        auto& recent = _recent[_recentCursor++ % _recent.size()];
        recent = {true, h.Source, h.Destination, h.Channel, h.MessageId};
    }

    ReassemblyRecord* FindOrCreateReassembly(const WireHeader& h) {
        for (auto& slot : _reassemblies) {
            if (slot.Used && slot.Source == h.Source && slot.Destination == h.Destination &&
                slot.Channel == h.Channel && slot.MessageId == h.MessageId) return &slot;
        }
        ReassemblyRecord* candidate = nullptr;
        for (auto& slot : _reassemblies) if (!slot.Used) { candidate = &slot; break; }
        if (candidate == nullptr) {
            candidate = &_reassemblies[0];
            for (auto& slot : _reassemblies) if (slot.Touch < candidate->Touch) candidate = &slot;
            candidate->Reset();
        }
        const std::size_t allocation = static_cast<std::size_t>(h.FragmentCount) * h.ChunkBytes;
        if (allocation > ESPRESSIO_RADIO_MAX_MESSAGE_BYTES) return nullptr;
        candidate->Used = true;
        candidate->Source = h.Source;
        candidate->Destination = h.Destination;
        candidate->Channel = h.Channel;
        candidate->MessageId = h.MessageId;
        candidate->FragmentCount = h.FragmentCount;
        candidate->ChunkBytes = h.ChunkBytes;
        candidate->HopLimit = h.HopLimit;
        candidate->Touch = ++_touchCounter;
        candidate->Buffer.resize(allocation);
        candidate->Received.assign(h.FragmentCount, 0u);
        return candidate;
    }

    void Complete(ReassemblyRecord& slot) {
        WireHeader identity;
        identity.Source = slot.Source;
        identity.Destination = slot.Destination;
        identity.Channel = slot.Channel;
        identity.MessageId = slot.MessageId;
        if (IsRecent(identity)) { slot.Reset(); return; }
        Remember(identity);

        const std::size_t total = slot.FragmentCount == 0 ? 0 :
            (static_cast<std::size_t>(slot.FragmentCount - 1u) * slot.ChunkBytes) + slot.LastPayloadBytes;

        if (slot.Destination == _localNode || slot.Destination == BroadcastNode) {
            if (_receiver != nullptr) {
                const RadioTransportMessageView message{
                    slot.Source, slot.Destination, slot.Channel, slot.MessageId,
                    total == 0 ? nullptr : slot.Buffer.data(), total
                };
                _receiver->OnRadioTransportMessage(message);
            }
        } else if (slot.HopLimit > 0) {
            (void)SendFramed(
                slot.Source, slot.Destination, slot.Channel, slot.MessageId,
                static_cast<uint8_t>(slot.HopLimit - 1u),
                total == 0 ? nullptr : slot.Buffer.data(), total
            );
        }
        slot.Reset();
    }

public:
    explicit RadioTransport(RadioNodeId localNode, uint8_t defaultHopLimit = 4) noexcept
        : _localNode(localNode), _defaultHopLimit(defaultHopLimit) {}

    RadioNodeId LocalNode() const noexcept { return _localNode; }
    void SetReceiver(IRadioTransportReceiver* receiver) noexcept { _receiver = receiver; }

    bool AddInterface(IRadio& radio, bool defaultRoute = false) noexcept {
        if (auto* existing = FindInterface(&radio)) {
            existing->DefaultRoute = defaultRoute;
            radio.SetReceiver(this);
            return true;
        }
        for (auto& record : _interfaces) {
            if (record.Radio == nullptr) {
                record = {&radio, defaultRoute};
                radio.SetReceiver(this);
                return true;
            }
        }
        return false;
    }

    bool SetRoute(RadioNodeId destination, IRadio& radio, const RadioAddress& nextHop) noexcept {
        if (destination == BroadcastNode || !nextHop.IsValid() || FindInterface(&radio) == nullptr) return false;
        for (auto& route : _routes) {
            if (route.Used && route.Destination == destination) {
                route.Radio = &radio;
                route.NextHop = nextHop;
                return true;
            }
        }
        for (auto& route : _routes) {
            if (!route.Used) {
                route.Used = true;
                route.Destination = destination;
                route.Radio = &radio;
                route.NextHop = nextHop;
                return true;
            }
        }
        return false;
    }

    bool RemoveRoute(RadioNodeId destination) noexcept {
        for (auto& route : _routes) {
            if (route.Used && route.Destination == destination) {
                route = RouteRecord{};
                return true;
            }
        }
        return false;
    }

    bool Start() {
        bool success = true;
        for (auto& record : _interfaces) if (record.Radio != nullptr) success = record.Radio->Start() && success;
        return success;
    }

    void Stop() noexcept {
        for (auto& record : _interfaces) if (record.Radio != nullptr) record.Radio->Stop();
        for (auto& slot : _reassemblies) slot.Reset();
    }

    void Poll() {
        for (auto& record : _interfaces) if (record.Radio != nullptr) record.Radio->Poll();
    }

    RadioTransportSendResult Send(
        RadioNodeId destination,
        RadioChannel channel,
        const uint8_t* payload,
        std::size_t payloadSize
    ) {
        if (destination == 0 || destination == _localNode) return {RadioTransportSendStatus::InvalidDestination, {}};
        if (payload == nullptr && payloadSize != 0) return {RadioTransportSendStatus::InvalidPayload, {}};
        RadioMessageId messageId = _nextMessageId++;
        if (_nextMessageId == 0) _nextMessageId = 1;
        return SendFramed(_localNode, destination, channel, messageId, _defaultHopLimit, payload, payloadSize);
    }

    void OnRadioPacket(IRadio&, const RadioPacketView& packet) override {
        WireHeader h;
        if (!DecodeHeader(packet.Payload, packet.PayloadSize, h)) return;
        if (h.Source == 0 || h.Source == _localNode || h.Destination == 0) return;
        if (IsRecent(h)) return;

        ReassemblyRecord* slot = FindOrCreateReassembly(h);
        if (slot == nullptr) return;
        if (slot->FragmentCount != h.FragmentCount || slot->ChunkBytes != h.ChunkBytes) {
            slot->Reset();
            return;
        }
        slot->Touch = ++_touchCounter;
        const std::size_t offset = static_cast<std::size_t>(h.FragmentIndex) * h.ChunkBytes;
        if (offset + h.PayloadBytes > slot->Buffer.size()) { slot->Reset(); return; }
        if (slot->Received[h.FragmentIndex] == 0u) {
            if (h.PayloadBytes != 0) std::memcpy(slot->Buffer.data() + offset, packet.Payload + WireHeaderBytes, h.PayloadBytes);
            slot->Received[h.FragmentIndex] = 1u;
            ++slot->ReceivedCount;
        }
        if (h.FragmentIndex == static_cast<uint8_t>(h.FragmentCount - 1u)) slot->LastPayloadBytes = h.PayloadBytes;
        if (slot->ReceivedCount == slot->FragmentCount) Complete(*slot);
    }
};

} // namespace ESPressio::Radio
