#include <ESPressio_RadioScheduler.hpp>

#include <array>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Radio;

using Arena=RadioStaticByteArena<RadioByteClass<64,8>,RadioByteClass<256,8>>;
using Domain=RadioStaticCapacityDomain<256,8,Arena>;
using Outbound=RadioCapacityPlane<RadioCapacityDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using FastProfile=RadioSchedulerProfile<32,32,32,32,32,32,64,1'000>;
using DebtProfile=RadioSchedulerProfile<10,10,10,10,10,10,30,10>;
using Scheduler=RadioDomainScheduler<Outbound,FastProfile,8,16,2,32>;
using DebtScheduler=RadioDomainScheduler<Outbound,DebtProfile,8,16,1,32>;

struct ResultSink final:IRadioTransferResultSink{
    std::array<RadioTransferTerminalResult,16> Results{};std::size_t Count=0;
    void RadioTransferResolved(const RadioTransferTerminalResult& result)noexcept override{assert(Count<Results.size());Results[Count++]=result;}
};
struct WakeCounter final{unsigned Count=0;static void Wake(void* c)noexcept{++static_cast<WakeCounter*>(c)->Count;}};

class Provider final:public IRadio{
public:
    enum class Mode{Immediate,Deferred,BusyOnce};
    Mode CurrentMode=Mode::Immediate;bool Ready=true;std::uint32_t NextHandle=1;IRadioRuntimeSink* Sink=nullptr;
    unsigned SendCalls=0;std::array<RadioServiceClass,32> Classes{};std::array<std::uint8_t,32> Fragments{};
    RadioContentionDomainId Domain{1};RadioAddress Address{};RadioDirectLinkEvidence ImmediateEvidence=RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement();
    Provider(){const std::uint8_t b[2]{7,8};Address=RadioAddress::FromBytes(b,2);}
    bool Start()override{return true;}void Stop()noexcept override{}bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing,24,2,100};}
    RadioAddress LocalAddress()const noexcept override{return Address;}RadioContentionDomainId ContentionDomain()const noexcept override{return Domain;}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,1,0};}
    bool IsTransmitReady()const noexcept override{return Ready;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t,const RadioServiceProfile&)const noexcept override{
        return {20,100,RadioCostEstimateQuality::ConservativeAirtime};
    }
    RadioSendResult Send(const RadioAddress&,const std::uint8_t* payload,std::size_t bytes)noexcept override{
        ++SendCalls;RadioTransportV3FragmentView decoded{};assert(DecodeRadioTransportV3Fragment(payload,bytes,decoded));
        Classes[SendCalls-1]=decoded.Header.ServiceClass;Fragments[SendCalls-1]=decoded.Header.FragmentIndex;
        if(CurrentMode==Mode::BusyOnce){CurrentMode=Mode::Immediate;Ready=false;return {RadioSendStatus::Busy,0};}
        if(CurrentMode==Mode::Deferred){const auto handle=RadioTransmissionHandle{NextHandle++};return RadioSendResult::Accepted({},handle);}
        return RadioSendResult::Accepted(ImmediateEvidence);
    }
    void SetReceiver(IRadioReceiver*)noexcept override{}void SetRuntimeSink(IRadioRuntimeSink* sink)noexcept override{Sink=sink;}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
    void Resolve(std::uint32_t handle,RadioDirectLinkEvidence evidence){assert(Sink);Sink->TransmissionResolved(*this,{handle},evidence);}
    void BecomeReady(){Ready=true;assert(Sink);Sink->TransmitReadinessChanged(*this);}
};

