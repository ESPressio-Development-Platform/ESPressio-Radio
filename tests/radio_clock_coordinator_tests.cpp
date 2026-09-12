#include <ESPressio_RadioClockCoordinator.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace ESPressio;
using namespace ESPressio::Radio;

class Provider final : public IRadio {
public:
    RadioAddress Address{};
    Provider(){const std::uint8_t bytes[5]{9,8,7,6,5};Address=RadioAddress::FromBytes(bytes,5);}
    bool Start() override{return true;}
    void Stop() noexcept override{}
    bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing|RadioCapability::ReceiveTimestamp,32,5,3060};}
    RadioAddress LocalAddress()const noexcept override{return Address;}
    RadioContentionDomainId ContentionDomain()const noexcept override{return {1};}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,0,0};}
    bool IsTransmitReady()const noexcept override{return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t bytes,const RadioServiceProfile&)const noexcept override{
        return {bytes,50'000,RadioCostEstimateQuality::ConservativeAirtime};
    }
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t)noexcept override{return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());}
    void SetReceiver(IRadioReceiver*)noexcept override{}
    void SetRuntimeSink(IRadioRuntimeSink*)noexcept override{}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
};

class Target final : public Timing::IClockSynchronizationTarget {
public:
    mutable Timing::ClockTimestampCapture<> NextCapture{};
    Timing::ClockSynchronizationProfile Profile{};
    Timing::ClockSynchronizationStatus Status{};
    Timing::ClockSynchronizationObservation<> LastObservation{};
    unsigned Submitted=0,Resets=0,DeadlineMisses=0,ActivityCalls=0;
    std::uint64_t SelectedReference=0;

    Timing::ClockTimestampCapture<> CaptureSynchronizationTimestamp(
        Timing::ClockCaptureQuality quality=Timing::ClockCaptureQuality::SoftwareUnbounded,
        Timing::ClockUncertainty uncertainty={}) const override {
        auto capture=NextCapture;
        capture.Quality=quality;
        capture.Uncertainty=uncertainty;
        return capture;
    }
    Timing::ClockSynchronizationResult SubmitSynchronizationObservation(const Timing::ClockSynchronizationObservation<>& observation) override {
        LastObservation=observation;++Submitted;return {true,Timing::ClockObservationRejection::None,0,0,0,0};
    }
    Timing::ClockSynchronizationStatus GetSynchronizationStatus() const override{return Status;}
    Timing::ClockConfigurationStatus ConfigureSynchronization(const Timing::ClockSynchronizationProfile& profile) override{Profile=profile;return Timing::ClockConfigurationStatus::Success;}
    Timing::ClockSynchronizationProfile GetSynchronizationProfile() const override{return Profile;}
    Timing::ClockConfigurationStatus SelectSynchronizationReference(std::uint64_t reference) override{SelectedReference=reference;Status.ReferenceIdentity=reference;return reference?Timing::ClockConfigurationStatus::Success:Timing::ClockConfigurationStatus::InvalidReference;}
    void SetSynchronizationActivity(bool acquiring,bool) override{++ActivityCalls;Status.HasSynchronizationDeadline=acquiring;}
    void ResetSynchronization() override{++Resets;}
    void RecordSynchronizationDeadlineMiss() override{++DeadlineMisses;}
};

struct Downstream final:IRadioTransferResultSink{
    unsigned Count=0;RadioTransferId Last=0;
    void RadioTransferResolved(const RadioTransferTerminalResult& result)noexcept override{++Count;Last=result.TransferId;}
};

class Scheduler final {
public:
    struct Pending final{
        bool Used=false;
        RadioTransferId Id=0;
        RadioAddress Destination{};
        RadioTransferTiming Timing{};
        std::array<std::uint8_t,RadioClockWireV1::ResponseBytes> Bytes{};
        std::size_t Size=0;
        RadioClockFramePrepareTarget Prepare{};
    };
    std::array<Pending,8> Items{};
    std::size_t Count=0;
    RadioTransferId Next=1;

    RadioTransferSubmissionResult SubmitClockFrame(
        IRadio&,const RadioAddress& destination,const RadioServiceProfile& profile,
        const RadioTransferTiming& timing,const std::uint8_t* frame,std::size_t bytes,
        RadioClockFramePrepareTarget prepare={}) noexcept {
        if(profile.Class!=RadioServiceClass::Clock||!frame||bytes==0||bytes>RadioClockWireV1::ResponseBytes||Count>=Items.size())
            return {RadioSchedulerStatus::InvalidConfiguration,0};
        auto& pending=Items[Count++];pending.Used=true;pending.Id=Next++;pending.Destination=destination;
        pending.Timing=timing;pending.Size=bytes;pending.Prepare=prepare;std::memcpy(pending.Bytes.data(),frame,bytes);
        return {RadioSchedulerStatus::Success,pending.Id};
    }
};

