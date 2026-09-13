#include <ESPressio_RadioScheduler.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace ESPressio::Radio;

using Arena=RadioStaticByteArena<RadioByteClass<64,8>,RadioByteClass<256,8>>;
using Domain=RadioStaticCapacityDomain<256,8,Arena>;
using Outbound=RadioCapacityPlane<RadioCapacityDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Profile=RadioSchedulerProfile<32,32,32,32,32,32,64,1'000>;
using Scheduler=RadioDomainScheduler<Outbound,Profile,8,16,1,64>;
using TinyScheduler=RadioDomainScheduler<Outbound,Profile,1,16,1,64>;

class Provider final:public IRadio{
    IRadioRuntimeSink* _sink{nullptr};
    RadioAddress _address{};
public:
    RadioContentionDomainId DomainId{7};
    Provider(){const std::uint8_t bytes[2]{0x11,0x22};_address=RadioAddress::FromBytes(bytes,2);}
    bool Start()override{return true;}
    void Stop()noexcept override{}
    bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing,32,1,128};}
    RadioAddress LocalAddress()const noexcept override{return _address;}
    RadioContentionDomainId ContentionDomain()const noexcept override{return DomainId;}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,1,0};}
    bool IsTransmitReady()const noexcept override{return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t,const RadioServiceProfile&)const noexcept override{
        return {8,100,RadioCostEstimateQuality::ConservativeAirtime};
    }
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t)noexcept override{
        return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    }
    void SetReceiver(IRadioReceiver*)noexcept override{}
    void SetRuntimeSink(IRadioRuntimeSink* sink)noexcept override{_sink=sink;}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
};

struct LeaseProbe final{
    RadioTransferId ExplicitReserved{0};
    std::array<RadioTransferId,8> Issued{};
    std::array<std::uint64_t,8> Correlations{};
    std::size_t IssuedCount{0};
    std::size_t ReleaseCount{0};
    RadioContentionDomainId LastDomain{};

    bool IsReservedValue(RadioTransferId id)const noexcept{
        if(id==ExplicitReserved)return true;
        for(std::size_t i=0;i<IssuedCount;++i)if(Issued[i]==id)return true;
        return false;
    }
    static bool IsReserved(void* context,RadioContentionDomainId domain,RadioTransferId id)noexcept{
        auto& self=*static_cast<LeaseProbe*>(context);self.LastDomain=domain;return self.IsReservedValue(id);
    }
    static bool ReserveIssued(void* context,RadioContentionDomainId domain,std::uint64_t correlation,RadioTransferId id)noexcept{
        auto& self=*static_cast<LeaseProbe*>(context);self.LastDomain=domain;
        if(self.IssuedCount>=self.Issued.size()||correlation==0||self.IsReservedValue(id))return false;
        self.Issued[self.IssuedCount]=id;self.Correlations[self.IssuedCount]=correlation;++self.IssuedCount;return true;
    }
    static void ReleaseIssued(void* context,RadioContentionDomainId domain,std::uint64_t correlation,RadioTransferId id)noexcept{
        auto& self=*static_cast<LeaseProbe*>(context);self.LastDomain=domain;
        for(std::size_t i=0;i<self.IssuedCount;++i){
            if(self.Issued[i]!=id||self.Correlations[i]!=correlation)continue;
            for(std::size_t j=i+1;j<self.IssuedCount;++j){self.Issued[j-1]=self.Issued[j];self.Correlations[j-1]=self.Correlations[j];}
            --self.IssuedCount;++self.ReleaseCount;return;
        }
        assert(false&&"release must match one reserved issued transfer id");
    }
    RadioTransferIdLeaseTarget Target()noexcept{return {this,&LeaseProbe::IsReserved,&LeaseProbe::ReserveIssued,&LeaseProbe::ReleaseIssued};}
};

static RadioServiceProfile Service(){return {RadioServiceClass::Responsive,RadioDeadlineTreatment::ExpiryOnly,RadioDirectLinkEvidenceRequirement::TransmissionCompletion};}
static RadioTransferTiming Timing(){return {1'000'000'000ULL,0};}

int main(){
    const std::uint8_t payload[4]{1,2,3,4};
    {
        Outbound capacity;capacity.Initialize();
        Provider provider;LeaseProbe leases;leases.ExplicitReserved=1;
        Scheduler scheduler(capacity,provider.DomainId);
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);
        assert(scheduler.BindTransferIdLeaseTarget(leases.Target())==RadioSchedulerStatus::Success);
        assert(scheduler.Initialize()==RadioSchedulerStatus::Success);
        assert(scheduler.BindTransferIdLeaseTarget(leases.Target())==RadioSchedulerStatus::Frozen);

        const auto zero=scheduler.Submit(provider,provider.LocalAddress(),Service(),Timing(),payload,sizeof(payload));
        assert(zero&&zero.TransferId==2);
        assert(leases.IssuedCount==0);

        const std::uint64_t correlation=0x1122334455667788ULL;
        const auto correlated=scheduler.Submit(provider,provider.LocalAddress(),Service(),Timing(),payload,sizeof(payload),correlation);
        assert(correlated&&correlated.TransferId!=0&&correlated.TransferId!=1&&correlated.TransferId!=zero.TransferId);
        assert(leases.IssuedCount==1&&leases.Issued[0]==correlated.TransferId&&leases.Correlations[0]==correlation);
        assert(leases.LastDomain==provider.DomainId);
        assert(scheduler.Shutdown()==RadioSchedulerStatus::Success);
    }
    {
        Outbound capacity;capacity.Initialize();
        Provider provider;LeaseProbe leases;
        TinyScheduler scheduler(capacity,provider.DomainId);
        assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);
        assert(scheduler.BindTransferIdLeaseTarget(leases.Target())==RadioSchedulerStatus::Success);
        assert(scheduler.Initialize()==RadioSchedulerStatus::Success);

        const auto first=scheduler.Submit(provider,provider.LocalAddress(),Service(),Timing(),payload,sizeof(payload),0xAAULL);
        assert(first&&leases.IssuedCount==1);
        const auto second=scheduler.Submit(provider,provider.LocalAddress(),Service(),Timing(),payload,sizeof(payload),0xBBULL);
        assert(second.Status==RadioSchedulerStatus::ResourceUnavailable&&second.TransferId==0);
        assert(leases.IssuedCount==1);
        assert(leases.ReleaseCount==1);
        assert(scheduler.Shutdown()==RadioSchedulerStatus::Success);
    }
    return 0;
}