static RadioServiceProfile Expiry(RadioServiceClass c,RadioDirectLinkEvidenceRequirement evidence=RadioDirectLinkEvidenceRequirement::TransmissionCompletion){return {c,RadioDeadlineTreatment::ExpiryOnly,evidence};}
static RadioServiceProfile Promote(RadioServiceClass c){return {c,RadioDeadlineTreatment::Promotable,RadioDirectLinkEvidenceRequirement::TransmissionCompletion};}
static RadioTransferTiming ExpiryTiming(){return {1'000'000'000ULL,0};}

int main(){
    const std::uint8_t bytes20[20]{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19};
    const std::uint8_t bytes5[5]{1,2,3,4,5};
    {
        Outbound capacity;capacity.Initialize();Provider provider;ResultSink results;WakeCounter wakes;Scheduler scheduler(capacity,{1});
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);
        assert(scheduler.Initialize(&results,{&wakes,&WakeCounter::Wake})==RadioSchedulerStatus::Success);
        auto best=scheduler.Submit(provider,provider.LocalAddress(),Expiry(RadioServiceClass::BestEffort),ExpiryTiming(),bytes20,20);
        auto critical=scheduler.Submit(provider,provider.LocalAddress(),Expiry(RadioServiceClass::Critical),ExpiryTiming(),bytes5,5);
        assert(best&&critical&&wakes.Count>=2);
        // Critical receives a class visit before the queued BestEffort transfer; BestEffort then advances one fragment per service quantum.
        assert(scheduler.Service(1).Status==RadioSchedulerStatus::Success);assert(provider.SendCalls==1);assert(provider.Classes[0]==RadioServiceClass::Critical);
        assert(scheduler.Service(2).Status==RadioSchedulerStatus::Success);assert(provider.SendCalls==2);assert(provider.Classes[1]==RadioServiceClass::BestEffort&&provider.Fragments[1]==0);
        assert(scheduler.Service(3).Status==RadioSchedulerStatus::Success);assert(provider.SendCalls==3&&provider.Fragments[2]==1);
        assert(scheduler.Service(4).Status==RadioSchedulerStatus::Success);assert(provider.SendCalls==4&&provider.Fragments[3]==2);
        assert(results.Count==2&&results.Results[0].Status==RadioTransferTerminalStatus::Completed&&results.Results[1].Status==RadioTransferTerminalStatus::Completed);
        assert(scheduler.Shutdown()==RadioSchedulerStatus::Success);
    }
    {
        // A deferred provider holds the whole contention domain until exact terminal completion; stale completion cannot release it.
        Outbound capacity;capacity.Initialize();Provider first;Provider second;first.CurrentMode=Provider::Mode::Deferred;second.CurrentMode=Provider::Mode::Immediate;
        ResultSink results;Scheduler scheduler(capacity,{1});assert(scheduler.BindProvider(first)==RadioSchedulerStatus::Success);assert(scheduler.BindProvider(second)==RadioSchedulerStatus::Success);assert(scheduler.Initialize(&results)==RadioSchedulerStatus::Success);
        assert(scheduler.Submit(first,first.LocalAddress(),Expiry(RadioServiceClass::Responsive),ExpiryTiming(),bytes5,5));
        assert(scheduler.Submit(second,second.LocalAddress(),Expiry(RadioServiceClass::BestEffort),ExpiryTiming(),bytes5,5));
        scheduler.Service(1);assert(first.SendCalls==1&&second.SendCalls==0&&scheduler.HasOutstandingFragment());
        first.Resolve(999,RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());assert(scheduler.StaleProviderCompletions()==1);
        scheduler.Service(2);assert(second.SendCalls==0&&scheduler.HasOutstandingFragment());
        first.Resolve(1,RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());scheduler.Service(3);
        assert(!scheduler.HasOutstandingFragment()&&second.SendCalls==1&&results.Count==2);
    }
    {
        // Busy retains the exact head and resumes only after provider readiness wake.
        Outbound capacity;capacity.Initialize();Provider provider;provider.CurrentMode=Provider::Mode::BusyOnce;ResultSink results;WakeCounter wakes;Scheduler scheduler(capacity,{1});
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);assert(scheduler.Initialize(&results,{&wakes,&WakeCounter::Wake})==RadioSchedulerStatus::Success);
        assert(scheduler.Submit(provider,provider.LocalAddress(),Expiry(RadioServiceClass::Responsive),ExpiryTiming(),bytes5,5));
        const auto before=wakes.Count;scheduler.Service(1);assert(provider.SendCalls==1&&results.Count==0);
        scheduler.Service(2);assert(provider.SendCalls==1);provider.BecomeReady();assert(wakes.Count==before+1);
        scheduler.Service(3);assert(provider.SendCalls==2&&results.Count==1&&results.Results[0].Status==RadioTransferTerminalStatus::Completed);
    }
    {
        // Required peer acknowledgement is never synthesized from completion alone.
        Outbound capacity;capacity.Initialize();Provider provider;ResultSink results;Scheduler scheduler(capacity,{1});
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);assert(scheduler.Initialize(&results)==RadioSchedulerStatus::Success);
        assert(scheduler.Submit(provider,provider.LocalAddress(),Expiry(RadioServiceClass::Critical,RadioDirectLinkEvidenceRequirement::PeerAcknowledgement),ExpiryTiming(),bytes5,5));
        scheduler.Service(1);assert(results.Count==1&&results.Results[0].Status==RadioTransferTerminalStatus::EvidenceInsufficient);
    }
    {
        // Urgent Clock work may borrow bounded debt; the next ordinary Clock transfer must repay through future quanta.
        Outbound capacity;capacity.Initialize();Provider provider;ResultSink results;DebtScheduler scheduler(capacity,{1});
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);assert(scheduler.Initialize(&results)==RadioSchedulerStatus::Success);
        RadioTransferTiming urgent{1'000'000,50};assert(scheduler.Submit(provider,provider.LocalAddress(),Promote(RadioServiceClass::Clock),urgent,bytes5,5));
        scheduler.Service(1);assert(provider.SendCalls==1&&results.Count==1);
        assert(scheduler.Submit(provider,provider.LocalAddress(),Expiry(RadioServiceClass::Clock),ExpiryTiming(),bytes5,5));
        scheduler.Service(2);assert(provider.SendCalls==1);
        scheduler.Service(3);assert(provider.SendCalls==1);
        scheduler.Service(4);assert(provider.SendCalls==1);
        scheduler.Service(5);assert(provider.SendCalls==2&&results.Count==2);
    }
    {
        // Independent contention domains make progress independently.
        Outbound capA;Outbound capB;capA.Initialize();capB.Initialize();Provider a;Provider b;a.Domain={1};b.Domain={2};ResultSink ra,rb;Scheduler sa(capA,{1}),sb(capB,{2});
        assert(sa.BindProvider(a)==RadioSchedulerStatus::Success&&sb.BindProvider(b)==RadioSchedulerStatus::Success);assert(sa.Initialize(&ra)==RadioSchedulerStatus::Success&&sb.Initialize(&rb)==RadioSchedulerStatus::Success);
        assert(sa.Submit(a,a.LocalAddress(),Expiry(RadioServiceClass::BestEffort),ExpiryTiming(),bytes5,5));assert(sb.Submit(b,b.LocalAddress(),Expiry(RadioServiceClass::BestEffort),ExpiryTiming(),bytes5,5));
        sa.Service(1);sb.Service(1);assert(a.SendCalls==1&&b.SendCalls==1);
    }
    return 0;
}
