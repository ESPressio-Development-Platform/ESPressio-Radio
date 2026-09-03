#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Observable.hpp>

#include "ESPressio_IRadio.hpp"

#ifndef ESPRESSIO_RADIO_MAX_INTERFACES
#define ESPRESSIO_RADIO_MAX_INTERFACES 4
#endif
#ifndef ESPRESSIO_RADIO_MAX_REASSEMBLIES
#define ESPRESSIO_RADIO_MAX_REASSEMBLIES 4
#endif
#ifndef ESPRESSIO_RADIO_MAX_RECENT_TRANSFERS
#define ESPRESSIO_RADIO_MAX_RECENT_TRANSFERS 32
#endif
#ifndef ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES
#define ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES 4096
#endif

namespace ESPressio::Radio {

class RadioTransport;

/// <summary>Borrowed complete logical transfer reconstructed by the Radio layer.</summary>
/// <remarks>
/// Source and Destination are opaque Radio endpoints. They are not device identities and carry no Mesh routing authority.
/// The payload is valid only for the duration of the receiver callback.
/// </remarks>
struct RadioTransportMessageView {
    RadioAddress Source{};
    RadioAddress Destination{};
    RadioTransferId TransferId = 0;
    const uint8_t* Payload = nullptr;
    std::size_t PayloadSize = 0;
    RadioPacketFlag Flags = RadioPacketFlag::None;
};

/// <summary>Consumes complete direct-link logical transfers after Radio-owned reassembly.</summary>
class IRadioTransportReceiver {
public:
    virtual ~IRadioTransportReceiver() = default;
    virtual void OnRadioTransportMessage(IRadio& radio, const RadioTransportMessageView& message) = 0;
};

enum class RadioTransportSendStatus : uint8_t {
    Accepted,
    NotStarted,
    InterfaceNotRegistered,
    InvalidDestination,
    InvalidPayload,
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

/// <summary>Observes RadioTransport lifecycle transitions.</summary>
class IRadioTransportLifecycleObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioTransportLifecycleObserver() = default;
    virtual void OnRadioTransportStarted(RadioTransport& transport) = 0;
    virtual void OnRadioTransportStopped(RadioTransport& transport) = 0;
};

/// <summary>Observes interface registration changes owned by RadioTransport.</summary>
class IRadioTransportInterfaceObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioTransportInterfaceObserver() = default;
    virtual void OnRadioInterfaceAdded(RadioTransport& transport, IRadio& radio) = 0;
    virtual void OnRadioInterfaceRemoved(RadioTransport& transport, IRadio& radio) = 0;
};

/// <summary>Observes complete logical-transfer send and receive operations.</summary>
class IRadioTransportMessageObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioTransportMessageObserver() = default;
    virtual void OnRadioTransportSendCompleted(
        RadioTransport& transport,
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioTransportSendResult& result
    ) = 0;
    virtual void OnRadioTransportMessageReceived(
        RadioTransport& transport,
        IRadio& radio,
        const RadioTransportMessageView& message
    ) = 0;
};

/// <summary>Typed RAII Observable subscriptions emitted by RadioTransport.</summary>
class RadioTransportObserverSubscriptions final {
private:
    class Dispatcher final : public Observable::Observable {
    public:
        void Started(RadioTransport& transport) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportLifecycleObserver>(
                    [&](IRadioTransportLifecycleObserver* o) { o->OnRadioTransportStarted(transport); });
            });
        }
        void Stopped(RadioTransport& transport) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportLifecycleObserver>(
                    [&](IRadioTransportLifecycleObserver* o) { o->OnRadioTransportStopped(transport); });
            });
        }
        void InterfaceAdded(RadioTransport& transport, IRadio& radio) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportInterfaceObserver>(
                    [&](IRadioTransportInterfaceObserver* o) { o->OnRadioInterfaceAdded(transport, radio); });
            });
        }
        void InterfaceRemoved(RadioTransport& transport, IRadio& radio) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportInterfaceObserver>(
                    [&](IRadioTransportInterfaceObserver* o) { o->OnRadioInterfaceRemoved(transport, radio); });
            });
        }
        void SendCompleted(
            RadioTransport& transport,
            IRadio& radio,
            const RadioAddress& destination,
            std::size_t payloadSize,
            const RadioTransportSendResult& result
        ) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportMessageObserver>(
                    [&](IRadioTransportMessageObserver* o) {
                        o->OnRadioTransportSendCompleted(transport, radio, destination, payloadSize, result);
                    });
            });
        }
        void MessageReceived(RadioTransport& transport, IRadio& radio, const RadioTransportMessageView& message) {
            ExecuteNotification([&](NotificationContext& n) {
                n.WithObservers<IRadioTransportMessageObserver>(
                    [&](IRadioTransportMessageObserver* o) { o->OnRadioTransportMessageReceived(transport, radio, message); });
            });
        }
    };

    using DispatcherOwner = decltype(System::Memory::MakeShared<
        Dispatcher,
        System::Memory::MemoryPolicy::ExternalPreferred
    >());

    DispatcherOwner _dispatcher;

