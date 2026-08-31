#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include <ESPressio_Radio.hpp>

using namespace ESPressio::Radio;

class FakeRadio final : public IRadio {
public:
    explicit FakeRadio(uint8_t addressByte, uint16_t mtu)
        : _local(RadioAddress::FromBytes(&addressByte, 1)), _mtu(mtu) {}

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override {
        return {RadioCapability::Broadcast | RadioCapability::HardwareAddressing, _mtu, 1};
    }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    void SetReceiver(IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void Connect(FakeRadio& peer) noexcept { _peer = &peer; }

    RadioSendResult Send(const RadioAddress& destination, const uint8_t* payload, std::size_t size) override {
        if (!_started) return {RadioSendStatus::NotStarted, 0};
        if (size > _mtu) return {RadioSendStatus::PayloadTooLarge, 0};
        if (_peer == nullptr || _peer->_receiver == nullptr) return {RadioSendStatus::NativeFailure, 0};
        if (destination != _peer->_local && !destination.IsBroadcast()) return {RadioSendStatus::InvalidAddress, 0};
        RadioPacketView packet;
        packet.Source = _local;
        packet.Destination = destination;
        packet.Payload = payload;
        packet.PayloadSize = size;
        packet.Flags = destination.IsBroadcast() ? RadioPacketFlag::Broadcast : RadioPacketFlag::None;
        _peer->_receiver->OnRadioPacket(*_peer, packet);
        return RadioSendResult::Accepted();
    }

private:
    RadioAddress _local{};
    uint16_t _mtu = 0;
    bool _started = false;
    IRadioReceiver* _receiver = nullptr;
    FakeRadio* _peer = nullptr;
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

static void TestFragmentedDirectDelivery() {
    FakeRadio radioA(0xA1, 32);
    FakeRadio radioB(0xB1, 32);
    radioA.Connect(radioB);
    radioB.Connect(radioA);

    RadioTransport nodeA(1);
    RadioTransport nodeB(2);
    CaptureReceiver receiver;
    nodeB.SetReceiver(&receiver);
    assert(nodeA.AddInterface(radioA));
    assert(nodeB.AddInterface(radioB));
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
}

static void TestCrossRadioForwarding() {
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
    CaptureReceiver receiver;
    nodeC.SetReceiver(&receiver);

    assert(nodeA.AddInterface(radioAB_A));
    assert(nodeB.AddInterface(radioAB_B));
    assert(nodeB.AddInterface(radioBC_B));
    assert(nodeC.AddInterface(radioBC_C));
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
}

int main() {
    TestFragmentedDirectDelivery();
    TestCrossRadioForwarding();
    return 0;
}
