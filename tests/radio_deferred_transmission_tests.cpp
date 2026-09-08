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
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override {
        return RadioSendResult::Accepted({}, RadioTransmissionHandle{17});
    }
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
 * - Transmission (RadioTransmissionHandle): 4 bytes [0 bytes dynamic allocation]
 * - Destination (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - PayloadSize (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Evidence (RadioDirectLinkEvidence): 2 bytes [0 bytes dynamic allocation]
 * Total Memory: 36 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class Observer final : public IRadioTransmissionObserver {
public:
    void OnRadioTransmissionResolved(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioDirectLinkEvidence& evidence
    ) override {
        ++Calls;
        SeenRadio = &radio;
        Transmission = transmission;
        Destination = destination;
        PayloadSize = payloadSize;
        Evidence = evidence;
    }
    std::size_t Calls{0};
    IRadio* SeenRadio{nullptr};
    RadioTransmissionHandle Transmission{};
    RadioAddress Destination{};
    std::size_t PayloadSize{0};
    RadioDirectLinkEvidence Evidence{};
};
}

int main() {
    TestRadio radio;
    Observer observer;
    auto subscription = radio.Observers().Subscribe<IRadioTransmissionObserver>(&observer);
    assert(subscription);

    const std::uint8_t destinationByte = 0x22;
    const auto destination = RadioAddress::FromBytes(&destinationByte, 1);
    const std::uint8_t payload = 0x55;
    const auto accepted = radio.Send(destination, &payload, 1);
    assert(accepted);
    assert(!accepted.Evidence.IsTerminal());
    assert(accepted.DeferredTransmission == RadioTransmissionHandle{17});

    // Invalid handles and non-terminal observations are not published.
    radio.Observers().NotifyTransmissionResolved(radio, {}, destination, 1, RadioDirectLinkEvidence::Failed());
    radio.Observers().NotifyTransmissionResolved(radio, accepted.DeferredTransmission, destination, 1, {});
    assert(observer.Calls == 0U);

    radio.Observers().NotifyTransmissionResolved(
        radio,
        accepted.DeferredTransmission,
        destination,
        1,
        RadioDirectLinkEvidence::CompletedAndAcknowledged()
    );
    assert(observer.Calls == 1U);
    assert(observer.SeenRadio == &radio);
    assert(observer.Transmission == accepted.DeferredTransmission);
    assert(observer.Destination == destination);
    assert(observer.PayloadSize == 1U);
    assert(observer.Evidence.TransmissionCompleted());
    assert(observer.Evidence.PeerAcknowledged());

    // Failed is also terminal and distinct from unknown/submission-only evidence.
    radio.Observers().NotifyTransmissionResolved(
        radio,
        RadioTransmissionHandle{18},
        destination,
        1,
        RadioDirectLinkEvidence::Failed()
    );
    assert(observer.Calls == 2U);
    assert(observer.Transmission == RadioTransmissionHandle{18});
    assert(observer.Evidence.TransmissionFailed());
    assert(!observer.Evidence.TransmissionCompleted());

    return 0;
}
