#include <ESPressio_RadioCapacity.hpp>
#include <ESPressio_RadioReassembly.hpp>
#include <ESPressio_RadioScheduler.hpp>
#include <ESPressio_RadioWireV3.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace ESPressio::Radio;

using Arena=RadioStaticByteArena<RadioByteClass<64,8>,RadioByteClass<256,8>>;
using Domain=RadioStaticCapacityDomain<256,8,Arena>;
using Inbound=RadioCapacityPlane<RadioCapacityDirection::Inbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=RadioCapacityPlane<RadioCapacityDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Table=RadioReassemblyTable<Inbound,8,8>;
using Profile=RadioSchedulerProfile<32,32,32,32,32,32,128,25'000>;
using Scheduler=RadioDomainScheduler<Outbound,Profile,8,16,1,64>;

class Provider final:public IRadio{
public:
    IRadioRuntimeSink* Sink=nullptr;
    unsigned Sends=0;
    std::array<RadioServiceClass,16> SentClasses{};
    RadioAddress Address{};
    Provider(){const std::uint8_t bytes[2]{9,9};Address=RadioAddress::FromBytes(bytes,2);}
    bool Start()override{return true;} void Stop()noexcept override{} bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing,32,2,256};}
    RadioAddress LocalAddress()const noexcept override{return Address;}
    RadioContentionDomainId ContentionDomain()const noexcept override{return {1};}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,0,0};}
    bool IsTransmitReady()const noexcept override{return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t bytes,const RadioServiceProfile&)const noexcept override{
        return {bytes,100'000,RadioCostEstimateQuality::ConservativeAirtime};
    }
    RadioSendResult Send(const RadioAddress&,const std::uint8_t* bytes,std::size_t count)noexcept override{
        RadioTransportV3FragmentView decoded{};assert(DecodeRadioTransportV3Fragment(bytes,count,decoded));
        SentClasses[Sends++]=decoded.Header.ServiceClass;
        return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());
    }
    void SetReceiver(IRadioReceiver*)noexcept override{}
    void SetRuntimeSink(IRadioRuntimeSink* sink)noexcept override{Sink=sink;}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
};

struct ResultSink final:IRadioTransferResultSink{
    unsigned Count=0;
    void RadioTransferResolved(const RadioTransferTerminalResult&)noexcept override{++Count;}
};

static RadioTransportV3FragmentView DecodeOne(
    const RadioTransportV3Header& header,const std::uint8_t* payload,std::size_t payloadBytes,
    std::array<std::uint8_t,64>& wire,std::size_t& encoded){
    assert(EncodeRadioTransportV3Fragment(header,payload,payloadBytes,wire.data(),wire.size(),encoded));
    RadioTransportV3FragmentView view{};assert(DecodeRadioTransportV3Fragment(wire.data(),encoded,view));return view;
}

int main(){
    const std::uint8_t sourceBytes[2]{1,2};
    const auto source=RadioAddress::FromBytes(sourceBytes,2);
    const RadioTransportV3Header valid{0x1234,0,1,3,source,RadioServiceClass::Responsive,25};
    const std::uint8_t payload[3]{7,8,9};
    std::array<std::uint8_t,64> encoded{};
    std::size_t encodedBytes=0;
    assert(EncodeRadioTransportV3Fragment(valid,payload,sizeof(payload),encoded.data(),encoded.size(),encodedBytes));

    // Every truncation before the complete encoded frame either rejects or yields a structurally valid partial fragment;
    // header truncation must always reject. This loop is intentionally adversarial and allocation-free.
    for(std::size_t length=0;length<encodedBytes;++length){
        RadioTransportV3FragmentView view{};
        const bool accepted=DecodeRadioTransportV3Fragment(encoded.data(),length,view);
        if(length<RadioTransportV3HeaderBytes(source)) assert(!accepted);
        if(accepted){assert(RadioTransportV3HeaderIsValid(view.Header));assert(view.FragmentPayloadBytes<=view.Header.LogicalPayloadBytes);}
    }

    auto corrupted=encoded;
    const auto rejectMutation=[&](std::size_t index,std::uint8_t value){
        auto candidate=encoded;candidate[index]=value;RadioTransportV3FragmentView view{};
        assert(!DecodeRadioTransportV3Fragment(candidate.data(),encodedBytes,view));
    };
    rejectMutation(0,0); rejectMutation(1,0); rejectMutation(2,2);
    rejectMutation(3,0); rejectMutation(4,0); // transfer id zero
    rejectMutation(6,0); // fragment count zero
    rejectMutation(7,0); rejectMutation(8,0); // logical bytes zero
    rejectMutation(9,0); rejectMutation(9,9); // source length
    rejectMutation(10,0); rejectMutation(10,7); // service spoof outside frozen taxonomy
    for(std::size_t i=11;i<15;++i) corrupted[i]=0;
    RadioTransportV3FragmentView zeroResidence{};
    assert(!DecodeRadioTransportV3Fragment(corrupted.data(),encodedBytes,zeroResidence));

    // Quarantine preserves the wire service but promotion must use the separately validated service exactly.
    Inbound inbound;inbound.Initialize();Table table(inbound);Provider provider;
    const std::uint8_t destinationBytes[2]{3,4};const auto destination=RadioAddress::FromBytes(destinationBytes,2);
    RadioPacketView packet{};packet.Source=source;packet.Destination=destination;
    std::array<std::uint8_t,64> wire{};std::size_t wireBytes=0;
    const RadioTransportV3Header critical{77,0,1,3,source,RadioServiceClass::Critical,100};
    auto criticalView=DecodeOne(critical,payload,sizeof(payload),wire,wireBytes);
    assert(table.Accept(provider,packet,criticalView,1'000'000,false)==RadioReassemblyStatus::Complete);
    assert(table.PromoteCompleted(provider,source,77,RadioServiceClass::Responsive)==RadioReassemblyStatus::Malformed);
    RadioCompletedReassembly blocked;
    assert(table.TakeCompleteTrusted(provider,source,77,blocked)==RadioReassemblyStatus::NotTrusted);
    assert(table.PromoteCompleted(provider,source,77,RadioServiceClass::Critical)==RadioReassemblyStatus::Complete);
    assert(table.TakeCompleteTrusted(provider,source,77,blocked)==RadioReassemblyStatus::Complete);
    blocked.Reset();

    // Saturated application traffic cannot make an eligible promotable Clock head wait behind BestEffort.
    Outbound outbound;outbound.Initialize();ResultSink results;Scheduler scheduler(outbound,{1});
    assert(scheduler.BindProvider(provider)==RadioSchedulerStatus::Success);
    assert(scheduler.Initialize(&results)==RadioSchedulerStatus::Success);
    const std::uint8_t small[2]{1,2};
    const RadioServiceProfile best{RadioServiceClass::BestEffort,RadioDeadlineTreatment::ExpiryOnly,
        RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    const RadioServiceProfile clock{RadioServiceClass::Clock,RadioDeadlineTreatment::Promotable,
        RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    for(unsigned i=0;i<4;++i) assert(scheduler.Submit(provider,destination,best,{10'000'000,0},small,sizeof(small)));
    assert(scheduler.Submit(provider,destination,clock,{10'000'000,120'000},small,sizeof(small)));
    assert(scheduler.Service(0).Status==RadioSchedulerStatus::Success);
    assert(provider.Sends==1&&provider.SentClasses[0]==RadioServiceClass::Clock);
    assert(scheduler.Shutdown()==RadioSchedulerStatus::Success);
    return 0;
}
