#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

#include <ESPressio_Synchronization.hpp>

#include "ESPressio_RadioDomainRuntime.hpp"
#include "ESPressio_RadioIngressRouter.hpp"
#include "ESPressio_RadioPeerRegistry.hpp"
#include "ESPressio_RadioScheduler.hpp"

namespace ESPressio::Radio {

enum class RadioRuntimeStatus : std::uint8_t {
    Success = 0,
    Frozen,
    NotInitialized,
    AlreadyRunning,
    InvalidConfiguration,
    ResourceUnavailable,
    DomainInitializationFailed,
    ProviderStartFailed,
    DomainStartFailed,
    TerminallyShutdown
};

/// <summary>Stable notification handle for one complete Radio-owned inbound logical transfer.</summary>
/// <remarks>
/// Provider and Source are direct-link facts only. DirectPeer is a process-local convenience handle and must never be
/// reinterpreted as authenticated original semantic provenance by RadioAdapters, Mesh or a Primitive family.
/// </remarks>
struct RadioInboundTransferHandle final {
    IRadio* Provider{nullptr};
    RadioPeerHandle DirectPeer{};
    RadioAddress Source{};
    RadioTransferId TransferId{0};
    RadioServiceClass Service{RadioServiceClass::Invalid};

    bool IsValid() const noexcept {
        return Provider!=nullptr && Source.IsValid() && TransferId!=0 && IsValidRadioServiceClass(Service);
    }
    explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Domain-qualified terminal result for one outbound Radio logical transfer.</summary>
struct RadioRuntimeTransferResult final {
    RadioContentionDomainId Domain{};
    RadioTransferTerminalResult Result{};
};

/// <summary>Fixed infrastructure sink notified when complete Radio-owned logical bytes are ready to be taken.</summary>
class IRadioLogicalTransferReadySink {
public:
    virtual ~IRadioLogicalTransferReadySink() = default;
    virtual void RadioLogicalTransferReady(const RadioInboundTransferHandle& handle) noexcept = 0;
};

/// <summary>Fixed infrastructure sink for domain-qualified outbound Radio terminal results.</summary>
class IRadioRuntimeTransferResultSink {
public:
    virtual ~IRadioRuntimeTransferResultSink() = default;
    virtual void RadioLogicalTransferResolved(const RadioRuntimeTransferResult& result) noexcept = 0;
};

/// <summary>
/// Family-opaque fixed Radio composition/runtime facade over configured contention-domain schedulers and runtimes.
/// </summary>
/// <remarks>
/// This is the replacement ownership boundary for the predecessor monolithic RadioTransport. It owns no Primitive-family
/// codec and never borrows caller payload memory after SubmitDirect/SubmitPeer returns. Each configured scheduler performs
/// the locked transactional Radio-local record+byte+queue acquisition before reporting Accepted. Inbound reassembly stays
/// Radio-owned until a fixed infrastructure consumer explicitly TakeInbound()s the complete lease; readiness notification
/// itself transfers no bytes and therefore cannot lose ownership under downstream backpressure.
///
/// Domain schedulers/runtimes are composition-owned concrete objects registered before Initialize(). RadioRuntime freezes
/// that topology, installs itself as the one provider ingress receiver, starts/stops providers and domain tasks in bounded
/// order, maintains generation-safe direct-peer handles and qualifies terminal results by contention domain so identical
/// scheduler-local transfer IDs in different domains never collide. An optional per-domain result-sink override allows the
/// bounded Clock coordinator to consume its own transfer IDs while forwarding all ordinary results to DomainResultSink().
///
/// Shutdown is terminal for this concrete runtime instance. It rejects new work, detaches ingress before joining domain
/// tasks, drains scheduler-owned outbound work through DomainRuntime::Shutdown(), expires all retained reassembly records,
/// stops providers and invalidates peers. A later restart creates/reconfigures a fresh runtime instance; stale process-local
/// handles from this instance therefore cannot become current work.
/// </remarks>
template<class TReassemblyTable,
         std::size_t TMaximumDomains,
         std::size_t TMaximumProviders,
         std::size_t TMaximumPeers>
class RadioRuntime final : public IRadioReassemblyReadySink {
    static_assert(TMaximumDomains>0 && TMaximumProviders>0 && TMaximumPeers>0);

