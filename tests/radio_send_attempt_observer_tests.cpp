#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_IRadio.hpp>

using namespace ESPressio::Radio;

namespace {
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _local (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _observers (RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 24 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
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

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Calls (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - SeenRadio (IRadio*): 4 bytes [0 bytes dynamic allocation]
 * - Destination (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - PayloadSize (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Result (RadioSendResult): 16 bytes [0 bytes dynamic allocation]
 * Total Memory: 44 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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

    handle.reset();
    radio.Observers().NotifySendAttempted(radio, destination, 1, RadioSendResult::Accepted());
    assert(observer.Calls == 2U);
    return 0;
}
