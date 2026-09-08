#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

static_assert(
    RadioTransport::ReassemblyPayloadCapacityBytes ==
        static_cast<std::size_t>(ESPRESSIO_RADIO_MAX_REASSEMBLIES) *
        static_cast<std::size_t>(ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES),
    "Radio reassembly payload storage must be exactly build-accountable."
);

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _local (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _mtu (uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - _logicalMaximum (uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _receiver (IRadioReceiver*): 4 bytes [0 bytes dynamic allocation]
 * - _workSignal (IRadioWorkSignal*): 4 bytes [0 bytes dynamic allocation]
 * - _observers (RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * - _peer (FakeRadio*): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 40 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class FakeRadio final : public IRadio {
public:
    explicit FakeRadio(uint8_t addressByte, uint16_t mtu, uint16_t logicalMaximum = 4096)
        : _local(RadioAddress::FromBytes(&addressByte, 1)), _mtu(mtu), _logicalMaximum(logicalMaximum) {}

    bool Start() override {
        if (_started) return true;
        _started = true;
        _observers.NotifyStarted(*this);
        return true;
    }

    void Stop() noexcept override {
        if (!_started) return;
        _started = false;
        _observers.NotifyStopped(*this);
    }

    bool IsStarted() const noexcept override { return _started; }

    RadioCapabilities Capabilities() const noexcept override {
        return {
            RadioCapability::Broadcast | RadioCapability::HardwareAddressing,
            _mtu,
            1,
            _logicalMaximum
        };
    }

    RadioAddress LocalAddress() const noexcept override { return _local; }
    void SetReceiver(IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetWorkSignal(IRadioWorkSignal* signal) noexcept override { _workSignal = signal; }
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
    void Connect(FakeRadio& peer) noexcept { _peer = &peer; }

    RadioSendResult Send(const RadioAddress& destination, const uint8_t* payload, std::size_t size) override {
        const auto complete = [&](RadioSendResult result) {
            _observers.NotifySendAttempted(*this, destination, size, result);
            return result;
        };
        if (!_started) return complete({RadioSendStatus::NotStarted, 0});
        if (size > _mtu) return complete({RadioSendStatus::PayloadTooLarge, 0});
        if (_peer == nullptr || _peer->_receiver == nullptr) return complete({RadioSendStatus::NativeFailure, 0});
        if (destination != _peer->_local && !destination.IsBroadcast()) return complete({RadioSendStatus::InvalidAddress, 0});

        RadioPacketView packet;
        packet.Source = _local;
        packet.Destination = destination;
        packet.Payload = payload;
        packet.PayloadSize = size;
        packet.Flags = destination.IsBroadcast() ? RadioPacketFlag::Broadcast : RadioPacketFlag::None;
        _peer->_receiver->OnRadioPacket(*_peer, packet);
        if (_peer->_workSignal != nullptr) _peer->_workSignal->OnRadioWorkAvailable(*_peer);
        return complete(RadioSendResult::Accepted());
    }

private:
    RadioAddress _local{};
    uint16_t _mtu = 0;
    uint16_t _logicalMaximum = 0;
    bool _started = false;
    IRadioReceiver* _receiver = nullptr;
    IRadioWorkSignal* _workSignal = nullptr;
    RadioObserverSubscriptions _observers{};
    FakeRadio* _peer = nullptr;
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _resolvesAddress (bool): 1 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _local (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _observers (RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 24 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class StartAddressRadio final : public IRadio {
public:
    explicit StartAddressRadio(bool resolvesAddress) : _resolvesAddress(resolvesAddress) {}

    bool Start() override {
        _started = true;
        if (_resolvesAddress) {
            const uint8_t address = 0xD1;
            _local = RadioAddress::FromBytes(&address, 1);
        }
        return true;
    }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override {
        return {RadioCapability::HardwareAddressing, 32, 1, 128};
    }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    RadioSendResult Send(const RadioAddress&, const uint8_t*, std::size_t) override {
        return RadioSendResult::Accepted();
    }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

private:
    bool _resolvesAddress = false;
    bool _started = false;
    RadioAddress _local{};
    RadioObserverSubscriptions _observers{};
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _transport (RadioTransport&): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class TestIngress final : public IRadioReceiver {
public:
    explicit TestIngress(RadioTransport& transport) : _transport(transport) {}

    void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) override {
        _transport.ProcessInboundPacket(radio, packet);
        radio.Observers().NotifyPacketReceived(radio, packet);
    }

private:
    RadioTransport& _transport;
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Interface (IRadio*): 4 bytes [0 bytes dynamic allocation]
 * - SourcePeer (RadioPeerHandle): 4 bytes [0 bytes dynamic allocation]
 * - Source (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - Destination (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - TransferId (RadioTransferId): 2 bytes [0 bytes dynamic allocation]
 * - Flags (RadioPacketFlag): 1 bytes [0 bytes dynamic allocation]
 * - Payload (std::vector<uint8_t>): 12 bytes [Capacity * (1 bytes) element storage]
 * - Count (int): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 52 bytes [Payload: Capacity * (1 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class CaptureReceiver final : public IRadioTransportReceiver {
public:
    void OnRadioTransportMessage(IRadio& radio, const RadioTransportMessageView& message) override {
        Interface = &radio;
        SourcePeer = message.SourcePeer;
        Source = message.Source;
        Destination = message.Destination;
        TransferId = message.TransferId;
        Flags = message.Flags;
        Payload.assign(message.Payload, message.Payload + message.PayloadSize);
        ++Count;
    }

    IRadio* Interface = nullptr;
    RadioPeerHandle SourcePeer{};
    RadioAddress Source{};
    RadioAddress Destination{};
    RadioTransferId TransferId = 0;
    RadioPacketFlag Flags = RadioPacketFlag::None;
    std::vector<uint8_t> Payload;
    int Count = 0;
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 12 bytes [0 bytes dynamic allocation]
 * Members:
 * - Started (int): 4 bytes [0 bytes dynamic allocation]
 * - Stopped (int): 4 bytes [0 bytes dynamic allocation]
 * - Received (int): 4 bytes [0 bytes dynamic allocation]
 * - Sent (int): 4 bytes [0 bytes dynamic allocation]
 * - LastSendAccepted (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 32 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class LinkObserver final :
    public IRadioLifecycleObserver,
    public IRadioPacketObserver,
    public IRadioSendAttemptObserver {
public:
    void OnRadioStarted(IRadio&) override { ++Started; }
    void OnRadioStopped(IRadio&) override { ++Stopped; }
    void OnRadioPacketReceived(IRadio&, const RadioPacketView&) override { ++Received; }
    void OnRadioSendAttempted(IRadio&, const RadioAddress&, std::size_t, const RadioSendResult& result) override {
        ++Sent;
        LastSendAccepted = static_cast<bool>(result);
    }

    int Started = 0;
    int Stopped = 0;
    int Received = 0;
    int Sent = 0;
    bool LastSendAccepted = false;
};

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 16 bytes [0 bytes dynamic allocation]
 * Members:
 * - Started (int): 4 bytes [0 bytes dynamic allocation]
 * - Stopped (int): 4 bytes [0 bytes dynamic allocation]
 * - InterfacesAdded (int): 4 bytes [0 bytes dynamic allocation]
 * - InterfacesRemoved (int): 4 bytes [0 bytes dynamic allocation]
 * - PeersObserved (int): 4 bytes [0 bytes dynamic allocation]
 * - PeersInvalidated (int): 4 bytes [0 bytes dynamic allocation]
 * - Sends (int): 4 bytes [0 bytes dynamic allocation]
 * - Received (int): 4 bytes [0 bytes dynamic allocation]
 * - LastSendAccepted (bool): 1 bytes [0 bytes dynamic allocation]
 * - LastPeerInterface (IRadio*): 4 bytes [0 bytes dynamic allocation]
 * - LastPeer (RadioPeerHandle): 4 bytes [0 bytes dynamic allocation]
 * - LastPeerAddress (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - LastInvalidationReason (RadioPeerInvalidationReason): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 72 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class TransportObserver final :
    public IRadioTransportLifecycleObserver,
    public IRadioTransportInterfaceObserver,
    public IRadioTransportPeerObserver,
    public IRadioTransportMessageObserver {
public:
    void OnRadioTransportStarted(RadioTransport&) override { ++Started; }
    void OnRadioTransportStopped(RadioTransport&) override { ++Stopped; }
    void OnRadioInterfaceAdded(RadioTransport&, IRadio&) override { ++InterfacesAdded; }
    void OnRadioInterfaceRemoved(RadioTransport&, IRadio&) override { ++InterfacesRemoved; }
    void OnRadioPeerObserved(RadioTransport&, IRadio& radio, RadioPeerHandle peer, const RadioAddress& address) override {
        ++PeersObserved;
        LastPeerInterface = &radio;
        LastPeer = peer;
        LastPeerAddress = address;
    }
    void OnRadioPeerInvalidated(
        RadioTransport&,
        IRadio& radio,
        RadioPeerHandle peer,
        const RadioAddress& address,
        RadioPeerInvalidationReason reason
    ) override {
        ++PeersInvalidated;
        LastPeerInterface = &radio;
        LastPeer = peer;
        LastPeerAddress = address;
        LastInvalidationReason = reason;
    }
    void OnRadioTransportSendAttempted(
        RadioTransport&,
        IRadio&,
        const RadioAddress&,
        std::size_t,
        const RadioTransportSendResult& result
    ) override {
        ++Sends;
        LastSendAccepted = static_cast<bool>(result);
    }
    void OnRadioTransportMessageReceived(RadioTransport&, IRadio&, const RadioTransportMessageView&) override {
        ++Received;
    }

    int Started = 0;
    int Stopped = 0;
    int InterfacesAdded = 0;
    int InterfacesRemoved = 0;
    int PeersObserved = 0;
    int PeersInvalidated = 0;
    int Sends = 0;
    int Received = 0;
    bool LastSendAccepted = false;
    IRadio* LastPeerInterface = nullptr;
    RadioPeerHandle LastPeer{};
    RadioAddress LastPeerAddress{};
    RadioPeerInvalidationReason LastInvalidationReason = RadioPeerInvalidationReason::Explicit;
};

static void TestFragmentedDirectLinkDeliveryAndObservers() {
    FakeRadio radioA(0xA1, 32);
    FakeRadio radioB(0xB1, 32);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    LinkObserver linkA;
    LinkObserver linkB;
    auto linkARegistration = radioA.Observers().Subscribe<IRadioLifecycleObserver, IRadioSendAttemptObserver>(&linkA);
    auto linkBRegistration = radioB.Observers().Subscribe<IRadioLifecycleObserver, IRadioPacketObserver>(&linkB);

    RadioTransport transportA;
    RadioTransport transportB;
    TestIngress ingressA(transportA);
    TestIngress ingressB(transportB);
    TransportObserver observerA;
    TransportObserver observerB;
    auto observerARegistration = transportA.Observers().Subscribe<
        IRadioTransportLifecycleObserver,
        IRadioTransportInterfaceObserver,
        IRadioTransportPeerObserver,
        IRadioTransportMessageObserver
    >(&observerA);
    auto observerBRegistration = transportB.Observers().Subscribe<
        IRadioTransportLifecycleObserver,
        IRadioTransportInterfaceObserver,
        IRadioTransportPeerObserver,
        IRadioTransportMessageObserver
    >(&observerB);

    CaptureReceiver receiver;
    transportB.SetReceiver(&receiver);
    assert(transportA.AddInterface(radioA));
    assert(transportB.AddInterface(radioB));
    radioA.SetReceiver(&ingressA);
    radioB.SetReceiver(&ingressB);
    assert(transportA.Start());
    assert(transportB.Start());

    assert(transportA.MaximumLogicalTransferSize(radioA) == 4096);

    std::vector<uint8_t> payload(100);
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
    const auto result = transportA.Send(radioA, radioB.LocalAddress(), payload.data(), payload.size());
    assert(result);
    assert(receiver.Count == 1);
    assert(receiver.Interface == &radioB);
    assert(receiver.SourcePeer);
    assert(receiver.Source == radioA.LocalAddress());
    assert(receiver.Destination == radioB.LocalAddress());
    assert(receiver.TransferId != 0);
    assert(receiver.Payload == payload);

    const auto* peer = transportB.Peers().Resolve(receiver.SourcePeer);
    assert(peer != nullptr);
    assert(peer->Interface == &radioB);
    assert(peer->Address == radioA.LocalAddress());
    assert(observerB.PeersObserved == 1);
    assert(observerB.LastPeer == receiver.SourcePeer);
    assert(observerB.LastPeerAddress == radioA.LocalAddress());
    assert(observerB.LastPeerInterface == &radioB);

    assert(linkA.Started == 1);
    assert(linkA.Sent > 1);
    assert(linkA.LastSendAccepted);
    assert(linkB.Started == 1);
    assert(linkB.Received == linkA.Sent);
    assert(observerA.InterfacesAdded == 1);
    assert(observerA.Started == 1);
    assert(observerA.Sends == 1);
    assert(observerA.LastSendAccepted);
    assert(observerB.Received == 1);

    const int sendsBeforeUnsubscribe = observerA.Sends;
    observerARegistration.reset();
    assert(transportA.Send(radioA, radioB.LocalAddress(), payload.data(), 1));
    assert(observerA.Sends == sendsBeforeUnsubscribe);

    transportA.Stop();
    transportB.Stop();
    assert(linkA.Stopped == 1);
    assert(linkB.Stopped == 1);
    assert(observerB.PeersInvalidated == 1);
    assert(observerB.LastInvalidationReason == RadioPeerInvalidationReason::TransportStopped);
    assert(observerB.Stopped == 1);
}

static void TestInterfaceMayResolveAddressDuringStart() {
    StartAddressRadio radio(true);
    RadioTransport transport;
    assert(!radio.LocalAddress().IsValid());
    assert(transport.AddInterface(radio));
    assert(transport.Start());
    assert(radio.IsStarted());
    assert(radio.LocalAddress().IsValid());
    transport.Stop();
}

static void TestStartRejectsInterfaceThatNeverResolvesAddress() {
    StartAddressRadio radio(false);
    RadioTransport transport;
    assert(transport.AddInterface(radio));
    assert(!transport.Start());
    assert(!transport.IsStarted());
    assert(!radio.IsStarted());
}

static void TestPeerHandleSendAndGenerationInvalidation() {
    FakeRadio radioA(0xA3, 64);
    FakeRadio radioB(0xB3, 64);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    RadioTransport transportA;
    RadioTransport transportB;
    TestIngress ingressA(transportA);
    TestIngress ingressB(transportB);
    CaptureReceiver receiverA;
    CaptureReceiver receiverB;
    TransportObserver observerB;
    auto observerRegistration = transportB.Observers().Subscribe<IRadioTransportPeerObserver>(&observerB);
    transportA.SetReceiver(&receiverA);
    transportB.SetReceiver(&receiverB);

    assert(transportA.AddInterface(radioA));
    assert(transportB.AddInterface(radioB));
    radioA.SetReceiver(&ingressA);
    radioB.SetReceiver(&ingressB);
    assert(transportA.Start());
    assert(transportB.Start());

    const std::array<uint8_t, 3> hello{{1, 2, 3}};
    assert(transportA.Send(radioA, radioB.LocalAddress(), hello.data(), hello.size()));
    const RadioPeerHandle peerAFromB = receiverB.SourcePeer;
    assert(peerAFromB);
    assert(observerB.PeersObserved == 1);

    const std::array<uint8_t, 2> reply{{9, 8}};
    assert(transportB.Send(peerAFromB, reply.data(), reply.size()));
    assert(receiverA.Count == 1);
    assert(receiverA.Payload == std::vector<uint8_t>(reply.begin(), reply.end()));
    assert(receiverA.SourcePeer);

    assert(transportB.InvalidatePeer(peerAFromB));
    assert(observerB.PeersInvalidated == 1);
    assert(observerB.LastPeer == peerAFromB);
    assert(observerB.LastInvalidationReason == RadioPeerInvalidationReason::Explicit);
    assert(transportB.Peers().Resolve(peerAFromB) == nullptr);

    assert(transportA.Send(radioA, radioB.LocalAddress(), hello.data(), hello.size()));
    const RadioPeerHandle replacement = receiverB.SourcePeer;
    assert(replacement);
    assert(replacement.Slot == peerAFromB.Slot);
    assert(replacement.Generation != peerAFromB.Generation);
    assert(observerB.PeersObserved == 2);

    transportB.Stop();
    assert(observerB.PeersInvalidated == 2);
    assert(observerB.LastPeer == replacement);
    assert(observerB.LastInvalidationReason == RadioPeerInvalidationReason::TransportStopped);
    const auto stale = transportB.Send(replacement, reply.data(), reply.size());
    assert(!stale);
    assert(stale.Status == RadioTransportSendStatus::InvalidPeer);

    transportA.Stop();
}

static void TestInterfaceRemovalInvalidatesOwnedPeers() {
    FakeRadio radioA(0xA4, 64);
    FakeRadio radioB(0xB4, 64);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    RadioTransport transportA;
    RadioTransport transportB;
    TestIngress ingressA(transportA);
    TestIngress ingressB(transportB);
    CaptureReceiver receiverB;
    TransportObserver observerB;
    auto observerRegistration = transportB.Observers().Subscribe<
        IRadioTransportPeerObserver,
        IRadioTransportInterfaceObserver
    >(&observerB);
    transportB.SetReceiver(&receiverB);

    assert(transportA.AddInterface(radioA));
    assert(transportB.AddInterface(radioB));
    radioA.SetReceiver(&ingressA);
    radioB.SetReceiver(&ingressB);
    assert(transportA.Start());
    assert(transportB.Start());

    const std::array<uint8_t, 1> payload{{7}};
    assert(transportA.Send(radioA, radioB.LocalAddress(), payload.data(), payload.size()));
    const auto peer = receiverB.SourcePeer;
    assert(peer);
    assert(observerB.PeersObserved == 1);

    assert(transportB.RemoveInterface(radioB));
    assert(observerB.PeersInvalidated == 1);
    assert(observerB.LastPeer == peer);
    assert(observerB.LastInvalidationReason == RadioPeerInvalidationReason::InterfaceRemoved);
    assert(observerB.InterfacesRemoved == 1);
    assert(transportB.Peers().Resolve(peer) == nullptr);

    transportA.Stop();
    transportB.Stop();
}

static void TestProviderLogicalMaximumIsEnforced() {
    FakeRadio radioA(0xA2, 64, 128);
    FakeRadio radioB(0xB2, 64, 128);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    RadioTransport transportA;
    RadioTransport transportB;
    TestIngress ingressA(transportA);
    TestIngress ingressB(transportB);
    CaptureReceiver receiver;
    transportB.SetReceiver(&receiver);

    assert(transportA.AddInterface(radioA));
    assert(transportB.AddInterface(radioB));
    radioA.SetReceiver(&ingressA);
    radioB.SetReceiver(&ingressB);
    assert(transportA.Start());
    assert(transportB.Start());
    assert(transportA.MaximumLogicalTransferSize(radioA) == 128);

    std::array<uint8_t, 129> tooLarge{};
    const auto rejected = transportA.Send(radioA, radioB.LocalAddress(), tooLarge.data(), tooLarge.size());
    assert(!rejected);
    assert(rejected.Status == RadioTransportSendStatus::MessageTooLarge);
    assert(receiver.Count == 0);

    std::array<uint8_t, 128> maximum{};
    const auto accepted = transportA.Send(radioA, radioB.LocalAddress(), maximum.data(), maximum.size());
    assert(accepted);
    assert(receiver.Count == 1);
    assert(receiver.SourcePeer);
    assert(receiver.Payload.size() == maximum.size());
}

static void TestReassemblySaturationDoesNotEvictActiveTransfer() {
    FakeRadio radio(0xB4, 16, 64);
    RadioTransport transport;
    CaptureReceiver receiver;
    transport.SetReceiver(&receiver);
    assert(transport.AddInterface(radio));
    assert(transport.Start());

    const uint8_t sourceByte = 0xA4;
    const auto source = RadioAddress::FromBytes(&sourceByte, 1);
    const auto destination = radio.LocalAddress();
    const auto submitFragment = [&](uint16_t transfer, uint8_t fragment, const uint8_t* payload, std::size_t size) {
        std::array<uint8_t, 16> frame{};
        frame[0] = 0xE5;
        frame[1] = 0x52;
        frame[2] = 2;
        frame[3] = static_cast<uint8_t>(transfer & 0xFFU);
        frame[4] = static_cast<uint8_t>(transfer >> 8U);
        frame[5] = fragment;
        frame[6] = 2;
        frame[7] = 6;
        frame[8] = 0;
        frame[9] = 1;
        frame[10] = sourceByte;
        if (size != 0U) std::memcpy(frame.data() + 11, payload, size);
        const RadioPacketView packet{source, destination, frame.data(), 11U + size, 0, 0, RadioPacketFlag::None};
        transport.ProcessInboundPacket(radio, packet);
    };

    const std::array<uint8_t, 5> first{{1, 2, 3, 4, 5}};
    for (uint16_t transfer = 1; transfer <= ESPRESSIO_RADIO_MAX_REASSEMBLIES; ++transfer) {
        submitFragment(transfer, 0, first.data(), first.size());
    }

    // A fifth incomplete transfer is dropped; it must not evict transfer 1.
    submitFragment(static_cast<uint16_t>(ESPRESSIO_RADIO_MAX_REASSEMBLIES + 1), 0, first.data(), first.size());
    const uint8_t last = 6;
    submitFragment(1, 1, &last, 1);
    assert(receiver.Count == 1);
    assert((receiver.Payload == std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
}

int main() {
    TestInterfaceMayResolveAddressDuringStart();
    TestStartRejectsInterfaceThatNeverResolvesAddress();
    TestFragmentedDirectLinkDeliveryAndObservers();
    TestPeerHandleSendAndGenerationInvalidation();
    TestInterfaceRemovalInvalidatesOwnedPeers();
    TestProviderLogicalMaximumIsEnforced();
    TestReassemblySaturationDoesNotEvictActiveTransfer();
    return 0;
}
