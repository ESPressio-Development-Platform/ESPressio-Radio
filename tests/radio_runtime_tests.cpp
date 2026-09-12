#include <ESPressio_RadioRuntime.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace ESPressio::Radio;

struct FakeReassembly final {
    std::size_t ExpireCalls{0};
    RadioReassemblyStatus Accept(IRadio&,const RadioPacketView&,const RadioTransportV3FragmentView&,std::uint64_t,bool) noexcept {
        return RadioReassemblyStatus::Accepted;
    }
    RadioReassemblyStatus TakeCompleteTrusted(IRadio&,const RadioAddress&,RadioTransferId,RadioCompletedReassembly&) noexcept {
        return RadioReassemblyStatus::Complete;
    }
    RadioReassemblyStatus PromoteCompleted(IRadio&,const RadioAddress&,RadioTransferId,RadioServiceClass) noexcept {
        return RadioReassemblyStatus::Complete;
    }
    std::size_t Expire(std::uint64_t) noexcept { ++ExpireCalls; return 0; }
};

struct FakeScheduler final {
    bool Bound{false};
    unsigned SubmitCalls{0};
    RadioSchedulerStatus BindProvider(IRadio&) noexcept { Bound=true; return RadioSchedulerStatus::Success; }
    RadioTransferSubmissionResult Submit(IRadio&,const RadioAddress&,const RadioServiceProfile&,const RadioTransferTiming&,const std::uint8_t*,std::size_t) noexcept {
        ++SubmitCalls; return {RadioSchedulerStatus::Success,42};
    }
};

struct FakeDomainRuntime final {
    IRadioTransferResultSink* Sink{nullptr};
    unsigned InitializeCalls{0};
    unsigned StartCalls{0};
    unsigned ShutdownCalls{0};
    RadioDomainRuntimeStatus Initialize(IRadioTransferResultSink* sink) { Sink=sink; ++InitializeCalls; return RadioDomainRuntimeStatus::Success; }
    RadioDomainRuntimeStatus Start() { ++StartCalls; return RadioDomainRuntimeStatus::Success; }
    RadioDomainRuntimeStatus Shutdown() noexcept { ++ShutdownCalls; return RadioDomainRuntimeStatus::Success; }
};

class FakeProvider final : public IRadio {
public:
    bool Started{false};
    IRadioReceiver* Receiver{nullptr};
    IRadioRuntimeSink* RuntimeSink{nullptr};
    RadioAddress Address{};
    FakeProvider(){const std::uint8_t value=7;Address=RadioAddress::FromBytes(&value,1);}
    bool Start() override {Started=true;return true;}
    void Stop() noexcept override {Started=false;}
    bool IsStarted() const noexcept override {return Started;}
    RadioCapabilities Capabilities() const noexcept override {return {RadioCapability::HardwareAddressing,64,1,1024};}
    RadioAddress LocalAddress() const noexcept override {return Address;}
    RadioContentionDomainId ContentionDomain() const noexcept override {return {7};}
    RadioProviderResourceProfile ProviderResources() const noexcept override {return {4,2,1,0};}
    bool IsTransmitReady() const noexcept override {return true;}
    RadioTransmissionCost EstimateTransmissionCost(const RadioAddress&,std::size_t,const RadioServiceProfile&) const noexcept override {return {1,1,RadioCostEstimateQuality::ConservativeAirtime};}
    RadioSendResult Send(const RadioAddress&,const std::uint8_t*,std::size_t) noexcept override {return RadioSendResult::Accepted(RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement());}
    void SetReceiver(IRadioReceiver* receiver) noexcept override {Receiver=receiver;}
    void SetRuntimeSink(IRadioRuntimeSink* sink) noexcept override {RuntimeSink=sink;}
    ManagedRadioIngressServiceResult ServiceInbound(std::size_t) noexcept override {return {};}
};

struct ReadySink final : IRadioLogicalTransferReadySink {
    unsigned Count{0};
    RadioInboundTransferHandle Last{};
    void RadioLogicalTransferReady(const RadioInboundTransferHandle& handle) noexcept override {++Count;Last=handle;}
};

struct ResultSink final : IRadioRuntimeTransferResultSink {
    unsigned Count{0};
    RadioRuntimeTransferResult Last{};
    void RadioLogicalTransferResolved(const RadioRuntimeTransferResult& result) noexcept override {++Count;Last=result;}
};

int main(){
    FakeReassembly reassembly;
    FakeScheduler scheduler;
    FakeDomainRuntime domainRuntime;
    FakeProvider provider;
    ReadySink ready;
    ResultSink results;
    RadioRuntime<FakeReassembly,1,1,4> runtime(reassembly);

    assert(runtime.RegisterDomain({7},scheduler,domainRuntime)==RadioRuntimeStatus::Success);
    assert(runtime.RegisterProvider(provider)==RadioRuntimeStatus::Success);
    assert(scheduler.Bound);
    assert(runtime.Initialize(&ready,&results)==RadioRuntimeStatus::Success);
    assert(domainRuntime.InitializeCalls==1);
    assert(provider.Receiver!=nullptr);
    assert(runtime.Start()==RadioRuntimeStatus::Success);
    assert(runtime.IsRunning()&&provider.Started&&domainRuntime.StartCalls==1);

    const std::uint8_t peerByte=9;
    const auto peerAddress=RadioAddress::FromBytes(&peerByte,1);
    RadioPeerHandle peer{};
    assert(runtime.ObservePeer(provider,peerAddress,peer)==RadioPeerObserveResult::Observed);
    assert(peer);

    const RadioServiceProfile profile{RadioServiceClass::Responsive,RadioDeadlineTreatment::ExpiryOnly,RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    const RadioTransferTiming timing{1'000'000'000ULL,0};
    const std::uint8_t payload[3]{1,2,3};
    const auto submitted=runtime.SubmitPeer(peer,profile,timing,payload,sizeof(payload));
    assert(submitted&&submitted.TransferId==42&&scheduler.SubmitCalls==1);

    runtime.RadioReassemblyReady(provider,peerAddress,11,RadioServiceClass::Responsive);
    assert(ready.Count==1&&ready.Last&&ready.Last.TransferId==11&&ready.Last.DirectPeer);

    assert(runtime.Shutdown()==RadioRuntimeStatus::Success);
    assert(!runtime.IsRunning()&&!provider.Started&&provider.Receiver==nullptr);
    assert(domainRuntime.ShutdownCalls==1&&reassembly.ExpireCalls==1);
    assert(runtime.Start()==RadioRuntimeStatus::TerminallyShutdown);
    assert(runtime.RegisterProvider(provider)==RadioRuntimeStatus::TerminallyShutdown);
    return 0;
}
