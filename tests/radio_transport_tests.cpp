#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

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
    uint16_t _logicalMaximum = 0;
    bool _started = false;
    IRadioReceiver* _receiver = nullptr;
    IRadioWorkSignal* _workSignal = nullptr;
    RadioObserverSubscriptions _observers{};
    FakeRadio* _peer = nullptr;
};

/// <summary>Test-only synchronous ingress stand-in for RadioWorker.</summary>
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
    void OnRadioTransportMessage(IRadio& radio, const RadioTransportMessageView& message) override {
        Interface = &radio;
        Source = message.Source;
        Destination = message.Destination;
        TransferId = message.TransferId;
        Flags = message.Flags;
        Payload.assign(message.Payload, message.Payload + message.PayloadSize);
        ++Count;
    }

    IRadio* Interface = nullptr;
    RadioAddress Source{};
    RadioAddress Destination{};
    RadioTransferId TransferId = 0;
    RadioPacketFlag Flags = RadioPacketFlag::None;
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
    public IRadioTransportInterfaceObserver,
    public IRadioTransportMessageObserver {
public:
    void OnRadioTransportStarted(RadioTransport&) override { ++Started; }
    void OnRadioTransportStopped(RadioTransport&) override { ++Stopped; }
    void OnRadioInterfaceAdded(RadioTransport&, IRadio&) override { ++InterfacesAdded; }
    void OnRadioInterfaceRemoved(RadioTransport&, IRadio&) override { ++InterfacesRemoved; }
    void OnRadioTransportSendCompleted(
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
    int Sends = 0;
    int Received = 0;
    bool LastSendAccepted = false;
};

static void TestFragmentedDirectLinkDeliveryAndObservers() {
    FakeRadio radioA(0xA1, 32);
    FakeRadio radioB(0xB1, 32);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    LinkObserver linkA;
    LinkObserver linkB;
    auto linkARegistration = radioA.Observers().Subscribe<IRadioLifecycleObserver, IRadioSendObserver>(&linkA);
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
        IRadioTransportMessageObserver
    >(&observerA);
    auto observerBRegistration = transportB.Observers().Subscribe<
        IRadioTransportLifecycleObserver,
        IRadioTransportInterfaceObserver,
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
    assert(receiver.Source == radioA.LocalAddress());
    assert(receiver.Destination == radioB.LocalAddress());
    assert(receiver.TransferId != 0);
    assert(receiver.Payload == payload);

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
    assert(observerB.Stopped == 1);
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
    assert(receiver.Payload.size() == maximum.size());
}

int main() {
    TestFragmentedDirectLinkDeliveryAndObservers();
    TestProviderLogicalMaximumIsEnforced();
    return 0;
}
