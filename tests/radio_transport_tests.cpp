#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

class FakeRadio final : public IRadio {
public:
    explicit FakeRadio(uint8_t addressByte, uint16_t mtu)
        : _local(RadioAddress::FromBytes(&addressByte, 1)), _mtu(mtu) {}

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
        return {RadioCapability::Broadcast | RadioCapability::HardwareAddressing, _mtu, 1};
    }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    void SetReceiver(IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetWorkSignal(IRadioWorkSignal* signal) noexcept override { _workSignal = signal; }
    void ProcessInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
    void Connect(FakeRadio& peer) noexcept { _peer = &peer; }

    RadioSendResult Send(const RadioAddress& destination, const uint8_t* payload, std::size_t size) override {
        const auto complete = [&](RadioSendResult result) {
            _observers.NotifySendCompleted(*this, destination, size, result);
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
    bool _started = false;
    IRadioReceiver* _receiver = nullptr;
    IRadioWorkSignal* _workSignal = nullptr;
    RadioObserverSubscriptions _observers{};
    FakeRadio* _peer = nullptr;
};

/// Test-only synchronous ingress stand-in for RadioWorker. Production code installs RadioWorker itself.
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

class CaptureReceiver final : public IRadioTransportReceiver {
public:
    void OnRadioTransportMessage(const RadioTransportMessageView& message) override {
        Source = message.SourceNode;
        Destination = message.DestinationNode;
        Channel = message.Channel;
        Payload.assign(message.Payload, message.Payload + message.PayloadSize);
        ++Count;
    }
    RadioNodeId Source = 0;
    RadioNodeId Destination = 0;
    RadioChannel Channel = 0;
    std::vector<uint8_t> Payload;
    int Count = 0;
};

class LinkObserver final :
    public IRadioLifecycleObserver,
    public IRadioPacketObserver,
    public IRadioSendObserver {
public:
    void OnRadioStarted(IRadio&) override { ++Started; }
    void OnRadioStopped(IRadio&) override { ++Stopped; }
    void OnRadioPacketReceived(IRadio&, const RadioPacketView&) override { ++Received; }
    void OnRadioSendCompleted(IRadio&, const RadioAddress&, std::size_t, const RadioSendResult& result) override {
        ++Sent;
        LastSendAccepted = static_cast<bool>(result);
    }
    int Started = 0;
    int Stopped = 0;
    int Received = 0;
    int Sent = 0;
    bool LastSendAccepted = false;
};

class TransportObserver final :
    public IRadioTransportLifecycleObserver,
    public IRadioTransportTopologyObserver,
    public IRadioTransportMessageObserver {
public:
    void OnRadioTransportStarted(RadioTransport&) override { ++Started; }
    void OnRadioTransportStopped(RadioTransport&) override { ++Stopped; }
    void OnRadioInterfaceConfigured(RadioTransport&, IRadio&, bool) override { ++Interfaces; }
    void OnRadioRouteConfigured(RadioTransport&, RadioNodeId, IRadio&, const RadioAddress&) override { ++Routes; }
    void OnRadioRouteRemoved(RadioTransport&, RadioNodeId) override { ++RoutesRemoved; }
    void OnRadioTransportSendCompleted(RadioTransport&, RadioNodeId, RadioChannel, std::size_t, const RadioTransportSendResult& result) override {
        ++Sends;
        LastSendAccepted = static_cast<bool>(result);
    }
    void OnRadioTransportMessageReceived(RadioTransport&, const RadioTransportMessageView&) override { ++Received; }
    void OnRadioTransportMessageForwarded(RadioTransport&, const RadioTransportMessageView&, const RadioTransportSendResult& result) override {
        ++Forwarded;
        LastForwardAccepted = static_cast<bool>(result);
    }
    int Started = 0;
    int Stopped = 0;
    int Interfaces = 0;
    int Routes = 0;
    int RoutesRemoved = 0;
    int Sends = 0;
    int Received = 0;
    int Forwarded = 0;
    bool LastSendAccepted = false;
    bool LastForwardAccepted = false;
};

static void TestFragmentedDirectDeliveryAndObservers() {
    FakeRadio radioA(0xA1, 32);
    FakeRadio radioB(0xB1, 32);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    LinkObserver linkA;
    LinkObserver linkB;
    auto linkARegistration = radioA.Observers().Subscribe<IRadioLifecycleObserver, IRadioSendObserver>(&linkA);
    auto linkBRegistration = radioB.Observers().Subscribe<IRadioLifecycleObserver, IRadioPacketObserver>(&linkB);

    RadioTransport nodeA(1);
    RadioTransport nodeB(2);
    TestIngress ingressA(nodeA);
    TestIngress ingressB(nodeB);
    TransportObserver transportA;
    TransportObserver transportB;
    auto transportARegistration = nodeA.Observers().Subscribe<IRadioTransportLifecycleObserver, IRadioTransportTopologyObserver, IRadioTransportMessageObserver>(&transportA);
    auto transportBRegistration = nodeB.Observers().Subscribe<IRadioTransportLifecycleObserver, IRadioTransportTopologyObserver, IRadioTransportMessageObserver>(&transportB);

    CaptureReceiver receiver;
    nodeB.SetReceiver(&receiver);
    assert(nodeA.AddInterface(radioA));
    assert(nodeB.AddInterface(radioB));
    radioA.SetReceiver(&ingressA);
    radioB.SetReceiver(&ingressB);
    assert(nodeA.SetRoute(2, radioA, radioB.LocalAddress()));
    assert(nodeB.SetRoute(1, radioB, radioA.LocalAddress()));
    assert(nodeA.Start());
    assert(nodeB.Start());

    std::vector<uint8_t> payload(100);
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
    const auto result = nodeA.Send(2, 7, payload.data(), payload.size());
    assert(result);
    assert(receiver.Count == 1);
    assert(receiver.Source == 1);
    assert(receiver.Destination == 2);
    assert(receiver.Channel == 7);
    assert(receiver.Payload == payload);

    assert(linkA.Started == 1);
    assert(linkA.Sent > 1);
    assert(linkA.LastSendAccepted);
    assert(linkB.Started == 1);
    assert(linkB.Received == linkA.Sent);
    assert(transportA.Interfaces == 1);
    assert(transportA.Routes == 1);
    assert(transportA.Started == 1);
    assert(transportA.Sends == 1);
    assert(transportA.LastSendAccepted);
    assert(transportB.Received == 1);

    const int sendsBeforeUnsubscribe = transportA.Sends;
    transportARegistration.reset();
    assert(nodeA.Send(2, 7, payload.data(), 1));
    assert(transportA.Sends == sendsBeforeUnsubscribe);

    nodeA.Stop();
    nodeB.Stop();
    assert(linkA.Stopped == 1);
    assert(linkB.Stopped == 1);
    assert(transportB.Stopped == 1);
}

static void TestCrossRadioForwardingObserver() {
    FakeRadio radioAB_A(0xA2, 32);
    FakeRadio radioAB_B(0xB2, 32);
    FakeRadio radioBC_B(0xB3, 64);
    FakeRadio radioBC_C(0xC3, 64);
    radioAB_A.Connect(radioAB_B);
    radioAB_B.Connect(radioAB_A);
    radioBC_B.Connect(radioBC_C);
    radioBC_C.Connect(radioBC_B);

    RadioTransport nodeA(10, 4);
    RadioTransport nodeB(20, 4);
    RadioTransport nodeC(30, 4);
    TestIngress ingressA(nodeA);
    TestIngress ingressB(nodeB);
    TestIngress ingressC(nodeC);
    TransportObserver bridgeObserver;
    auto bridgeRegistration = nodeB.Observers().Subscribe<IRadioTransportMessageObserver>(&bridgeObserver);
    CaptureReceiver receiver;
    nodeC.SetReceiver(&receiver);

    assert(nodeA.AddInterface(radioAB_A));
    assert(nodeB.AddInterface(radioAB_B));
    assert(nodeB.AddInterface(radioBC_B));
    assert(nodeC.AddInterface(radioBC_C));
    radioAB_A.SetReceiver(&ingressA);
    radioAB_B.SetReceiver(&ingressB);
    radioBC_B.SetReceiver(&ingressB);
    radioBC_C.SetReceiver(&ingressC);
    assert(nodeA.SetRoute(30, radioAB_A, radioAB_B.LocalAddress()));
    assert(nodeB.SetRoute(30, radioBC_B, radioBC_C.LocalAddress()));
    assert(nodeC.SetRoute(10, radioBC_C, radioBC_B.LocalAddress()));
    assert(nodeA.Start());
    assert(nodeB.Start());
    assert(nodeC.Start());

    std::array<uint8_t, 73> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(0xF0u ^ i);
    const auto result = nodeA.Send(30, 11, payload.data(), payload.size());
    assert(result);
    assert(receiver.Count == 1);
    assert(receiver.Source == 10);
    assert(receiver.Destination == 30);
    assert(receiver.Channel == 11);
    assert(receiver.Payload.size() == payload.size());
    assert(std::memcmp(receiver.Payload.data(), payload.data(), payload.size()) == 0);
    assert(bridgeObserver.Forwarded == 1);
    assert(bridgeObserver.LastForwardAccepted);
}

int main() {
    TestFragmentedDirectDeliveryAndObservers();
    TestCrossRadioForwardingObserver();
    return 0;
}
