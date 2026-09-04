#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

namespace {

class EvidenceRadio final : public IRadio {
public:
    enum class Mode : std::uint8_t {
        SubmissionOnly,
        Completed,
        Acknowledged,
        MixedAcknowledgement,
        MixedCompletion
    };

    explicit EvidenceRadio(Mode mode) : _mode(mode) {
        const std::uint8_t address = 0x11;
        _local = RadioAddress::FromBytes(&address, 1);
    }

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }

    RadioCapabilities Capabilities() const noexcept override {
        return {RadioCapability::HardwareAddressing | RadioCapability::LinkAcknowledgement, 16, 1, 4096};
    }
    RadioAddress LocalAddress() const noexcept override { return _local; }

    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override {
        ++_sendCount;
        switch (_mode) {
            case Mode::SubmissionOnly:
                return RadioSendResult::Accepted();
            case Mode::Completed:
                return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
            case Mode::Acknowledged:
                return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged());
            case Mode::MixedAcknowledgement:
                if (_sendCount == 2U) {
                    return RadioSendResult::Accepted({
                        RadioTransmissionCompletion::Completed,
                        RadioPeerAcknowledgement::Unknown
                    });
                }
                return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged());
            case Mode::MixedCompletion:
                if (_sendCount == 2U) return RadioSendResult::Accepted();
                return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged());
        }
        return {RadioSendStatus::NativeFailure, 0};
    }

    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
    std::size_t SendCount() const noexcept { return _sendCount; }

private:
    Mode _mode;
    RadioAddress _local{};
    bool _started{false};
    std::size_t _sendCount{0};
    RadioObserverSubscriptions _observers{};
};

RadioTransportSendResult Send(EvidenceRadio::Mode mode, std::size_t& fragmentCount) {
    EvidenceRadio radio(mode);
    RadioTransport transport;
    assert(transport.AddInterface(radio));
    assert(transport.Start());

    const std::uint8_t destinationByte = 0x22;
    const auto destination = RadioAddress::FromBytes(&destinationByte, 1);
    std::array<std::uint8_t, 12> payload{};
    const auto result = transport.Send(radio, destination, payload.data(), payload.size());
    fragmentCount = radio.SendCount();
    transport.Stop();
    return result;
}

} // namespace

int main() {
    std::size_t fragments = 0;

    const auto submissionOnly = Send(EvidenceRadio::Mode::SubmissionOnly, fragments);
    assert(submissionOnly);
    assert(fragments > 1U);
    assert(!submissionOnly.LinkResult.Evidence.TransmissionCompleted());
    assert(submissionOnly.LinkResult.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);

    const auto completed = Send(EvidenceRadio::Mode::Completed, fragments);
    assert(completed);
    assert(completed.LinkResult.Evidence.TransmissionCompleted());
    assert(!completed.LinkResult.Evidence.PeerAcknowledged());
    assert(completed.LinkResult.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);

    const auto acknowledged = Send(EvidenceRadio::Mode::Acknowledged, fragments);
    assert(acknowledged);
    assert(acknowledged.LinkResult.Evidence.TransmissionCompleted());
    assert(acknowledged.LinkResult.Evidence.PeerAcknowledged());

    const auto mixedAck = Send(EvidenceRadio::Mode::MixedAcknowledgement, fragments);
    assert(mixedAck);
    assert(mixedAck.LinkResult.Evidence.TransmissionCompleted());
    assert(mixedAck.LinkResult.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unknown);

    const auto mixedCompletion = Send(EvidenceRadio::Mode::MixedCompletion, fragments);
    assert(mixedCompletion);
    assert(!mixedCompletion.LinkResult.Evidence.TransmissionCompleted());
    assert(mixedCompletion.LinkResult.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable);

    return 0;
}