    class DomainRelay final : public IRadioTransferResultSink {
        RadioRuntime* _owner{nullptr};
        std::size_t _index{0};
    public:
        void Configure(RadioRuntime* owner,std::size_t index) noexcept {_owner=owner;_index=index;}
        void RadioTransferResolved(const RadioTransferTerminalResult& result) noexcept override {
            if(_owner) _owner->OnDomainTransferResolved(_index,result);
        }
    };

    struct DomainBinding final {
        bool Occupied{false};
        RadioContentionDomainId Domain{};
        void* Scheduler{nullptr};
        RadioSchedulerStatus (*BindProvider)(void*,IRadio&) noexcept{nullptr};
        RadioTransferSubmissionResult (*Submit)(
            void*,IRadio&,const RadioAddress&,const RadioServiceProfile&,const RadioTransferTiming&,
            const std::uint8_t*,std::size_t) noexcept{nullptr};
        void* Runtime{nullptr};
        RadioDomainRuntimeStatus (*InitializeRuntime)(void*,IRadioTransferResultSink*){nullptr};
        RadioDomainRuntimeStatus (*StartRuntime)(void*){nullptr};
        RadioDomainRuntimeStatus (*ShutdownRuntime)(void*) noexcept{nullptr};
        IRadioTransferResultSink* ResultSinkOverride{nullptr};
        DomainRelay Relay{};
    };

    struct ProviderBinding final {
        IRadio* Provider{nullptr};
        std::size_t DomainIndex{TMaximumDomains};
        bool Started{false};
    };

    TReassemblyTable* _reassembly{nullptr};
    RadioIngressRouter<TReassemblyTable> _ingress;
    RadioPeerRegistry<TMaximumPeers> _peers{};
    std::array<DomainBinding,TMaximumDomains> _domains{};
    std::array<ProviderBinding,TMaximumProviders> _providers{};
    std::size_t _domainCount{0};
    std::size_t _providerCount{0};
    IRadioLogicalTransferReadySink* _readySink{nullptr};
    IRadioRuntimeTransferResultSink* _resultSink{nullptr};
    RadioIngressTrustTarget _trust{};
    mutable System::Synchronization::Mutex _mutex;
    bool _initialized{false};
    bool _running{false};
    bool _terminallyShutdown{false};

    template<class TScheduler>
    static RadioSchedulerStatus BindProviderThunk(void* context,IRadio& provider) noexcept {
        return static_cast<TScheduler*>(context)->BindProvider(provider);
    }
    template<class TScheduler>
    static RadioTransferSubmissionResult SubmitThunk(
        void* context,IRadio& provider,const RadioAddress& destination,const RadioServiceProfile& profile,
        const RadioTransferTiming& timing,const std::uint8_t* payload,std::size_t payloadBytes) noexcept {
        return static_cast<TScheduler*>(context)->Submit(provider,destination,profile,timing,payload,payloadBytes);
    }
    template<class TDomainRuntime>
    static RadioDomainRuntimeStatus InitializeRuntimeThunk(void* context,IRadioTransferResultSink* sink) {
        return static_cast<TDomainRuntime*>(context)->Initialize(sink);
    }
    template<class TDomainRuntime>
    static RadioDomainRuntimeStatus StartRuntimeThunk(void* context) {
        return static_cast<TDomainRuntime*>(context)->Start();
    }
    template<class TDomainRuntime>
    static RadioDomainRuntimeStatus ShutdownRuntimeThunk(void* context) noexcept {
        return static_cast<TDomainRuntime*>(context)->Shutdown();
    }