static RadioReceiveTimestampEvidence Evidence(
    std::uint64_t mono,std::uint64_t system,std::uint32_t generation,std::uint64_t uncertainty=100) {
    RadioReceiveTimestampEvidence evidence{};
    evidence.ProviderCaptureCoordinate=mono;
    evidence.MonotonicNanoseconds=mono;
    evidence.ConservativeUncertaintyNanoseconds=uncertainty;
    evidence.ContinuityGeneration=generation;
    evidence.Source=RadioTimestampCaptureSource::Hardware;
    evidence.Quality=RadioTimestampQuality::FiniteBounded;
    evidence.CaptureModel.AnchorMonotonic=mono;
    evidence.CaptureModel.AnchorTime=system;
    evidence.CaptureModel.AnchorUncertainty=Timing::ClockUncertainty::Known(500);
    evidence.HasCaptureModel=true;
    return evidence;
}

static RadioPacketView Packet(const RadioAddress& source,const RadioAddress& destination,const std::uint8_t* bytes,std::size_t size){
    RadioPacketView packet{};packet.Source=source;packet.Destination=destination;packet.Payload=bytes;packet.PayloadSize=size;return packet;
}

int main(){
    Provider provider;Scheduler scheduler;Target target;Downstream downstream;
    const std::uint8_t referenceBytes[5]{1,2,3,4,5};const auto reference=RadioAddress::FromBytes(referenceBytes,5);
    target.Profile.MaximumAcceptedRoundTripDelayNanoseconds=100'000'000;
    target.Status.HasSynchronizationDeadline=true;
    target.Status.NextRequiredSynchronizationMonotonic=1'000'000'000;
    target.Status.Reliability=Timing::TimeReliability::Acquiring;

    RadioClockCoordinator<Scheduler> coordinator(scheduler,provider,target,&downstream);
    RadioClockCoordinatorConfig config{};config.Mode=RadioClockCoordinatorMode::ClientAndReference;
    config.ReferencePeer=reference;config.ReferenceIdentity=0x1234;
    config.LocalTransmitCaptureQuality=Timing::ClockCaptureQuality::SoftwareBounded;
    config.LocalTransmitCaptureUncertainty=Timing::ClockUncertainty::Known(200);
    config.SchedulerSafetyGuardNanoseconds=100'000;
    config.ReferenceProcessingAllowanceNanoseconds=200'000;
    config.ReferenceResponseServiceBudgetNanoseconds=1'000'000;
    assert(coordinator.Initialize(config)==RadioClockCoordinatorStatus::Success);
    assert(target.SelectedReference==0x1234&&target.ActivityCalls==1);

    // Timing's adaptive deadline drives one request campaign; no fixed interval is involved.
    coordinator.ServiceDomain(999'700'000);
    assert(scheduler.Count==1&&coordinator.HasPendingClientExchange());
    auto& requestItem=scheduler.Items[0];
    RadioClockRequestV1 request{};assert(DecodeRadioClockRequestV1(requestItem.Bytes.data(),requestItem.Size,request));
    assert(request.ReplyAddress==provider.LocalAddress());
    target.NextCapture={10'000'000'000ULL,999'100'000ULL,Timing::ClockUncertainty::Known(200),Timing::ClockCaptureQuality::SoftwareBounded};
    assert(requestItem.Prepare.Prepare(requestItem.Prepare.Context,requestItem.Bytes.data(),requestItem.Size,999'100'000));
    coordinator.RadioTransferResolved({requestItem.Id,RadioTransferTerminalStatus::Completed,RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()});
    assert(coordinator.HasPendingClientExchange());

    // A response carries separate remote System and monotonic processing durations and creates one Timing observation.
    RadioClockResponseV1 response{};response.Sequence=request.Sequence;response.T2SystemNanoseconds=20'000'000'000ULL;
    response.RemoteSystemProcessingNanoseconds=300'000;response.RemoteMonotonicProcessingNanoseconds=250'000;
    response.ReferenceReliability=Timing::TimeReliability::Synchronized;response.CaptureQuality=RadioClockCaptureQuality::SoftwareBounded;
    response.ReferenceUncertainty=Timing::ClockUncertainty::Known(50'000);response.CaptureUncertainty=Timing::ClockUncertainty::Known(300);
    std::array<std::uint8_t,RadioClockWireV1::ResponseBytes> responseWire{};assert(EncodeRadioClockResponseV1(response,responseWire.data(),responseWire.size()));
    const auto t4Evidence=Evidence(999'500'000ULL,10'000'400'000ULL,7,150);
    coordinator.OnRadioClockFrame(provider,Packet(reference,provider.LocalAddress(),responseWire.data(),responseWire.size()),t4Evidence);
    assert(target.Submitted==1&&!coordinator.HasPendingClientExchange());
    assert(target.LastObservation.T1.SystemTimeNanoseconds==10'000'000'000ULL);
    assert(target.LastObservation.T2.SystemTimeNanoseconds==20'000'000'000ULL);
    assert(target.LastObservation.T3.SystemTimeNanoseconds==20'000'300'000ULL);
    assert(target.LastObservation.T2.MonotonicTimeNanoseconds==1);
    assert(target.LastObservation.T3.MonotonicTimeNanoseconds==250'001ULL);
    assert(target.LastObservation.T4.SystemTimeNanoseconds==10'000'400'000ULL);
    assert(target.LastObservation.ReferenceIdentity==0x1234);

    // Reference-side request capture is retained, then T3 is patched only at late scheduler preparation.
    RadioClockRequestV1 remoteRequest{77,reference};std::array<std::uint8_t,RadioClockWireV1::MaximumRequestBytes> requestWire{};std::size_t requestWireBytes=0;
    assert(EncodeRadioClockRequestV1(remoteRequest,requestWire.data(),requestWire.size(),requestWireBytes));
    const auto t2Evidence=Evidence(2'000'000'000ULL,30'000'000'000ULL,7,120);
    coordinator.OnRadioClockFrame(provider,Packet(reference,provider.LocalAddress(),requestWire.data(),requestWireBytes),t2Evidence);
    assert(coordinator.HasPendingReferenceResponse());
    coordinator.ServiceDomain(2'000'100'000ULL);assert(scheduler.Count==2);
    auto& responseItem=scheduler.Items[1];assert(responseItem.Size==RadioClockWireV1::ResponseBytes);
    target.Status.Reliability=Timing::TimeReliability::Synchronized;target.Status.CurrentUncertainty=Timing::ClockUncertainty::Known(80'000);
    target.NextCapture={30'000'250'000ULL,2'000'240'000ULL,Timing::ClockUncertainty::Known(200),Timing::ClockCaptureQuality::SoftwareBounded};
    assert(responseItem.Prepare.Prepare(responseItem.Prepare.Context,responseItem.Bytes.data(),responseItem.Size,2'000'240'000ULL));
    RadioClockResponseV1 prepared{};assert(DecodeRadioClockResponseV1(responseItem.Bytes.data(),responseItem.Size,prepared));
    assert(prepared.Sequence==77&&prepared.T2SystemNanoseconds==30'000'000'000ULL);
    assert(prepared.RemoteSystemProcessingNanoseconds==250'000);
    assert(prepared.RemoteMonotonicProcessingNanoseconds==240'000);
    assert(prepared.ReferenceReliability==Timing::TimeReliability::Synchronized);
    assert(prepared.CaptureQuality==RadioClockCaptureQuality::SoftwareBounded);
    coordinator.RadioTransferResolved({responseItem.Id,RadioTransferTerminalStatus::Completed,RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()});
    assert(!coordinator.HasPendingReferenceResponse());

    // Provider timestamp continuity changes invalidate Timing estimator lineage without changing the selected reference.
    response.Sequence=88; // stale and ignored, but generation transition is still observed as provider continuity evidence.
    assert(EncodeRadioClockResponseV1(response,responseWire.data(),responseWire.size()));
    auto changed=Evidence(3'000'000'000ULL,40'000'000'000ULL,8,100);
    coordinator.OnRadioClockFrame(provider,Packet(reference,provider.LocalAddress(),responseWire.data(),responseWire.size()),changed);
    assert(target.Resets==1);

    // Non-Clock transfer results remain transparent to the coordinator.
    coordinator.RadioTransferResolved({999,RadioTransferTerminalStatus::Completed,RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()});
    assert(downstream.Count==1&&downstream.Last==999);

    const auto stats=coordinator.Statistics();assert(stats.ClientCampaignsQueued==1&&stats.ObservationsAccepted==1);
    assert(stats.ReferenceRequestsReceived==1&&stats.ReferenceResponsesCompleted==1&&stats.TimestampContinuityResets==1);
    coordinator.Shutdown();assert(target.ActivityCalls==2);
    return 0;
}