public:
    RadioTransportObserverSubscriptions()
        : _dispatcher(System::Memory::MakeShared<
            Dispatcher,
            System::Memory::MemoryPolicy::ExternalPreferred
        >()) {}

    template<typename... ObserverInterfaces, typename TObserver>
    Observable::ObserverHandlePtr Subscribe(TObserver* observer) {
        return _dispatcher->template RegisterObserverAs<ObserverInterfaces...>(observer);
    }

    void Unsubscribe(Observable::IObserver* observer) { _dispatcher->UnregisterObserver(observer); }
    bool IsSubscribed(Observable::IObserver* observer) { return _dispatcher->IsObserverRegistered(observer); }

    void NotifyStarted(RadioTransport& t) noexcept { try { _dispatcher->Started(t); } catch (...) {} }
    void NotifyStopped(RadioTransport& t) noexcept { try { _dispatcher->Stopped(t); } catch (...) {} }
    void NotifyInterfaceAdded(RadioTransport& t, IRadio& r) noexcept { try { _dispatcher->InterfaceAdded(t, r); } catch (...) {} }
    void NotifyInterfaceRemoved(RadioTransport& t, IRadio& r) noexcept { try { _dispatcher->InterfaceRemoved(t, r); } catch (...) {} }
    void NotifySendCompleted(RadioTransport& t, IRadio& r, const RadioAddress& d, std::size_t s, const RadioTransportSendResult& result) noexcept {
        try { _dispatcher->SendCompleted(t, r, d, s, result); } catch (...) {}
    }
    void NotifyMessageReceived(RadioTransport& t, IRadio& r, const RadioTransportMessageView& m) noexcept {
        try { _dispatcher->MessageReceived(t, r, m); } catch (...) {}
    }
};

/// <summary>
/// Hardware-neutral direct-link logical transfer service for ESPressio radios.
/// </summary>
/// <remarks>
/// RadioTransport owns only bounded hop-local fragmentation/reassembly, duplicate suppression and delivery of complete
/// opaque bytes over a caller-selected Radio interface and next-hop RadioAddress. It deliberately owns no logical-node
/// routing table, no forwarding policy, no Mesh hop limit and no conceptual primitive semantics. Higher layers such as
/// ESPressio-Mesh select the Radio and next hop for every independent delivery.
///
/// Every transport fragment carries the sending RadioAddress inside the Radio-owned framing. This is required for radio
/// technologies such as nRF24 whose hardware receive path does not reveal the transmitter endpoint. A provider-supplied
/// physical Source, when available, is treated as corroborating link evidence and must match the framed source.
/// </remarks>
class RadioTransport final {
private:
    static constexpr uint8_t WireMagic0 = 0xE5u;
    static constexpr uint8_t WireMagic1 = 0x52u;
    static constexpr uint8_t WireVersion = 2u;
    static constexpr std::size_t FixedWireHeaderBytes = 10u;

    struct InterfaceRecord { IRadio* Radio = nullptr; };

