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

DeferredLogicalTransferDescriptor Descriptor(TestRadio& radio, std::uint8_t destination, RadioTransferId transferId) {
    return {&radio, {}, RadioAddress::FromBytes(&destination, 1), transferId, 64};
}
}

int main() {
    TestRadio radioA(1);
    TestRadio radioB(2);
    DeferredLogicalTransferTracker<2> tracker;
    IDeferredLogicalTransferTracker& erased = tracker;
    LogicalTransferTerminalEvidence terminal{};

    const auto transfer = erased.Begin(Descriptor(radioA, 9, 1), 3);
    assert(transfer);
    assert(erased.RegisterAcceptedFragment(
        transfer, 0, radioA,
        RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged()), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.RegisterAcceptedFragment(
        transfer, 1, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{11}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.RegisterAcceptedFragment(
        transfer, 2, radioB, RadioSendResult::Accepted({}, RadioTransmissionHandle{12}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.Contains(transfer));

    assert(erased.Resolve(
        radioA, RadioTransmissionHandle{11}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::Pending);
    assert(erased.Resolve(
        radioB, RadioTransmissionHandle{12}, RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);
    assert(terminal.Transfer == transfer);
    assert(terminal.Descriptor.Radio == &radioA);
    assert(terminal.Descriptor.TransferId == 1);
    assert(terminal.Descriptor.PayloadBytes == 64);
    assert(terminal.Evidence.TransmissionCompleted());
    assert(!terminal.Evidence.PeerAcknowledged());
    assert(terminal.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);
    assert(!erased.Contains(transfer));

    const auto failed = erased.Begin(Descriptor(radioA, 9, 2), 2);
    assert(failed);
    assert(erased.RegisterAcceptedFragment(
        failed, 0, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{21}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.RegisterAcceptedFragment(
        failed, 1, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{22}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.Resolve(
        radioA, RadioTransmissionHandle{21}, RadioDirectLinkEvidence::Failed(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);
    assert(terminal.Transfer == failed);
    assert(terminal.Descriptor.TransferId == 2);
    assert(terminal.Evidence.TransmissionFailed());
    assert(erased.Resolve(
        radioA, RadioTransmissionHandle{22}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::UnknownTransmission);

    const auto unobservable = erased.Begin(Descriptor(radioA, 9, 3), 2);
    assert(unobservable);
    assert(erased.RegisterAcceptedFragment(
        unobservable, 0, radioA,
        RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged()), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.RegisterAcceptedFragment(
        unobservable, 1, radioA, RadioSendResult::Accepted(), &terminal
    ) == DeferredFragmentRegistrationResult::LogicalTransferUnobservable);
    assert(!erased.Contains(unobservable));

    const auto scoped = erased.Begin(Descriptor(radioA, 9, 4), 1);
    assert(scoped);
    assert(erased.RegisterAcceptedFragment(
        scoped, 0, radioA, RadioSendResult::Accepted({}, RadioTransmissionHandle{31}), &terminal
    ) == DeferredFragmentRegistrationResult::Registered);
    assert(erased.Resolve(
        radioB, RadioTransmissionHandle{31}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::UnknownTransmission);
    assert(erased.Resolve(
        radioA, RadioTransmissionHandle{31}, RadioDirectLinkEvidence::CompletedAndAcknowledged(), &terminal
    ) == DeferredResolutionResult::LogicalTransferTerminal);

    const auto one = erased.Begin(Descriptor(radioA, 9, 5), 1);
    const auto two = erased.Begin(Descriptor(radioA, 9, 6), 1);
    assert(one && two);
    assert(!erased.Begin(Descriptor(radioA, 9, 7), 1));
    assert(erased.Release(one));
    const auto replacement = erased.Begin(Descriptor(radioA, 9, 8), 1);
    assert(replacement);
    assert(replacement.Slot == one.Slot);
    assert(replacement.Generation != one.Generation);
    assert(!erased.Release(one));
    assert(erased.Release(replacement));
    assert(erased.Release(two));
    assert(erased.Size() == 0U);

    DeferredLogicalTransferDescriptor invalid{};
    assert(!erased.Begin(invalid, 1));
    assert(!erased.Begin(Descriptor(radioA, 9, 9), 0));

    return 0;
}
