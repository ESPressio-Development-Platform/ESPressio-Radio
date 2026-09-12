#include <ESPressio_IRadio.hpp>

#include <cassert>

using namespace ESPressio::Radio;

class DummyRadio final : public IRadio {
    IRadioReceiver* _receiver = nullptr;
    IRadioRuntimeSink* _sink = nullptr;
    bool _started = false;
public:
    bool Start() override { _started = true; if (_sink) _sink->LifecycleAvailabilityChanged(*this); return true; }
    void Stop() noexcept override { _started = false; if (_sink) _sink->LifecycleAvailabilityChanged(*this); }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override {
        return {RadioCapability::HardwareAddressing,32,5,3060};
    }
    RadioAddress LocalAddress() const noexcept override {
        const std::uint8_t bytes[5]{1,2,3,4,5};
        return RadioAddress::FromBytes(bytes,5);
    }
    RadioContentionDomainId ContentionDomain() const noexcept override { return {7}; }
    RadioProviderResourceProfile ProviderResources() const noexcept override { return {3,1,0,0}; }
    bool IsTransmitReady() const noexcept override { return _started; }
    RadioTransmissionCost EstimateTransmissionCost(
        const RadioAddress&,std::size_t payloadBytes,const RadioServiceProfile&) const noexcept override {
        return {payloadBytes+1,(payloadBytes+1)*1000,RadioCostEstimateQuality::ConservativeAirtime};
    }
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t) noexcept override {
        return _started
            ? RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement())
            : RadioSendResult{RadioSendStatus::NotStarted,0};
    }
    void SetReceiver(IRadioReceiver* receiver) noexcept override { _receiver=receiver; }
    void SetRuntimeSink(IRadioRuntimeSink* sink) noexcept override { _sink=sink; }
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t) noexcept override { return {}; }
    bool HasReceiver() const noexcept { return _receiver!=nullptr; }
};

class DummyReceiver final : public IRadioReceiver {
public:
    void OnRadioPacket(IRadio&,const RadioPacketView&,const RadioReceiveTimestampEvidence&) noexcept override {}
};

class DummySink final : public IRadioRuntimeSink {
public:
    unsigned Lifecycle=0;
    void InboundAvailable(IRadio&) noexcept override {}
    void TransmissionResolved(IRadio&,RadioTransmissionHandle,const RadioDirectLinkEvidence&) noexcept override {}
    void TransmitReadinessChanged(IRadio&) noexcept override {}
    void LifecycleAvailabilityChanged(IRadio&) noexcept override { ++Lifecycle; }
};

int main() {
    DummyRadio radio;
    DummyReceiver receiver;
    DummySink sink;
    radio.SetReceiver(&receiver);
    radio.SetRuntimeSink(&sink);
    assert(radio.HasReceiver());
    assert(radio.ProviderResources().HasFiniteIngressService());
    assert(radio.ContentionDomain());
    assert(radio.Start());
    assert(sink.Lifecycle==1);
    const RadioServiceProfile profile{RadioServiceClass::Responsive,RadioDeadlineTreatment::Promotable,
        RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    const auto cost=radio.EstimateTransmissionCost(radio.LocalAddress(),12,profile);
    assert(cost.SupportsPromotableDeadline());
    const auto sent=radio.Send(radio.LocalAddress(),nullptr,0);
    assert(sent && sent.Evidence.TransmissionCompleted());
    radio.Stop();
    assert(sink.Lifecycle==2);
    return 0;
}
