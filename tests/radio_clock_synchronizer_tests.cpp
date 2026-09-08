#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioClockSynchronizer.hpp>

using namespace ESPressio;
using namespace ESPressio::Radio;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - AcceptSamples (bool): 1 bytes [0 bytes dynamic allocation]
 * - SubmittedSamples (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - LastSample (Timing::ClockSynchronizationSample<Timing::ClockTick>): 32 bytes [0 bytes dynamic allocation]
 * - LastAdjustmentMode (Timing::ClockSynchronizationAdjustmentMode): 1 bytes [0 bytes dynamic allocation]
 * - _offsetNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - _configuration (Timing::ClockSynchronizationConfig): 8 bytes [0 bytes dynamic allocation]
 * - _status (Timing::ClockSynchronizationStatus<Timing::ClockTick>): 72 bytes [0 bytes dynamic allocation]
 * Total Memory: 136 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class FakeSynchronizationTarget final :
    public Timing::IClockSynchronizationTarget<Timing::ClockTick> {
public:
    explicit FakeSynchronizationTarget(int64_t offsetNanoseconds = 0)
        : _offsetNanoseconds(offsetNanoseconds) {}

    Timing::ClockTick GetSynchronizationTimestampNanoseconds() const override {
        const uint64_t monotonic = System::Clock::Monotonic().NowNanoseconds();
        if (_offsetNanoseconds >= 0) {
            return monotonic + static_cast<uint64_t>(_offsetNanoseconds);
        }
        const uint64_t magnitude = static_cast<uint64_t>(-_offsetNanoseconds);
        return monotonic >= magnitude ? monotonic - magnitude : 0;
    }

    Timing::ClockSynchronizationResult<Timing::ClockTick> SubmitSynchronizationSample(
        const Timing::ClockSynchronizationSample<Timing::ClockTick>& sample,
        Timing::ClockSynchronizationAdjustmentMode adjustmentMode
    ) override {
        LastSample = sample;
        LastAdjustmentMode = adjustmentMode;
        ++SubmittedSamples;
        Timing::ClockSynchronizationResult<Timing::ClockTick> result;
        result.Accepted = AcceptSamples;
        if (result.Accepted) ++_status.AcceptedSampleCount;
        else ++_status.RejectedSampleCount;
        _status.HasAcceptedSample = _status.AcceptedSampleCount != 0;
        return result;
    }

    Timing::ClockSynchronizationStatus<Timing::ClockTick> GetSynchronizationStatus() const override {
        return _status;
    }

    void ConfigureSynchronization(const Timing::ClockSynchronizationConfig& config) override {
        _configuration = config;
    }

    Timing::ClockSynchronizationConfig GetSynchronizationConfig() const override {
        return _configuration;
    }

    void ResetSynchronization() override {
        _status = {};
        SubmittedSamples = 0;
        LastSample = {};
    }

    bool AcceptSamples = true;
    uint32_t SubmittedSamples = 0;
    Timing::ClockSynchronizationSample<Timing::ClockTick> LastSample{};
    Timing::ClockSynchronizationAdjustmentMode LastAdjustmentMode =
        Timing::ClockSynchronizationAdjustmentMode::SlewOnly;

private:
    int64_t _offsetNanoseconds = 0;
    Timing::ClockSynchronizationConfig _configuration{};
    Timing::ClockSynchronizationStatus<Timing::ClockTick> _status{};
};

/// <summary>
/// Host fake that routes recognized control bytes directly to IRadioControlProtocol and records any packet which would
/// otherwise have fallen through to the standard Radio receiver/observer lifecycle.
/// </summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - ControlDeliveries (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - StandardDeliveries (std::uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - _local (RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - _timestamped (bool): 1 bytes [0 bytes dynamic allocation]
 * - _mtu (uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - _exposeReceiveSource (bool): 1 bytes [0 bytes dynamic allocation]
 * - _deliveryEnabled (bool): 1 bytes [0 bytes dynamic allocation]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * - _receiver (IRadioReceiver*): 4 bytes [0 bytes dynamic allocation]
 * - _workSignal (IRadioWorkSignal*): 4 bytes [0 bytes dynamic allocation]
 * - _controlProtocol (IRadioControlProtocol*): 4 bytes [0 bytes dynamic allocation]
 * - _observers (RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * - _peer (FakeClockRadio*): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 52 bytes [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class FakeClockRadio final : public IRadio {
public:
    FakeClockRadio(
        uint8_t addressByte,
        bool timestamped = true,
        uint16_t mtu = 32,
        bool exposeReceiveSource = true
    ) : _local(RadioAddress::FromBytes(&addressByte, 1)),
        _timestamped(timestamped),
        _mtu(mtu),
        _exposeReceiveSource(exposeReceiveSource) {}

    void Connect(FakeClockRadio& peer) noexcept { _peer = &peer; }
    void SetDeliveryEnabled(bool enabled) noexcept { _deliveryEnabled = enabled; }
    void AttachControlProtocol(IRadioControlProtocol& protocol) noexcept { _controlProtocol = &protocol; }

    bool Start() override {
        _started = true;
        _observers.NotifyStarted(*this);
        return true;
    }

    void Stop() noexcept override {
        if (!_started) return;
        _started = false;
        _observers.NotifyStopped(*this);
    }

    bool IsStarted() const noexcept override { return _started; }

    RadioCapabilities Capabilities() const noexcept override {
        RadioCapability flags = RadioCapability::HardwareAddressing;
        if (_timestamped) flags = flags | RadioCapability::ReceiveTimestamp;
        return {flags, _mtu, 1};
    }

    RadioAddress LocalAddress() const noexcept override { return _local; }

    RadioSendResult Send(
        const RadioAddress& destination,
        const uint8_t* payload,
        std::size_t payloadSize
    ) override {
        const auto complete = [&](RadioSendResult result) {
            _observers.NotifySendAttempted(*this, destination, payloadSize, result);
            return result;
        };
        if (!_started) return complete({RadioSendStatus::NotStarted, 0});
        if (payloadSize > _mtu) return complete({RadioSendStatus::PayloadTooLarge, 0});
        if (_peer == nullptr || destination != _peer->_local) {
            return complete({RadioSendStatus::InvalidAddress, 0});
        }
        if (!_deliveryEnabled) return complete(RadioSendResult::Accepted());

        RadioPacketView packet;
        packet.Source = _peer->_exposeReceiveSource ? _local : RadioAddress{};
        packet.Destination = destination;
        packet.Payload = payload;
        packet.PayloadSize = payloadSize;
        packet.ReceiveTimestampNanoseconds = _peer->_timestamped
            ? System::Clock::Monotonic().NowNanoseconds()
            : 0;

        if (_peer->_controlProtocol != nullptr &&
            _peer->_controlProtocol->MatchesControlFrame(*_peer, payload, payloadSize)) {
            ++_peer->ControlDeliveries;
            _peer->_controlProtocol->ProcessControlPacket(*_peer, packet);
        } else {
            ++_peer->StandardDeliveries;
            if (_peer->_receiver != nullptr) _peer->_receiver->OnRadioPacket(*_peer, packet);
            _peer->_observers.NotifyPacketReceived(*_peer, packet);
        }
        return complete(RadioSendResult::Accepted());
    }

    void SetReceiver(IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetWorkSignal(IRadioWorkSignal* signal) noexcept override { _workSignal = signal; }
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

    std::uint32_t ControlDeliveries{0U};
    std::uint32_t StandardDeliveries{0U};

private:
    RadioAddress _local{};
    bool _timestamped = true;
    uint16_t _mtu = 32;
    bool _exposeReceiveSource = true;
    bool _deliveryEnabled = true;
    bool _started = false;
    IRadioReceiver* _receiver = nullptr;
    IRadioWorkSignal* _workSignal = nullptr;
    IRadioControlProtocol* _controlProtocol = nullptr;
    RadioObserverSubscriptions _observers{};
    FakeClockRadio* _peer = nullptr;
};

static void AttachPair(
    FakeClockRadio& clientRadio,
    RadioClockSynchronizer& client,
    FakeClockRadio& referenceRadio,
    RadioClockSynchronizer& reference
) {
    clientRadio.AttachControlProtocol(client);
    referenceRadio.AttachControlProtocol(reference);
}

static void TestFourTimestampExchangeAtNrf24MtuUsesControlPath() {
    FakeClockRadio clientRadio(0xA1, true, 32);
    FakeClockRadio referenceRadio(0xB1, true, 32);
    clientRadio.Connect(referenceRadio);
    referenceRadio.Connect(clientRadio);
    assert(clientRadio.Start());
    assert(referenceRadio.Start());

    FakeSynchronizationTarget clientTarget(0);
    FakeSynchronizationTarget referenceTarget(5000000);
    RadioClockSynchronizer client(clientRadio, &clientTarget);
    RadioClockSynchronizer reference(referenceRadio, &referenceTarget);

    RadioClockSynchronizationConfig referenceConfig;
    referenceConfig.Mode = RadioClockSynchronizationMode::Reference;
    referenceConfig.RequireReceiveTimestamp = true;
    assert(reference.Initialize(referenceConfig));

    RadioClockSynchronizationConfig clientConfig;
    clientConfig.Mode = RadioClockSynchronizationMode::Client;
    clientConfig.ReferencePeer = referenceRadio.LocalAddress();
    clientConfig.RequireReceiveTimestamp = true;
    clientConfig.AdjustmentMode = Timing::ClockSynchronizationAdjustmentMode::StepIfUnsynchronized;
    assert(client.Initialize(clientConfig));
    AttachPair(clientRadio, client, referenceRadio, reference);

    // RadioControlWorker calls ServiceControl periodically; the first client service issues the exchange.
    client.ServiceControl();
    assert(clientTarget.SubmittedSamples == 1);
    assert(clientTarget.LastAdjustmentMode == Timing::ClockSynchronizationAdjustmentMode::StepIfUnsynchronized);
    assert(clientTarget.LastSample.LocalRequestTransmitTime != 0);
    assert(clientTarget.LastSample.RemoteRequestReceiveTime != 0);
    assert(clientTarget.LastSample.RemoteResponseTransmitTime != 0);
    assert(clientTarget.LastSample.LocalResponseReceiveTime != 0);
    assert(clientTarget.LastSample.RemoteRequestReceiveTime > clientTarget.LastSample.LocalRequestTransmitTime);

    const auto clientStats = client.GetStatistics();
    const auto referenceStats = reference.GetStatistics();
    assert(clientStats.RequestsAttempted == 1);
    assert(clientStats.RequestsSent == 1);
    assert(clientStats.ResponsesReceived == 1);
    assert(clientStats.SamplesAccepted == 1);
    assert(clientStats.TimestampFallbacks == 0);
    assert(referenceStats.RequestsReceived == 1);
    assert(referenceStats.ResponsesSent == 1);

    // The request and response were classified as control and never entered the standard packet lifecycle.
    assert(referenceRadio.ControlDeliveries == 1U);
    assert(clientRadio.ControlDeliveries == 1U);
    assert(referenceRadio.StandardDeliveries == 0U);
    assert(clientRadio.StandardDeliveries == 0U);
}

static void TestSourceLessRadioUsesEmbeddedRequesterAddress() {
    FakeClockRadio clientRadio(0xA4, false, 32, false);
    FakeClockRadio referenceRadio(0xB4, false, 32, false);
    clientRadio.Connect(referenceRadio);
    referenceRadio.Connect(clientRadio);
    assert(clientRadio.Start());
    assert(referenceRadio.Start());

    FakeSynchronizationTarget clientTarget;
    FakeSynchronizationTarget referenceTarget(2000000);
    RadioClockSynchronizer client(clientRadio, &clientTarget);
    RadioClockSynchronizer reference(referenceRadio, &referenceTarget);

    RadioClockSynchronizationConfig referenceConfig;
    referenceConfig.Mode = RadioClockSynchronizationMode::Reference;
    assert(reference.Initialize(referenceConfig));

    RadioClockSynchronizationConfig clientConfig;
    clientConfig.Mode = RadioClockSynchronizationMode::Client;
    clientConfig.ReferencePeer = referenceRadio.LocalAddress();
    assert(client.Initialize(clientConfig));
    AttachPair(clientRadio, client, referenceRadio, reference);

    assert(client.RequestSynchronization());
    assert(clientTarget.SubmittedSamples == 1);
    assert(reference.GetStatistics().RequestsReceived == 1);
    assert(reference.GetStatistics().ResponsesSent == 1);
    assert(client.GetStatistics().ResponsesReceived == 1);
    assert(client.GetStatistics().TimestampFallbacks == 1);
    assert(reference.GetStatistics().TimestampFallbacks == 1);
    assert(clientRadio.StandardDeliveries == 0U && referenceRadio.StandardDeliveries == 0U);
}

static void TestTimestampFallbackAndStrictRequirement() {
    FakeClockRadio clientRadio(0xA2, false, 32);
    FakeClockRadio referenceRadio(0xB2, false, 32);
    clientRadio.Connect(referenceRadio);
    referenceRadio.Connect(clientRadio);
    assert(clientRadio.Start());
    assert(referenceRadio.Start());

    FakeSynchronizationTarget clientTarget;
    FakeSynchronizationTarget referenceTarget(1000000);
    RadioClockSynchronizer client(clientRadio, &clientTarget);
    RadioClockSynchronizer reference(referenceRadio, &referenceTarget);

    RadioClockSynchronizationConfig strict;
    strict.Mode = RadioClockSynchronizationMode::Client;
    strict.ReferencePeer = referenceRadio.LocalAddress();
    strict.RequireReceiveTimestamp = true;
    assert(!client.Initialize(strict));

    RadioClockSynchronizationConfig referenceConfig;
    referenceConfig.Mode = RadioClockSynchronizationMode::Reference;
    assert(reference.Initialize(referenceConfig));

    RadioClockSynchronizationConfig clientConfig;
    clientConfig.Mode = RadioClockSynchronizationMode::Client;
    clientConfig.ReferencePeer = referenceRadio.LocalAddress();
    assert(client.Initialize(clientConfig));
    AttachPair(clientRadio, client, referenceRadio, reference);

    assert(client.RequestSynchronization());
    assert(clientTarget.SubmittedSamples == 1);
    assert(client.GetStatistics().TimestampFallbacks == 1);
    assert(reference.GetStatistics().TimestampFallbacks == 1);
}

static void TestOutstandingExchangeIsNotReplaced() {
    FakeClockRadio clientRadio(0xA5, true, 32);
    FakeClockRadio referenceRadio(0xB5, true, 32);
    clientRadio.Connect(referenceRadio);
    referenceRadio.Connect(clientRadio);
    assert(clientRadio.Start());
    assert(referenceRadio.Start());

    // Accept the request at the radio boundary but deliberately withhold delivery/response.
    clientRadio.SetDeliveryEnabled(false);
    FakeSynchronizationTarget clientTarget;
    RadioClockSynchronizer client(clientRadio, &clientTarget);
    RadioClockSynchronizationConfig clientConfig;
    clientConfig.Mode = RadioClockSynchronizationMode::Client;
    clientConfig.ReferencePeer = referenceRadio.LocalAddress();
    clientConfig.RequireReceiveTimestamp = true;
    assert(client.Initialize(clientConfig));

    const auto first = client.RequestSynchronization();
    assert(first.Status == RadioSendStatus::Accepted);
    const auto second = client.RequestSynchronization();
    assert(second.Status == RadioSendStatus::Busy);
    assert(client.GetStatistics().RequestsAttempted == 2U);
    assert(client.GetStatistics().RequestsSent == 1U);
    assert(clientTarget.SubmittedSamples == 0U);
}

static void TestControlMatcherIsBoundedToClockWirePrefix() {
    FakeClockRadio radio(0xA6, true, 32);
    FakeSynchronizationTarget target;
    RadioClockSynchronizer synchronizer(radio, &target);

    const std::uint8_t unrelated[8]{0x52U, 0x54U, 0x01U, 0x01U, 0U, 0U, 0U, 1U};
    assert(!synchronizer.MatchesControlFrame(radio, unrelated, sizeof(unrelated)));
    assert(!synchronizer.MatchesControlFrame(radio, unrelated, 3U));
}

static void TestMtuBelowResponseSizeIsRejected() {
    FakeClockRadio radio(0xA3, true, 31);
    FakeSynchronizationTarget target;
    RadioClockSynchronizer synchronizer(radio, &target);
    RadioClockSynchronizationConfig config;
    config.Mode = RadioClockSynchronizationMode::Reference;
    assert(!synchronizer.Initialize(config));
}

int main() {
    TestFourTimestampExchangeAtNrf24MtuUsesControlPath();
    TestSourceLessRadioUsesEmbeddedRequesterAddress();
    TestTimestampFallbackAndStrictRequirement();
    TestOutstandingExchangeIsNotReplaced();
    TestControlMatcherIsBoundedToClockWirePrefix();
    TestMtuBelowResponseSizeIsRejected();
    return 0;
}
