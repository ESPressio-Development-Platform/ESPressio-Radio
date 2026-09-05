#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_DeferredLogicalTransferTracker.hpp>

using namespace ESPressio::Radio;

namespace {
class TestRadio final : public IRadio {
public:
    explicit TestRadio(std::uint8_t address) { _address = RadioAddress::FromBytes(&address, 1); }
    bool Start() override { return true; }
    void Stop() noexcept override {}
    bool IsStarted() const noexcept override { return true; }
    RadioCapabilities Capabilities() const noexcept override { return {RadioCapability::HardwareAddressing, 32, 1, 256}; }
    RadioAddress LocalAddress() const noexcept override { return _address; }
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override { return {}; }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
private:
    RadioAddress _address{};
    RadioObserverSubscriptions _observers{};
};
}

int main() {
    TestRadio radioA(1);
    TestRadio radioB(2);
    DeferredLogicalTransferTracker<2> tracker;
    LogicalTransferTerminalEvidence terminal{};

    // Mixed synchronous + deferred fragments complete only when every fragment is terminal.
    const auto transfer = tracker.Begin(3);
    assert(transfer);
    assert(tracker.RegisterAcceptedFragment(
        transfer, 0, radioA,
        RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged()),
        &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(
        transfer, 1, radioA,
        RadioSendResult::Accepted({}, RadioTransmissionHandle{11}),
        &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(
        transfer, 2, radioB,
        RadioSendResult::Accepted({}, RadioTransmissionHandle{12}),
        &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.Contains(transfer));

    assert(tracker.Resolve(
        radioA, RadioTransmissionHandle{11}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::Pending);
    assert(tracker.Resolve(
        radioB, RadioTransmissionHandle{12}, RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);
    assert(terminal.Transfer == transfer);
    assert(terminal.Evidence.TransmissionCompleted());
    assert(!terminal.Evidence.PeerAcknowledged());
    assert(terminal.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);
    assert(!tracker.Contains(transfer));

    // One terminal failure terminates the logical transfer; a late duplicate provider observation is unknown.
    const auto failed = tracker.Begin(2);
    assert(failed);
    assert(tracker.RegisterAcceptedFragment(
        failed, 0, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{21}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(
        failed, 1, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{22}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.Resolve(
        radioA, RadioTransmissionHandle{21}, RadioDirectLinkEvidence::Failed(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);
    assert(terminal.Transfer == failed);
    assert(terminal.Evidence.TransmissionFailed());
    assert(tracker.Resolve(
        radioA, RadioTransmissionHandle{22}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::UnknownTransmission);

    // Accepted-but-unobservable fragment prevents fabrication of stronger logical terminal evidence.
    const auto unobservable = tracker.Begin(2);
    assert(unobservable);
    assert(tracker.RegisterAcceptedFragment(
        unobservable, 0, radioA, RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged()), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.RegisterAcceptedFragment(
        unobservable, 1, radioA, RadioSendResult::Accepted(), &terminal
    ) == DeferredFragmentRegistrationResult::LogicalTransferUnobservable);
    assert(!tracker.Contains(unobservable));

    // Correlation is provider-local: identical handle values on another Radio do not match.
    const auto scoped = tracker.Begin(1);
    assert(scoped);
    assert(tracker.RegisterAcceptedFragment(
        scoped, 0, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{31}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(tracker.Resolve(
        radioB, RadioTransmissionHandle{31}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::UnknownTransmission);
    assert(tracker.Resolve(
        radioA, RadioTransmissionHandle{31}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);

    // Capacity is explicit and generation-safe; stale handles cannot affect a reused slot.
    const auto one = tracker.Begin(1);
    const auto two = tracker.Begin(1);
    assert(one && two);
    assert(!tracker.Begin(1));
    assert(tracker.Release(one));
    const auto replacement = tracker.Begin(1);
    assert(replacement);
    assert(replacement.Slot == one.Slot);
    assert(replacement.Generation != one.Generation);
    assert(!tracker.Release(one));
    assert(tracker.Release(replacement));
    assert(tracker.Release(two));
    assert(tracker.Size() == 0U);

    return 0;
}