    std::size_t FindDomain(RadioContentionDomainId domain) const noexcept {
        for(std::size_t i=0;i<_domainCount;++i)
            if(_domains[i].Occupied && _domains[i].Domain==domain) return i;
        return TMaximumDomains;
    }
    std::size_t FindProvider(const IRadio& provider) const noexcept {
        for(std::size_t i=0;i<_providerCount;++i)
            if(_providers[i].Provider==&provider) return i;
        return TMaximumProviders;
    }
    void OnDomainTransferResolved(std::size_t index,const RadioTransferTerminalResult& result) noexcept {
        IRadioRuntimeTransferResultSink* sink=nullptr;
        RadioContentionDomainId domain{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(index>=_domainCount || !_domains[index].Occupied) return;
            domain=_domains[index].Domain;
            sink=_resultSink;
        }
        if(sink) sink->RadioLogicalTransferResolved({domain,result});
    }

public:
    explicit RadioRuntime(TReassemblyTable& reassembly) noexcept
        : _reassembly(&reassembly),_ingress(reassembly) {
        for(std::size_t i=0;i<TMaximumDomains;++i) _domains[i].Relay.Configure(this,i);
    }
    RadioRuntime(const RadioRuntime&)=delete;
    RadioRuntime& operator=(const RadioRuntime&)=delete;

    static constexpr std::size_t MaximumDomains() noexcept { return TMaximumDomains; }
    static constexpr std::size_t MaximumProviders() noexcept { return TMaximumProviders; }
    static constexpr std::size_t MaximumPeers() noexcept { return TMaximumPeers; }

    template<class TScheduler,class TDomainRuntime>
    RadioRuntimeStatus RegisterDomain(
        RadioContentionDomainId domain,TScheduler& scheduler,TDomainRuntime& runtime) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_terminallyShutdown) return RadioRuntimeStatus::TerminallyShutdown;
        if(_initialized) return RadioRuntimeStatus::Frozen;
        if(!domain || _domainCount>=TMaximumDomains || FindDomain(domain)!=TMaximumDomains)
            return RadioRuntimeStatus::InvalidConfiguration;
        auto& binding=_domains[_domainCount];
        binding.Occupied=true;
        binding.Domain=domain;
        binding.Scheduler=&scheduler;
        binding.BindProvider=&BindProviderThunk<TScheduler>;
        binding.Submit=&SubmitThunk<TScheduler>;
        binding.Runtime=&runtime;
        binding.InitializeRuntime=&InitializeRuntimeThunk<TDomainRuntime>;
        binding.StartRuntime=&StartRuntimeThunk<TDomainRuntime>;
        binding.ShutdownRuntime=&ShutdownRuntimeThunk<TDomainRuntime>;
        ++_domainCount;
        return RadioRuntimeStatus::Success;
    }