    struct WireHeader {
        RadioTransferId TransferId = 0;
        uint8_t FragmentIndex = 0;
        uint8_t FragmentCount = 0;
        uint16_t LogicalPayloadBytes = 0;
        RadioAddress Source{};
        std::size_t EncodedBytes = 0;
    };

    using ByteBuffer = System::Memory::ByteVector<System::Memory::MemoryPolicy::ExternalPreferred>;

    struct ReassemblyRecord {
        bool Used = false;
        IRadio* Radio = nullptr;
        RadioAddress Source{};
        RadioAddress Destination{};
        RadioTransferId TransferId = 0;
        uint8_t FragmentCount = 0;
        uint16_t LogicalPayloadBytes = 0;
        uint16_t ReceivedCount = 0;
        uint32_t Touch = 0;
        RadioPacketFlag Flags = RadioPacketFlag::None;
        std::array<uint8_t, 32> ReceivedBitmap{};
        ByteBuffer Buffer{};

        void Reset() {
            Used = false;
            Radio = nullptr;
            Source = {};
            Destination = {};
            TransferId = 0;
            FragmentCount = 0;
            LogicalPayloadBytes = 0;
            ReceivedCount = 0;
            Touch = 0;
            Flags = RadioPacketFlag::None;
            ReceivedBitmap.fill(0);
            Buffer.clear();
        }

        bool HasFragment(uint8_t index) const noexcept {
            return (ReceivedBitmap[index / 8u] & static_cast<uint8_t>(1u << (index % 8u))) != 0;
        }

        void MarkFragment(uint8_t index) noexcept {
            ReceivedBitmap[index / 8u] |= static_cast<uint8_t>(1u << (index % 8u));
        }
    };

    struct RecentTransferRecord {
        bool Used = false;
        IRadio* Radio = nullptr;
        RadioAddress Source{};
        RadioTransferId TransferId = 0;
    };

    IRadioTransportReceiver* _receiver = nullptr;
    RadioTransportObserverSubscriptions _observers{};
    std::array<InterfaceRecord, ESPRESSIO_RADIO_MAX_INTERFACES> _interfaces{};
    std::array<ReassemblyRecord, ESPRESSIO_RADIO_MAX_REASSEMBLIES> _reassemblies{};
    std::array<RecentTransferRecord, ESPRESSIO_RADIO_MAX_RECENT_TRANSFERS> _recent{};
    std::size_t _recentCursor = 0;
    uint32_t _touchCounter = 0;
    RadioTransferId _nextTransferId = 1;
    bool _started = false;

