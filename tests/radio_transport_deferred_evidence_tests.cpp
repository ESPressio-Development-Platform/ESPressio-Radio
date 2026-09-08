#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioTransport.hpp>

using namespace ESPressio::Radio;

namespace {
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - SendCount (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Handles (std::array<RadioTransmissionHandle, 8>): 32 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _local (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _mtu (std::uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - _observers (RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 60 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
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

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Count (std::size_t): 4 bytes [0 bytes dynamic allocation]
 * - Last (LogicalTransferTerminalEvidence): 32 bytes [0 bytes dynamic allocation]
 * Total Memory: 40 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
