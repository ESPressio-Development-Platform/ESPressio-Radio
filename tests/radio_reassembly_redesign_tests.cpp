#include <ESPressio_RadioReassembly.hpp>

#include <array>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Radio;

using Arena=RadioStaticByteArena<RadioByteClass<64,4>,RadioByteClass<256,4>,RadioByteClass<1024,2>>;
using Domain=RadioStaticCapacityDomain<384,4,Arena>;
using Inbound=RadioCapacityPlane<RadioCapacityDirection::Inbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Table=RadioReassemblyTable<Inbound,8,8>;

class Provider final : public IRadio {
    IRadioReceiver* _receiver=nullptr;
    IRadioRuntimeSink* _sink=nullptr;
public:
    bool Start() override{return true;} void Stop() noexcept override{} bool IsStarted()const noexcept override{return true;}
    RadioCapabilities Capabilities()const noexcept override{return {RadioCapability::HardwareAddressing,32,2,100};}
    RadioAddress LocalAddress()const noexcept override{const std::uint8_t b[2]{9,9};return RadioAddress::FromBytes(b,2);}
    RadioContentionDomainId ContentionDomain()const noexcept override{return {1};}
    RadioProviderResourceProfile ProviderResources()const noexcept override{return {4,2,0,0};}
    bool IsTransmitReady()const noexcept override{return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t n,const RadioServiceProfile&)const noexcept override{return {n+1,(n+1)*1000,RadioCostEstimateQuality::ConservativeAirtime};}
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t)noexcept override{return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());}
    void SetReceiver(IRadioReceiver* r)noexcept override{_receiver=r;}
    void SetRuntimeSink(IRadioRuntimeSink* s)noexcept override{_sink=s;}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t)noexcept override{return {};}
};

static RadioTransportV3FragmentView MakeFragment(
    RadioTransferId id,std::uint8_t index,std::uint8_t count,std::uint16_t logical,
    const RadioAddress& source,RadioServiceClass service,std::uint32_t residence,
    const std::uint8_t* payload,std::size_t payloadBytes,std::array<std::uint8_t,64>& wire){
    RadioTransportV3Header header{id,index,count,logical,source,service,residence};
    std::size_t encoded=0;
    assert(EncodeRadioTransportV3Fragment(header,payload,payloadBytes,wire.data(),wire.size(),encoded));
    RadioTransportV3FragmentView view{};
    assert(DecodeRadioTransportV3Fragment(wire.data(),encoded,view));
    return view;
}

int main(){
    Inbound capacity; capacity.Initialize(); Table table(capacity); Provider provider;
    const std::uint8_t sourceBytes[2]{1,2}; const auto source=RadioAddress::FromBytes(sourceBytes,2);
    const std::uint8_t destinationBytes[2]{3,4}; const auto destination=RadioAddress::FromBytes(destinationBytes,2);
    std::array<std::uint8_t,20> logical{}; for(std::size_t i=0;i<logical.size();++i)logical[i]=static_cast<std::uint8_t>(i+1);
    // MTU 32, source length 2 => 15 fragment bytes, therefore 20 logical bytes => two fragments.
    std::array<std::uint8_t,64> wire0{},wire1{};
    auto second=MakeFragment(1,1,2,20,source,RadioServiceClass::Responsive,100,logical.data()+15,5,wire1);
    RadioPacketView packet{}; packet.Source=source;packet.Destination=destination;packet.Flags=RadioPacketFlag::None;
    assert(table.Accept(provider,packet,second,1'000'000,true)==RadioReassemblyStatus::Accepted);
    assert(table.Accept(provider,packet,second,2'000'000,true)==RadioReassemblyStatus::Duplicate);
    auto first=MakeFragment(1,0,2,20,source,RadioServiceClass::Responsive,50,logical.data(),15,wire0);
    assert(table.Accept(provider,packet,first,2'000'000,true)==RadioReassemblyStatus::Complete);
    RadioCompletedReassembly completed;
    assert(table.TakeCompleteTrusted(provider,source,1,completed)==RadioReassemblyStatus::Complete);
    assert(completed.Record().ExpiryNanoseconds==52'000'000ULL);
    const auto view=completed.Payload();assert(view.Size==20);
    for(std::size_t i=0;i<logical.size();++i)assert(view.Data[i]==logical[i]);
    completed.Reset();
    assert(table.Accept(provider,packet,first,3'000'000,true)==RadioReassemblyStatus::RecentlyCompleted);

    // Conflicting duplicate bytes are malformed and release the active transfer.
    auto transfer2=MakeFragment(2,0,2,20,source,RadioServiceClass::BestEffort,100,logical.data(),15,wire0);
    assert(table.Accept(provider,packet,transfer2,5'000'000,true)==RadioReassemblyStatus::Accepted);
    auto changed=logical;changed[0]^=0xFF;
    auto transfer2Conflict=MakeFragment(2,0,2,20,source,RadioServiceClass::BestEffort,90,changed.data(),15,wire0);
    assert(table.Accept(provider,packet,transfer2Conflict,6'000'000,true)==RadioReassemblyStatus::Malformed);
    assert(table.Accept(provider,packet,transfer2,7'000'000,true)==RadioReassemblyStatus::Accepted);

    // Untrusted traffic occupies quarantine only and cannot be handed upward until promoted.
    auto untrusted0=MakeFragment(3,0,2,20,source,RadioServiceClass::Critical,100,logical.data(),15,wire0);
    auto untrusted1=MakeFragment(3,1,2,20,source,RadioServiceClass::Critical,90,logical.data()+15,5,wire1);
    assert(table.Accept(provider,packet,untrusted0,10'000'000,false)==RadioReassemblyStatus::Accepted);
    assert(table.Accept(provider,packet,untrusted1,11'000'000,false)==RadioReassemblyStatus::Complete);
    RadioCompletedReassembly blocked;
    assert(table.TakeCompleteTrusted(provider,source,3,blocked)==RadioReassemblyStatus::NotTrusted);
    assert(table.PromoteCompleted(provider,source,3,RadioServiceClass::Critical)==RadioReassemblyStatus::Complete);
    assert(table.TakeCompleteTrusted(provider,source,3,blocked)==RadioReassemblyStatus::Complete);
    assert(blocked.Payload().Size==20);
    blocked.Reset();

    // Expiry releases volatile ownership.
    auto expiring=MakeFragment(4,0,2,20,source,RadioServiceClass::Convergent,1,logical.data(),15,wire0);
    assert(table.Accept(provider,packet,expiring,20'000'000,true)==RadioReassemblyStatus::Accepted);
    assert(table.Expire(21'000'000)==1);

    return 0;
}
