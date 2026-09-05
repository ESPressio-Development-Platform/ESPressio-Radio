#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeferredLogicalTransferObserverBridge.hpp>

using namespace ESPressio::Radio;

namespace {
class TestRadio final : public IRadio {
public:
    explicit TestRadio(std::uint8_t address) : _local(RadioAddress::FromBytes(&address, 1)) {}
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override { return {RadioCapability::HardwareAddressing, 32, 1, 256}; }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override { return {}; }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
private:
    bool _started{false};
    RadioAddress _local{};
    RadioObserverSubscriptions _observers{};
};

class TerminalObserver final : public ILogicalTransferTerminalObserver {
public:
    void OnLogicalTransferTerminal(const LogicalTransferTerminalEvidence& terminal) override {
        ++Calls;
        Last = terminal;
    }
    std::size_t Calls{0};
    LogicalTransferTerminalEvidence Last{};
};
}

int main() {
    TestRadio radio(0x11);
    const std::uint8_t destinationByte = 0x22;
    const auto destination = RadioAddress::FromBytes(&destinationByte, 1);

    DeferredLogicalTransferTracker<2> tracker;
    TerminalObserver terminalObserver;
    DeferredLogicalTransferObserverBridge bridge(tracker, terminalObserver);
    auto subscription = radio.Observers().Subscribe<IRadioTransmissionObserver>(&bridge);
    assert(subscription);

    DeferredLogicalTransferDescriptor descriptor;
    descriptor.Radio = &radio;
    descriptor.Destination = destination;
    descriptor.TransferId = 9;
    descriptor.PayloadBytes = 40;
    const auto logical = tracker.Begin(descriptor, 2);
    assert(logical);

    const auto first = RadioSendResult::Accepted({}, RadioTransmissionHandle{101});
    const auto second = RadioSendResult::Accepted({}, RadioTransmissionHandle{102});
    assert(tracker.RegisterAcceptedFragment(logical, 0, radio, first) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(logical, 1, radio, second) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.Contains(logical));

    radio.Observers().NotifyTransmissionResolved(
        radio, RadioTransmissionHandle{101}, destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminalObserver.Calls == 0U);
    assert(tracker.Contains(logical));

    radio.Observers().NotifyTransmissionResolved(
        radio, RadioTransmissionHandle{102}, destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminalObserver.Calls == 1U);
    assert(!tracker.Contains(logical));
    assert(terminalObserver.Last.Descriptor.TransferId == 9U);
    assert(terminalObserver.Last.Evidence.TransmissionCompleted());
    assert(terminalObserver.Last.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);

    // A stale provider-local handle cannot produce another logical terminal observation.
    radio.Observers().NotifyTransmissionResolved(
        radio, RadioTransmissionHandle{102}, destination, 20,
        RadioDirectLinkEvidence::Failed());
    assert(terminalObserver.Calls == 1U);

    // One failed fragment terminates the aggregate immediately; the other deferred handle becomes stale.
    descriptor.TransferId = 10;
    const auto failedLogical = tracker.Begin(descriptor, 2);
    assert(failedLogical);
    assert(tracker.RegisterAcceptedFragment(
        failedLogical, 0, radio,
        RadioSendResult::Accepted({}, RadioTransmissionHandle{201})) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(
        failedLogical, 1, radio,
        RadioSendResult::Accepted({}, RadioTransmissionHandle{202})) == DeferredFragmentRegistrationResult::Registered);

    radio.Observers().NotifyTransmissionResolved(
        radio, RadioTransmissionHandle{201}, destination, 20,
        RadioDirectLinkEvidence::Failed());
    assert(terminalObserver.Calls == 2U);
    assert(terminalObserver.Last.Descriptor.TransferId == 10U);
    assert(terminalObserver.Last.Evidence.TransmissionFailed());
    assert(!tracker.Contains(failedLogical));

    radio.Observers().NotifyTransmissionResolved(
        radio, RadioTransmissionHandle{202}, destination, 20,
        RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    assert(terminalObserver.Calls == 2U);

    return 0;
}
