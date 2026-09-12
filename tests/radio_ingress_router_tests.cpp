#include <ESPressio_RadioIngressRouter.hpp>

#include <array>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Radio;

using Arena=RadioStaticByteArena<RadioByteClass<64,4>,RadioByteClass<256,4>>;
using Domain=RadioStaticCapacityDomain<384,4,Arena>;
using Inbound=RadioCapacityPlane<RadioCapacityDirection::Inbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Table=RadioReassemblyTable<Inbound,8,8>;
using Router=RadioIngressRouter<Table>;

class Provider final : public IRadio {
    IRadioReceiver* _receiver=nullptr;
public:
    bool Start()override{return true;} void Stop()noexcept override{} bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing,32,2,100};}
    RadioAddress LocalAddress()const noexcept override{const std::uint8_t b[2]{9,9};return RadioAddress::FromBytes(b,2);}
    RadioContentionDomainId ContentionDomain()const noexcept override{return {1};}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,0,0};}
    bool IsTransmitReady()const noexcept override{return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t n,const RadioServiceProfile&)const noexcept override{return {n,1000,RadioCostEstimateQuality::ConservativeAirtime};}
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t)noexcept override{return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());}
    void SetReceiver(IRadioReceiver* receiver)noexcept override{_receiver=receiver;}
    void SetRuntimeSink(IRadioRuntimeSink*)noexcept override{}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
    void Deliver(const RadioPacketView& packet,const RadioReceiveTimestampEvidence& timestamp){assert(_receiver);_receiver->OnRadioPacket(*this,packet,timestamp);}
    bool HasReceiver()const noexcept{return _receiver!=nullptr;}
};

struct ClockSink final:IRadioClockFrameSink{
    unsigned Count=0;
    void OnRadioClockFrame(IRadio&,const RadioPacketView&,const RadioReceiveTimestampEvidence&)noexcept override{++Count;}
};
struct ReadySink final:IRadioReassemblyReadySink{
    unsigned Count=0;RadioTransferId Last=0;RadioServiceClass Service=RadioServiceClass::Invalid;
    void RadioReassemblyReady(IRadio&,const RadioAddress&,RadioTransferId id,RadioServiceClass service)noexcept override{++Count;Last=id;Service=service;}
};
struct TrustState{bool Trusted=true;};
static bool Trust(void* context,IRadio&,const RadioPacketView&,const RadioTransportV3FragmentView&)noexcept{
    return static_cast<TrustState*>(context)->Trusted;
}

static RadioPacketView Packet(const RadioAddress& source,const RadioAddress& destination,const std::uint8_t* bytes,std::size_t count){
    RadioPacketView packet{};packet.Source=source;packet.Destination=destination;packet.Payload=bytes;packet.PayloadSize=count;return packet;
}

int main(){
    Inbound capacity;capacity.Initialize();Table table(capacity);Router router(table);Provider provider;ClockSink clock;ReadySink ready;TrustState trust;
    router.Configure(&clock,&ready,{&trust,&Trust});router.Attach(provider);assert(provider.HasReceiver());
    const std::uint8_t sourceBytes[2]{1,2};const auto source=RadioAddress::FromBytes(sourceBytes,2);
    const std::uint8_t destinationBytes[2]{9,9};const auto destination=RadioAddress::FromBytes(destinationBytes,2);
    RadioReceiveTimestampEvidence timestamp{};timestamp.MonotonicNanoseconds=1'000'000;timestamp.ContinuityGeneration=1;

    // Compact Clock discriminator is consumed by Clock and never offered to v3 reassembly.
    std::array<std::uint8_t,RadioClockWireV1::MaximumRequestBytes> clockBytes{};std::size_t clockCount=0;
    assert(EncodeRadioClockRequestV1({7,source},clockBytes.data(),clockBytes.size(),clockCount));
    provider.Deliver(Packet(source,destination,clockBytes.data(),clockCount),timestamp);assert(clock.Count==1);

    // Trusted v3 single-fragment transfer enters protected Q1 and produces one readiness notification.
    const std::uint8_t logical[5]{1,2,3,4,5};std::array<std::uint8_t,32> wire{};std::size_t written=0;
    RadioTransportV3Header header{11,0,1,5,source,RadioServiceClass::Responsive,100};
    assert(EncodeRadioTransportV3Fragment(header,logical,sizeof(logical),wire.data(),wire.size(),written));
    provider.Deliver(Packet(source,destination,wire.data(),written),timestamp);
    assert(ready.Count==1&&ready.Last==11&&ready.Service==RadioServiceClass::Responsive);
    RadioCompletedReassembly completed;assert(table.TakeCompleteTrusted(provider,source,11,completed)==RadioReassemblyStatus::Complete);
    assert(completed.Payload().Size==5);completed.Reset();

    // Untrusted v3 remains quarantined and cannot generate a trusted-ready signal.
    trust.Trusted=false;header.TransferId=12;assert(EncodeRadioTransportV3Fragment(header,logical,sizeof(logical),wire.data(),wire.size(),written));
    provider.Deliver(Packet(source,destination,wire.data(),written),timestamp);assert(ready.Count==1);
    RadioCompletedReassembly blocked;assert(table.TakeCompleteTrusted(provider,source,12,blocked)==RadioReassemblyStatus::NotTrusted);

    // Non-Clock/non-v3 garbage is rejected without callback confusion.
    const std::uint8_t garbage[4]{0xde,0xad,0xbe,0xef};provider.Deliver(Packet(source,destination,garbage,sizeof(garbage)),timestamp);
    const auto stats=router.Statistics();assert(stats.ClockFrames==1&&stats.V3Fragments==2&&stats.MalformedFrames==1);
    router.Detach(provider);assert(!provider.HasReceiver());
    return 0;
}