    static uint16_t ReadU16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
    }

    static void WriteU16(uint8_t* p, uint16_t value) noexcept {
        p[0] = static_cast<uint8_t>(value & 0xFFu);
        p[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    static std::size_t HeaderBytesForAddress(const RadioAddress& address) noexcept {
        return address.IsValid() ? FixedWireHeaderBytes + address.Length : 0;
    }

    static bool DecodeHeader(const uint8_t* data, std::size_t size, WireHeader& header) noexcept {
        if (data == nullptr || size < FixedWireHeaderBytes) return false;
        if (data[0] != WireMagic0 || data[1] != WireMagic1 || data[2] != WireVersion) return false;
        header.TransferId = ReadU16(data + 3);
        header.FragmentIndex = data[5];
        header.FragmentCount = data[6];
        header.LogicalPayloadBytes = ReadU16(data + 7);
        const uint8_t sourceLength = data[9];
        if (sourceLength == 0 || sourceLength > MaximumRadioAddressBytes) return false;
        header.EncodedBytes = FixedWireHeaderBytes + sourceLength;
        if (size < header.EncodedBytes) return false;
        header.Source = RadioAddress::FromBytes(data + FixedWireHeaderBytes, sourceLength);
        return header.TransferId != 0 && header.FragmentCount != 0 && header.FragmentIndex < header.FragmentCount && header.Source.IsValid();
    }

    static void EncodeHeader(uint8_t* data, const WireHeader& header) noexcept {
        data[0] = WireMagic0;
        data[1] = WireMagic1;
        data[2] = WireVersion;
        WriteU16(data + 3, header.TransferId);
        data[5] = header.FragmentIndex;
        data[6] = header.FragmentCount;
        WriteU16(data + 7, header.LogicalPayloadBytes);
        data[9] = header.Source.Length;
        std::memcpy(data + FixedWireHeaderBytes, header.Source.Bytes.data(), header.Source.Length);
    }

    static std::size_t FragmentPayloadBytes(const IRadio& radio, const RadioAddress& source) noexcept {
        const std::size_t headerBytes = HeaderBytesForAddress(source);
        const std::size_t mtu = radio.Capabilities().MaximumPayloadBytes;
        return headerBytes == 0 || mtu <= headerBytes ? 0 : mtu - headerBytes;
    }

    InterfaceRecord* FindInterface(IRadio& radio) noexcept {
        for (auto& record : _interfaces) if (record.Radio == &radio) return &record;
        return nullptr;
    }

    const InterfaceRecord* FindInterface(const IRadio& radio) const noexcept {
        for (const auto& record : _interfaces) if (record.Radio == &radio) return &record;
        return nullptr;
    }

    bool WasRecentlyDelivered(IRadio& radio, const RadioAddress& source, RadioTransferId transferId) const noexcept {
        for (const auto& recent : _recent) {
            if (recent.Used && recent.Radio == &radio && recent.TransferId == transferId && recent.Source == source) return true;
        }
        return false;
    }

    void RememberDelivered(IRadio& radio, const RadioAddress& source, RadioTransferId transferId) noexcept {
        auto& slot = _recent[_recentCursor];
        slot.Used = true;
        slot.Radio = &radio;
        slot.Source = source;
        slot.TransferId = transferId;
        _recentCursor = (_recentCursor + 1u) % _recent.size();
    }

    ReassemblyRecord* FindReassembly(IRadio& radio, const RadioAddress& source, RadioTransferId transferId) noexcept {
        for (auto& record : _reassemblies) {
            if (record.Used && record.Radio == &radio && record.TransferId == transferId && record.Source == source) return &record;
        }
        return nullptr;
    }

    ReassemblyRecord* AllocateReassembly() noexcept {
        for (auto& record : _reassemblies) if (!record.Used) return &record;
        ReassemblyRecord* oldest = &_reassemblies[0];
        for (auto& record : _reassemblies) if (record.Touch < oldest->Touch) oldest = &record;
        oldest->Reset();
        return oldest;
    }

    RadioTransferId NextTransferId() noexcept {
        RadioTransferId result = _nextTransferId++;
        if (result == 0) result = _nextTransferId++;
        if (_nextTransferId == 0) _nextTransferId = 1;
        return result;
    }

public:
    RadioTransport() = default;
    RadioTransport(const RadioTransport&) = delete;
    RadioTransport& operator=(const RadioTransport&) = delete;

    /// <summary>Returns the finite maximum complete logical transfer accepted for one interface.</summary>
    std::size_t MaximumLogicalTransferSize(const IRadio& radio) const noexcept {
        const RadioAddress source = radio.LocalAddress();
        const std::size_t chunk = FragmentPayloadBytes(radio, source);
        if (chunk == 0) return 0;
        std::size_t maximum = std::min<std::size_t>(ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES, chunk * 255u);
        const uint16_t providerMaximum = radio.Capabilities().MaximumLogicalTransferBytes;
        if (providerMaximum != 0) maximum = std::min<std::size_t>(maximum, providerMaximum);
        return maximum;
    }

    /// <summary>Registers one physical/link Radio interface with this logical-transfer service.</summary>
    bool AddInterface(IRadio& radio) noexcept {
        if (!radio.LocalAddress().IsValid()) return false;
        if (FindInterface(radio) != nullptr) return true;
        for (auto& record : _interfaces) {
            if (record.Radio != nullptr) continue;
            record.Radio = &radio;
            _observers.NotifyInterfaceAdded(*this, radio);
            return true;
        }
        return false;
    }

    /// <summary>Removes a Radio interface and any incomplete reassemblies owned by it.</summary>
    bool RemoveInterface(IRadio& radio) noexcept {
        auto* record = FindInterface(radio);
        if (record == nullptr) return false;
        record->Radio = nullptr;
        for (auto& reassembly : _reassemblies) if (reassembly.Radio == &radio) reassembly.Reset();
        for (auto& recent : _recent) if (recent.Radio == &radio) recent = {};
        _observers.NotifyInterfaceRemoved(*this, radio);
        return true;
    }

    /// <summary>Starts all registered Radio interfaces and enables logical transfer.</summary>
    bool Start() {
        if (_started) return true;
        for (const auto& record : _interfaces) {
            if (record.Radio != nullptr && !record.Radio->Start()) {
                for (const auto& rollback : _interfaces) {
                    if (rollback.Radio == record.Radio) break;
                    if (rollback.Radio != nullptr) rollback.Radio->Stop();
                }
                return false;
            }
        }
        _started = true;
        _observers.NotifyStarted(*this);
        return true;
    }

    /// <summary>Stops logical transfer and all registered Radio interfaces.</summary>
    void Stop() noexcept {
        if (!_started) return;
        _started = false;
        for (auto& reassembly : _reassemblies) reassembly.Reset();
        for (auto& recent : _recent) recent = {};
        for (const auto& record : _interfaces) if (record.Radio != nullptr) record.Radio->Stop();
        _observers.NotifyStopped(*this);
    }

    bool IsStarted() const noexcept { return _started; }
    void SetReceiver(IRadioTransportReceiver* receiver) noexcept { _receiver = receiver; }
    RadioTransportObserverSubscriptions& Observers() noexcept { return _observers; }

    /// <summary>Sends one complete immutable opaque logical transfer over the explicitly selected Radio and next-hop endpoint.</summary>
    RadioTransportSendResult Send(
        IRadio& radio,
        const RadioAddress& destination,
        const uint8_t* payload,
        std::size_t payloadSize
    ) {
        const auto complete = [&](RadioTransportSendResult result) {
            _observers.NotifySendCompleted(*this, radio, destination, payloadSize, result);
            return result;
        };

        if (!_started || !radio.IsStarted()) return complete({RadioTransportSendStatus::NotStarted, {RadioSendStatus::NotStarted, 0}});
        if (FindInterface(radio) == nullptr) return complete({RadioTransportSendStatus::InterfaceNotRegistered, {}});
        if (!destination.IsValid()) return complete({RadioTransportSendStatus::InvalidDestination, {RadioSendStatus::InvalidAddress, 0}});
        if (payload == nullptr && payloadSize != 0) return complete({RadioTransportSendStatus::InvalidPayload, {}});

        const RadioAddress source = radio.LocalAddress();
        const std::size_t headerBytes = HeaderBytesForAddress(source);
        const std::size_t chunk = FragmentPayloadBytes(radio, source);
        const std::size_t maximum = MaximumLogicalTransferSize(radio);
        if (headerBytes == 0 || chunk == 0 || payloadSize > maximum || payloadSize > 0xFFFFu)
            return complete({RadioTransportSendStatus::MessageTooLarge, {RadioSendStatus::PayloadTooLarge, 0}});

        const std::size_t fragmentCountValue = payloadSize == 0 ? 1u : ((payloadSize + chunk - 1u) / chunk);
        if (fragmentCountValue == 0 || fragmentCountValue > 255u)
            return complete({RadioTransportSendStatus::MessageTooLarge, {RadioSendStatus::PayloadTooLarge, 0}});

        const RadioTransferId transferId = NextTransferId();
        const uint8_t fragmentCount = static_cast<uint8_t>(fragmentCountValue);
        System::Memory::ByteVector<System::Memory::MemoryPolicy::ExternalPreferred> frame;
        try {
            frame.resize(radio.Capabilities().MaximumPayloadBytes);
        } catch (...) {
            return complete({RadioTransportSendStatus::RadioRejected, {RadioSendStatus::NoMemory, 0}});
        }

        for (uint8_t index = 0; index < fragmentCount; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * chunk;
            const std::size_t remaining = payloadSize > offset ? payloadSize - offset : 0;
            const std::size_t fragmentBytes = std::min(chunk, remaining);
            const WireHeader header{
                transferId,
                index,
                fragmentCount,
                static_cast<uint16_t>(payloadSize),
                source,
                headerBytes
            };
            EncodeHeader(frame.data(), header);
            if (fragmentBytes != 0) std::memcpy(frame.data() + headerBytes, payload + offset, fragmentBytes);
            const auto linkResult = radio.Send(destination, frame.data(), headerBytes + fragmentBytes);
            if (!linkResult) return complete({RadioTransportSendStatus::RadioRejected, linkResult});
        }

        return complete({RadioTransportSendStatus::Accepted, RadioSendResult::Accepted()});
    }

    /// <summary>
    /// Consumes one physical packet on the Radio worker context and advances bounded reassembly.
    /// This is an internal Radio-layer operation and never performs Mesh routing or authentication.
    /// </summary>
    void ProcessInboundPacket(IRadio& radio, const RadioPacketView& packet) {
        if (!_started || FindInterface(radio) == nullptr) return;
        if (!packet.Destination.IsValid()) return;

        WireHeader header;
        if (!DecodeHeader(packet.Payload, packet.PayloadSize, header)) return;
        if (packet.Source.IsValid() && packet.Source != header.Source) return;
        if (WasRecentlyDelivered(radio, header.Source, header.TransferId)) return;

        const std::size_t chunk = FragmentPayloadBytes(radio, header.Source);
        if (chunk == 0 || header.LogicalPayloadBytes > MaximumLogicalTransferSize(radio)) return;
        const std::size_t expectedFragments = header.LogicalPayloadBytes == 0
            ? 1u
            : ((static_cast<std::size_t>(header.LogicalPayloadBytes) + chunk - 1u) / chunk);
        if (expectedFragments != header.FragmentCount) return;

        const std::size_t offset = static_cast<std::size_t>(header.FragmentIndex) * chunk;
        if (offset > header.LogicalPayloadBytes) return;
        const std::size_t expectedBytes = std::min<std::size_t>(chunk, header.LogicalPayloadBytes - offset);
        if (packet.PayloadSize != header.EncodedBytes + expectedBytes) return;

        ReassemblyRecord* record = FindReassembly(radio, header.Source, header.TransferId);
        if (record == nullptr) {
            record = AllocateReassembly();
            record->Used = true;
            record->Radio = &radio;
            record->Source = header.Source;
            record->Destination = packet.Destination;
            record->TransferId = header.TransferId;
            record->FragmentCount = header.FragmentCount;
            record->LogicalPayloadBytes = header.LogicalPayloadBytes;
            record->Flags = packet.Flags;
            try {
                record->Buffer.resize(header.LogicalPayloadBytes);
            } catch (...) {
                record->Reset();
                return;
            }
        } else if (
            record->FragmentCount != header.FragmentCount ||
            record->LogicalPayloadBytes != header.LogicalPayloadBytes ||
            record->Destination != packet.Destination ||
            HasFlag(record->Flags, RadioPacketFlag::Broadcast) != HasFlag(packet.Flags, RadioPacketFlag::Broadcast)
        ) {
            record->Reset();
            return;
        }

        record->Touch = ++_touchCounter;
        if (!record->HasFragment(header.FragmentIndex)) {
            if (expectedBytes != 0) std::memcpy(record->Buffer.data() + offset, packet.Payload + header.EncodedBytes, expectedBytes);
            record->MarkFragment(header.FragmentIndex);
            ++record->ReceivedCount;
        }

        if (record->ReceivedCount != record->FragmentCount) return;

        RadioTransportMessageView message;
        message.Source = record->Source;
        message.Destination = record->Destination;
        message.TransferId = record->TransferId;
        message.Payload = record->Buffer.empty() ? nullptr : record->Buffer.data();
        message.PayloadSize = record->LogicalPayloadBytes;
        message.Flags = record->Flags;

        RememberDelivered(radio, record->Source, record->TransferId);
        if (_receiver != nullptr) _receiver->OnRadioTransportMessage(radio, message);
        _observers.NotifyMessageReceived(*this, radio, message);
        record->Reset();
    }
};

} // namespace ESPressio::Radio