    RadioRuntimeStatus RegisterProvider(IRadio& provider) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_terminallyShutdown) return RadioRuntimeStatus::TerminallyShutdown;
        if(_initialized) return RadioRuntimeStatus::Frozen;
        if(_providerCount>=TMaximumProviders || FindProvider(provider)!=TMaximumProviders)
            return RadioRuntimeStatus::ResourceUnavailable;
        const auto domainIndex=FindDomain(provider.ContentionDomain());
        if(domainIndex>=_domainCount) return RadioRuntimeStatus::InvalidConfiguration;
        const auto bound=_domains[domainIndex].BindProvider(_domains[domainIndex].Scheduler,provider);
        if(bound!=RadioSchedulerStatus::Success) return RadioRuntimeStatus::InvalidConfiguration;
        _providers[_providerCount++]={&provider,domainIndex,false};
        return RadioRuntimeStatus::Success;
    }

    /// <summary>
    /// Returns the stable default result relay for a domain. A Clock coordinator may use it as its downstream sink.
    /// </summary>
    IRadioTransferResultSink* DomainResultSink(RadioContentionDomainId domain) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        const auto index=FindDomain(domain);
        return index<TMaximumDomains?static_cast<IRadioTransferResultSink*>(&_domains[index].Relay):nullptr;
    }

    /// <summary>Overrides one domain's scheduler result sink before Initialize, e.g. with its Clock coordinator.</summary>
    RadioRuntimeStatus SetDomainTransferResultSink(
        RadioContentionDomainId domain,IRadioTransferResultSink* sink) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_terminallyShutdown) return RadioRuntimeStatus::TerminallyShutdown;
        if(_initialized) return RadioRuntimeStatus::Frozen;
        const auto index=FindDomain(domain);
        if(index>=TMaximumDomains) return RadioRuntimeStatus::InvalidConfiguration;
        _domains[index].ResultSinkOverride=sink;
        return RadioRuntimeStatus::Success;
    }

    RadioRuntimeStatus Initialize(
        IRadioLogicalTransferReadySink* readySink,
        IRadioRuntimeTransferResultSink* resultSink,
        RadioIngressTrustTarget trust={}) {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_terminallyShutdown) return RadioRuntimeStatus::TerminallyShutdown;
        if(_initialized) return RadioRuntimeStatus::Frozen;
        if(_domainCount==0 || _providerCount==0) return RadioRuntimeStatus::InvalidConfiguration;
        _readySink=readySink;
        _resultSink=resultSink;
        _trust=trust;
        _ingress.Configure(nullptr,this,_trust);
        std::size_t initializedDomains=0;
        for(;initializedDomains<_domainCount;++initializedDomains) {
            auto& domain=_domains[initializedDomains];
            auto* sink=domain.ResultSinkOverride?domain.ResultSinkOverride:static_cast<IRadioTransferResultSink*>(&domain.Relay);
            if(domain.InitializeRuntime(domain.Runtime,sink)!=RadioDomainRuntimeStatus::Success) break;
        }
        if(initializedDomains!=_domainCount) {
            while(initializedDomains!=0) {
                --initializedDomains;
                (void)_domains[initializedDomains].ShutdownRuntime(_domains[initializedDomains].Runtime);
            }
            _readySink=nullptr;_resultSink=nullptr;
            return RadioRuntimeStatus::DomainInitializationFailed;
        }
        for(std::size_t i=0;i<_providerCount;++i) _ingress.Attach(*_providers[i].Provider);
        _initialized=true;
        return RadioRuntimeStatus::Success;
    }

    RadioRuntimeStatus Start() {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(_terminallyShutdown) return RadioRuntimeStatus::TerminallyShutdown;
            if(!_initialized) return RadioRuntimeStatus::NotInitialized;
            if(_running) return RadioRuntimeStatus::AlreadyRunning;
        }
        std::size_t startedProviders=0;
        for(;startedProviders<_providerCount;++startedProviders) {
            if(!_providers[startedProviders].Provider->Start()) break;
            _providers[startedProviders].Started=true;
        }
        if(startedProviders!=_providerCount) {
            while(startedProviders!=0) {
                --startedProviders;
                _providers[startedProviders].Provider->Stop();
                _providers[startedProviders].Started=false;
            }
            return RadioRuntimeStatus::ProviderStartFailed;
        }
        std::size_t startedDomains=0;
        for(;startedDomains<_domainCount;++startedDomains)
            if(_domains[startedDomains].StartRuntime(_domains[startedDomains].Runtime)!=RadioDomainRuntimeStatus::Success) break;
        if(startedDomains!=_domainCount) {
            while(startedDomains!=0) {
                --startedDomains;
                (void)_domains[startedDomains].ShutdownRuntime(_domains[startedDomains].Runtime);
            }
            for(std::size_t i=0;i<_providerCount;++i) {
                if(_providers[i].Started){_providers[i].Provider->Stop();_providers[i].Started=false;}
            }
            return RadioRuntimeStatus::DomainStartFailed;
        }
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _running=true;
        return RadioRuntimeStatus::Success;
    }

    RadioPeerObserveResult ObservePeer(IRadio& provider,const RadioAddress& address,RadioPeerHandle& handle) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_running || FindProvider(provider)>=_providerCount) {handle={};return RadioPeerObserveResult::Invalid;}
        return _peers.Observe(provider,address,handle);
    }

    bool InvalidatePeer(RadioPeerHandle handle) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return _peers.Invalidate(handle);
    }

    RadioTransferSubmissionResult SubmitDirect(
        IRadio& provider,const RadioAddress& destination,const RadioServiceProfile& profile,
        const RadioTransferTiming& timing,const std::uint8_t* payload,std::size_t payloadBytes) noexcept {
        void* scheduler=nullptr;
        RadioTransferSubmissionResult (*submit)(
            void*,IRadio&,const RadioAddress&,const RadioServiceProfile&,const RadioTransferTiming&,
            const std::uint8_t*,std::size_t) noexcept=nullptr;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!_running) return {RadioSchedulerStatus::NotInitialized,0};
            const auto providerIndex=FindProvider(provider);
            if(providerIndex>=_providerCount) return {RadioSchedulerStatus::InvalidConfiguration,0};
            const auto domainIndex=_providers[providerIndex].DomainIndex;
            scheduler=_domains[domainIndex].Scheduler;
            submit=_domains[domainIndex].Submit;
        }
        return submit(scheduler,provider,destination,profile,timing,payload,payloadBytes);
    }

    RadioTransferSubmissionResult SubmitPeer(
        RadioPeerHandle peer,const RadioServiceProfile& profile,const RadioTransferTiming& timing,
        const std::uint8_t* payload,std::size_t payloadBytes) noexcept {
        RadioPeerBinding binding{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!_running) return {RadioSchedulerStatus::NotInitialized,0};
            const auto* resolved=_peers.Resolve(peer);
            if(!resolved) return {RadioSchedulerStatus::InvalidConfiguration,0};
            binding=*resolved;
        }
        return SubmitDirect(*binding.Interface,binding.Address,profile,timing,payload,payloadBytes);
    }

    RadioReassemblyStatus TakeInbound(
        const RadioInboundTransferHandle& handle,RadioCompletedReassembly& output) noexcept {
        if(!handle.IsValid()) return RadioReassemblyStatus::NotFound;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!_running || FindProvider(*handle.Provider)>=_providerCount) return RadioReassemblyStatus::NotFound;
        }
        return _reassembly->TakeCompleteTrusted(*handle.Provider,handle.Source,handle.TransferId,output);
    }

    RadioReassemblyStatus PromoteInbound(
        IRadio& provider,const RadioAddress& source,RadioTransferId transferId,RadioServiceClass validatedService) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!_running || FindProvider(provider)>=_providerCount) return RadioReassemblyStatus::NotFound;
        }
        return _reassembly->PromoteCompleted(provider,source,transferId,validatedService);
    }

    std::size_t ExpireInbound(std::uint64_t nowNanoseconds) noexcept {
        return _reassembly->Expire(nowNanoseconds);
    }

    RadioRuntimeStatus Shutdown() noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(_terminallyShutdown) return RadioRuntimeStatus::Success;
            _running=false;
            _terminallyShutdown=true;
        }
        // Stop new provider callbacks before joining the one service context per domain.
        for(std::size_t i=0;i<_providerCount;++i) _ingress.Detach(*_providers[i].Provider);
        for(std::size_t i=0;i<_domainCount;++i)
            (void)_domains[i].ShutdownRuntime(_domains[i].Runtime);
        for(std::size_t i=0;i<_providerCount;++i) {
            if(_providers[i].Started) {
                _providers[i].Provider->Stop();
                _providers[i].Started=false;
            }
        }
        (void)_reassembly->Expire(std::numeric_limits<std::uint64_t>::max());
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            _peers.Clear();
            _readySink=nullptr;
            _resultSink=nullptr;
            _initialized=false;
        }
        return RadioRuntimeStatus::Success;
    }

    bool IsInitialized() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _initialized;
    }
    bool IsRunning() const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);return _running;
    }

    void RadioReassemblyReady(
        IRadio& provider,const RadioAddress& source,RadioTransferId transferId,RadioServiceClass service) noexcept override {
        IRadioLogicalTransferReadySink* sink=nullptr;
        RadioPeerHandle peer{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!_running || FindProvider(provider)>=_providerCount) return;
            (void)_peers.Observe(provider,source,peer);
            sink=_readySink;
        }
        if(sink) sink->RadioLogicalTransferReady({&provider,peer,source,transferId,service});
    }
};

} // namespace ESPressio::Radio
