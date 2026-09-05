#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

namespace {
class DeferredRadio final : public IRadio {
public:
    DeferredRadio(std::uint8_t local, std::uint16_t mtu)
        : _local(RadioAddress::FromBytes(&local, 1)), _mtu(mtu) {}

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override {
        return {RadioCapability::HardwareAddressing, _mtu, 1, 256};
    }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override {
        ++SendCount;
        const RadioTransmissionHandle handle{static_cast<std::uint16_t>(100U + SendCount)};
        Handles[SendCount - 1U] = handle;
        return RadioSendResult::Accepted({}, handle);
    }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

    std::size_t SendCount{0};
    std::array<RadioTransmissionHandle, 8> Handles{};
private:
    bool _started{false};
    RadioAddress _local{};
    std::uint16_t _mtu{0};
    RadioObserverSubscriptions _observers{};
};

class TerminalObserver final : public ILogicalTransferTerminalObserver {
public:
    void OnLogicalTransferTerminal(const LogicalTransferTerminalEvidence& terminal) override {
        ++Count;
        Last = terminal;
    }
    std::size_t Count{0};
    LogicalTransferTerminalEvidence Last{};
};
}

int main() {
    DeferredLogicalTransferTracker<1> tracker;
    TerminalObserver terminals;
    RadioTransport transport(tracker, terminals);
    DeferredRadio radio(0x11, 20);
    const std::uint8_t destinationByte = 0x22;
    const auto destination = RadioAddress::FromBytes(&destinationByte, 1);

    assert(transport.AddInterface(radio));
    assert(transport.Start());

    RadioPeerHandle peer{};
    assert(transport.Peers().Observe(radio, destination, peer) == RadioPeerObserveResult::Observed);
    assert(peer);

    // Header is 11 bytes for a one-byte source, leaving nine bytes per physical fragment.
    const std::array<std::uint8_t, 20> payload{};
    const auto first = transport.Send(peer, payload.data(), payload.size());
    assert(first.Status == RadioTransportSendStatus::Accepted);
    assert(first.DeferredTransfer);
    assert(radio.SendCount == 3U);
    assert(tracker.Size() == 1U);
    assert(terminals.Count == 0U);

    // Explicit capacity prevents another logical transfer from partially entering the Radio before correlation exists.
    const auto saturated = transport.Send(peer, payload.data(), payload.size());
    assert(saturated.Status == RadioTransportSendStatus::ResourceUnavailable);
    assert(radio.SendCount == 3U);

    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[0], destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[1], destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminals.Count == 0U);
    assert(tracker.Size() == 1U);

    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[2], destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminals.Count == 1U);
    assert(tracker.Size() == 0U);
    assert(terminals.Last.Descriptor.Peer == peer);
    assert(terminals.Last.Descriptor.Destination == destination);
    assert(terminals.Last.Descriptor.PayloadBytes == payload.size());
    assert(terminals.Last.Evidence.TransmissionCompleted());

    // A new transfer may now enter the released bounded slot. One terminal fragment failure terminates the aggregate.
    const auto second = transport.Send(peer, payload.data(), payload.size());
    assert(second.Status == RadioTransportSendStatus::Accepted);
    assert(second.DeferredTransfer);
    assert(radio.SendCount == 6U);
    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[3], destination, 20, RadioDirectLinkEvidence::Failed());
    assert(terminals.Count == 2U);
    assert(terminals.Last.Evidence.TransmissionFailed());
    assert(tracker.Size() == 0U);

    // Remaining provider handles from the failed logical transfer are stale and cannot emit another aggregate.
    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[4], destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminals.Count == 2U);

    assert(transport.RemoveInterface(radio));
    // Subscription is gone: provider callbacks after interface removal cannot reach the transport tracker bridge.
    radio.Observers().NotifyTransmissionResolved(
        radio, radio.Handles[5], destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminals.Count == 2U);

    return 0;
}
