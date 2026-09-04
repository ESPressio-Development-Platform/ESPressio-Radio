#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_IRadio.hpp>

using namespace ESPressio::Radio;

namespace {
class TestRadio final : public IRadio {
public:
    TestRadio() {
        const std::uint8_t local = 0x11;
        _local = RadioAddress::FromBytes(&local, 1);
    }

    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override { return {RadioCapability::HardwareAddressing, 32, 1, 256}; }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override { return RadioSendResult::Accepted(); }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

private:
    bool _started{false};
    RadioAddress _local{};
    RadioObserverSubscriptions _observers{};
};

class SendObserver final : public IRadioSendAttemptObserver {
public:
    void OnRadioSendAttempted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) override {
        ++Calls;
        SeenRadio = &radio;
        Destination = destination;
        PayloadSize = payloadSize;
        Result = result;
    }

    std::size_t Calls{0};
    IRadio* SeenRadio{nullptr};
    RadioAddress Destination{};
    std::size_t PayloadSize{0};
    RadioSendResult Result{};
};
}

int main() {
    TestRadio radio;
    SendObserver observer;
    auto handle = radio.Observers().Subscribe<IRadioSendAttemptObserver>(&observer);
    assert(handle);

    const std::uint8_t destinationByte = 0x22;
    const auto destination = RadioAddress::FromBytes(&destinationByte, 1);

    // Submission-only evidence must remain submission-only.
    radio.Observers().NotifySendAttempted(radio, destination, 7, RadioSendResult::Accepted());
    assert(observer.Calls == 1U);
    assert(observer.SeenRadio == &radio);
    assert(observer.Destination == destination);
    assert(observer.PayloadSize == 7U);
    assert(observer.Result.Status == RadioSendStatus::Accepted);
    assert(!observer.Result.Evidence.TransmissionCompleted());

    // A provider which really proved completion/ack may publish that stronger evidence through the same attempt surface.
    radio.Observers().NotifySendAttempted(
        radio,
        destination,
        9,
        RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedAndAcknowledged())
    );
    assert(observer.Calls == 2U);
    assert(observer.PayloadSize == 9U);
    assert(observer.Result.Evidence.TransmissionCompleted());
    assert(observer.Result.Evidence.PeerAcknowledged());

    // Provider propagation compatibility hook has identical attempt semantics; it is not a consumer completion contract.
    radio.Observers().NotifySendCompleted(radio, destination, 11, RadioSendResult::Accepted());
    assert(observer.Calls == 3U);
    assert(observer.PayloadSize == 11U);
    assert(!observer.Result.Evidence.TransmissionCompleted());

    handle.reset();
    radio.Observers().NotifySendAttempted(radio, destination, 1, RadioSendResult::Accepted());
    assert(observer.Calls == 3U);
    return 0;
}
