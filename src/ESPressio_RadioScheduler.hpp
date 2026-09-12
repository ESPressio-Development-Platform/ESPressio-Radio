#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include <ESPressio_Synchronization.hpp>

#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioCapacity.hpp"
#include "ESPressio_RadioTransferId.hpp"
#include "ESPressio_RadioWireV3.hpp"

namespace ESPressio::Radio {

enum class RadioTransferTerminalStatus : std::uint8_t {
    Completed = 0,
    TransmissionFailed,
    EvidenceInsufficient,
    Expired,
    ProviderUnavailable,
    Cancelled
};

struct RadioTransferTerminalResult final {
    RadioTransferId TransferId{0};
    RadioTransferTerminalStatus Status{RadioTransferTerminalStatus::TransmissionFailed};
    RadioDirectLinkEvidence Evidence{};
};

/// <summary>Fixed transport-owner sink for one terminal Radio logical-transfer result.</summary>
class IRadioTransferResultSink {
public:
    virtual ~IRadioTransferResultSink() = default;
    virtual void RadioTransferResolved(const RadioTransferTerminalResult& result) noexcept = 0;
};

struct RadioSchedulerWakeTarget final {
    void* Context{nullptr};
    void (*Wake)(void*) noexcept{nullptr};
};

enum class RadioSchedulerStatus : std::uint8_t {
    Success = 0,
    Busy,
    NotInitialized,
    Frozen,
    InvalidConfiguration,
    ResourceUnavailable,
    ProviderUnavailable,
    PayloadTooLarge,
    Expired
};

struct RadioTransferSubmissionResult final {
    RadioSchedulerStatus Status{RadioSchedulerStatus::InvalidConfiguration};
    RadioTransferId TransferId{0};
    constexpr explicit operator bool() const noexcept { return Status == RadioSchedulerStatus::Success; }
};

struct RadioSchedulerServiceResult final {
    RadioSchedulerStatus Status{RadioSchedulerStatus::Success};
    bool ImmediateWorkRemaining{false};
    std::uint64_t EarliestDeadlineNanoseconds{0};
};

/// <summary>Compile-time weighted-DRR and bounded-promotion profile for one contention domain.</summary>
template<std::uint64_t TInfrastructureQuantum,
         std::uint64_t TClockQuantum,
         std::uint64_t TCriticalQuantum,
         std::uint64_t TResponsiveQuantum,
         std::uint64_t TConvergentQuantum,
         std::uint64_t TBestEffortQuantum,
         std::uint64_t TMaximumPromotionDebt,
         std::uint64_t TSchedulingGuardNanoseconds>
struct RadioSchedulerProfile final {
    static_assert(TInfrastructureQuantum>0 && TClockQuantum>0 && TCriticalQuantum>0 &&
                  TResponsiveQuantum>0 && TConvergentQuantum>0 && TBestEffortQuantum>0,
                  "Every enabled Radio class requires a positive finite DRR quantum");
    static_assert(TInfrastructureQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                  TClockQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                  TCriticalQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                  TResponsiveQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                  TConvergentQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                  TBestEffortQuantum<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                  "Radio DRR quanta must fit signed deficit accounting");
    static_assert(TMaximumPromotionDebt<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                  "Radio promotion debt must fit signed deficit accounting");

    inline static constexpr std::array<std::uint64_t,RadioServiceClassCount> Quanta{
        TInfrastructureQuantum,TClockQuantum,TCriticalQuantum,TResponsiveQuantum,TConvergentQuantum,TBestEffortQuantum};
    static constexpr std::int64_t MaximumPromotionDebt=static_cast<std::int64_t>(TMaximumPromotionDebt);
    static constexpr std::uint64_t SchedulingGuardNanoseconds=TSchedulingGuardNanoseconds;
};

struct RadioOutboundTransferRecord final {
    RadioByteLease Bytes;
    IRadio* Provider{nullptr};
    RadioAddress Destination{};
    RadioTransferId TransferId{0};
    RadioServiceProfile Profile{};
    RadioTransferTiming Timing{};
    std::uint8_t FragmentCount{0};
    std::uint8_t NextFragmentIndex{0};
    RadioDirectLinkEvidence AggregateEvidence{};

    RadioOutboundTransferRecord(
        RadioByteLease&& bytes,
        IRadio* provider,
        const RadioAddress& destination,
        RadioTransferId transferId,
        const RadioServiceProfile& profile,
        const RadioTransferTiming& timing,
        std::uint8_t fragmentCount) noexcept
        : Bytes(std::move(bytes)),Provider(provider),Destination(destination),TransferId(transferId),
          Profile(profile),Timing(timing),FragmentCount(fragmentCount) {}

    bool IsComplete() const noexcept { return FragmentCount!=0 && NextFragmentIndex>=FragmentCount; }
};

namespace Detail {
template<std::size_t TDepth>
class RadioTransferQueue final {
    static_assert(TDepth>0);
    std::array<RadioCapacityRecordLease,TDepth> _items{};
    std::size_t _head{0},_tail{0},_count{0};
public:
    bool Empty() const noexcept{return _count==0;}
    bool Full() const noexcept{return _count==_items.size();}
    std::size_t Size()const noexcept{return _count;}
    RadioCapacityRecordLease* Head() noexcept{return _count?&_items[_head]:nullptr;}
    const RadioCapacityRecordLease* Head()const noexcept{return _count?&_items[_head]:nullptr;}
    bool Push(RadioCapacityRecordLease&& lease) noexcept{
        if(!lease||Full())return false;
        _items[_tail]=std::move(lease);_tail=(_tail+1u)%_items.size();++_count;return true;
    }
    bool Pop(RadioCapacityRecordLease& output) noexcept{
        if(output||Empty())return false;
        output=std::move(_items[_head]);_head=(_head+1u)%_items.size();--_count;return true;
    }
    template<class TPredicate>
    bool Any(TPredicate&& predicate)const noexcept{
        for(std::size_t i=0,index=_head;i<_count;++i,index=(index+1u)%_items.size())
            if(predicate(_items[index]))return true;
        return false;
    }
    void Drain() noexcept{for(auto& item:_items)item.Reset();_head=_tail=_count=0;}
};
}

/// <summary>
/// One bounded R3 arbiter for one physical contention domain.
/// </summary>
/// <remarks>
/// It owns six FIFO class queues, rotating DRR deficits, one fixed physical-frame scratch buffer and at most one
/// scheduler-owned outstanding physical fragment. Provider Busy never creates a polling retry; fixed provider/capacity/
/// deadline wakes ask the owner to call Service() again. This class owns no Task; RadioDomainRuntime supplies the one T1
/// execution context in the next layer.
/// </remarks>
template<class TOutboundCapacity,class TSchedulerProfile,
         std::size_t TQueueDepth,std::size_t TRecentTransferIds,
         std::size_t TMaximumProviders,std::size_t TPhysicalScratchBytes>
class RadioDomainScheduler final : public IRadioRuntimeSink {
    static_assert(TQueueDepth>0 && TRecentTransferIds>0 && TMaximumProviders>0 && TPhysicalScratchBytes>0);
    TOutboundCapacity* _capacity{nullptr};
    RadioContentionDomainId _domain{};
    std::array<IRadio*,TMaximumProviders> _providers{};
    std::size_t _providerCount{0};
    std::size_t _inboundProviderCursor{0};
    std::array<Detail::RadioTransferQueue<TQueueDepth>,RadioServiceClassCount> _queues{};
    std::array<std::int64_t,RadioServiceClassCount> _deficit{};
    std::size_t _cursor{0};
    RadioTransferIdIssuer<TRecentTransferIds> _ids{};
    IRadioTransferResultSink* _resultSink{nullptr};
    RadioSchedulerWakeTarget _wake{};
    System::Synchronization::Mutex _mutex;
    std::array<std::uint8_t,TPhysicalScratchBytes> _scratch{};
    bool _initialized{false};
    bool _stopping{false};

    bool _outstanding{false};
    std::size_t _outstandingClass{0};
    IRadio* _outstandingProvider{nullptr};
    RadioTransmissionHandle _outstandingHandle{};
    RadioCapacityRecordIdentity _outstandingRecord{};
    std::atomic<IRadio*> _publishedOutstandingProvider{nullptr};
    std::atomic<std::uint32_t> _publishedOutstandingHandle{0};
    std::atomic<bool> _completionPending{false};
    std::atomic<std::uint8_t> _completionTransmission{static_cast<std::uint8_t>(RadioTransmissionCompletion::Unknown)};
    std::atomic<std::uint8_t> _completionAcknowledgement{static_cast<std::uint8_t>(RadioPeerAcknowledgement::Unavailable)};
    std::atomic<std::uint64_t> _staleProviderCompletions{0};

    static std::size_t ClassIndex(RadioServiceClass service) noexcept{
        const auto raw=static_cast<std::uint8_t>(service);
        return raw>=1&&raw<=RadioServiceClassCount?static_cast<std::size_t>(raw-1u):RadioServiceClassCount;
    }
    void Wake() noexcept{if(_wake.Wake)_wake.Wake(_wake.Context);}
    bool ProviderBound(IRadio& provider)const noexcept{
        for(std::size_t i=0;i<_providerCount;++i)if(_providers[i]==&provider)return true;
        return false;
    }
    bool ActiveTransferId(RadioTransferId id)const noexcept{
        for(const auto& queue:_queues)
            if(queue.Any([&](const RadioCapacityRecordLease& lease)noexcept{
                return lease.template Get<RadioOutboundTransferRecord>().TransferId==id;
            }))return true;
        return false;
    }
    bool HasQueuedWork()const noexcept{for(const auto& queue:_queues)if(!queue.Empty())return true;return false;}
    bool HasReadyProviderWork()const noexcept{
        for(const auto& queue:_queues){
            const auto* head=queue.Head();
            if(!head)continue;
            const auto& record=head->template Get<RadioOutboundTransferRecord>();
            if(record.Provider&&record.Provider->IsTransmitReady())return true;
        }
        return false;
    }

    static bool AddSaturating(std::uint64_t a,std::uint64_t b,std::uint64_t& out)noexcept{
        if(a>std::numeric_limits<std::uint64_t>::max()-b){out=std::numeric_limits<std::uint64_t>::max();return false;}
        out=a+b;return true;
    }
    static std::uint64_t FragmentPayloadBytes(const RadioOutboundTransferRecord& record)noexcept{
        const auto mtu=record.Provider->Capabilities().MaximumPayloadBytes;
        const auto chunk=RadioTransportV3MaximumFragmentPayload(mtu,record.Provider->LocalAddress().Length);
        const auto offset=static_cast<std::size_t>(record.NextFragmentIndex)*chunk;
        const auto total=record.Bytes.Length();
        if(chunk==0||offset>=total)return 0;
        const auto remaining=total-offset;
        return remaining<chunk?remaining:chunk;
    }
    static std::size_t EncodedFragmentBytes(const RadioOutboundTransferRecord& record)noexcept{
        const auto payload=FragmentPayloadBytes(record);
        const auto header=RadioTransportV3HeaderBytes(record.Provider->LocalAddress());
        return payload&&header?header+payload:0;
    }
    static RadioTransmissionCost CostFor(const RadioOutboundTransferRecord& record)noexcept{
        const auto bytes=EncodedFragmentBytes(record);
        return bytes?record.Provider->EstimateTransmissionCost(record.Destination,bytes,record.Profile):RadioTransmissionCost{};
    }
    static bool IsUrgent(
        const RadioOutboundTransferRecord& record,
        const RadioTransmissionCost& cost,
        std::uint64_t now)noexcept{
        if(record.Profile.DeadlineTreatment!=RadioDeadlineTreatment::Promotable ||
           !cost.SupportsPromotableDeadline() || record.Timing.ServiceDeadlineNanoseconds==0)return false;
        std::uint64_t threshold=0;
        AddSaturating(now,cost.ConservativeAirtimeNanoseconds,threshold);
        AddSaturating(threshold,TSchedulerProfile::SchedulingGuardNanoseconds,threshold);
        return threshold>=record.Timing.ServiceDeadlineNanoseconds;
    }
    static bool EvidenceSatisfies(const RadioOutboundTransferRecord& record,const RadioDirectLinkEvidence& evidence)noexcept{
        return SatisfiesDirectLinkEvidence(evidence,record.Profile.RequiredDirectLinkEvidence);
    }

    void ResolveTransfer(std::size_t classIndex,RadioTransferTerminalStatus status,const RadioDirectLinkEvidence& evidence)noexcept{
        RadioCapacityRecordLease terminal;
        if(!_queues[classIndex].Pop(terminal))return;
        const auto transferId=terminal.template Get<RadioOutboundTransferRecord>().TransferId;
        _ids.RememberCompleted(transferId);
        if(_resultSink)_resultSink->RadioTransferResolved({transferId,status,evidence});
    }
    void AdvanceSuccessfulFragment(std::size_t classIndex,const RadioDirectLinkEvidence& evidence)noexcept{
        auto* head=_queues[classIndex].Head();
        if(!head)return;
        auto& record=head->template Get<RadioOutboundTransferRecord>();
        record.AggregateEvidence.Transmission=RadioTransmissionCompletion::Completed;
        if(evidence.PeerAcknowledged())record.AggregateEvidence.PeerAcknowledgement=RadioPeerAcknowledgement::Acknowledged;
        else if(record.AggregateEvidence.PeerAcknowledgement!=RadioPeerAcknowledgement::Acknowledged)
            record.AggregateEvidence.PeerAcknowledgement=evidence.PeerAcknowledgement;
        if(!EvidenceSatisfies(record,evidence)){
            ResolveTransfer(classIndex,RadioTransferTerminalStatus::EvidenceInsufficient,record.AggregateEvidence);
            return;
        }
        ++record.NextFragmentIndex;
        if(record.IsComplete())ResolveTransfer(classIndex,RadioTransferTerminalStatus::Completed,record.AggregateEvidence);
    }
    void ConsumePendingCompletion()noexcept{
        if(!_completionPending.exchange(false,std::memory_order_acq_rel))return;
        if(!_outstanding)return;
        auto* head=_queues[_outstandingClass].Head();
        if(!head||head->Identity().Generation!=_outstandingRecord.Generation||
           head->Identity().Slot!=_outstandingRecord.Slot||head->Identity().Domain!=_outstandingRecord.Domain){
            ++_staleProviderCompletions;ClearOutstanding();return;
        }
        const RadioDirectLinkEvidence evidence{
            static_cast<RadioTransmissionCompletion>(_completionTransmission.load(std::memory_order_acquire)),
            static_cast<RadioPeerAcknowledgement>(_completionAcknowledgement.load(std::memory_order_acquire))};
        const auto classIndex=_outstandingClass;
        ClearOutstanding();
        if(evidence.TransmissionFailed())ResolveTransfer(classIndex,RadioTransferTerminalStatus::TransmissionFailed,evidence);
        else if(evidence.TransmissionCompleted())AdvanceSuccessfulFragment(classIndex,evidence);
        else ++_staleProviderCompletions;
    }
    void ClearOutstanding()noexcept{
        _publishedOutstandingHandle.store(0,std::memory_order_release);
        _publishedOutstandingProvider.store(nullptr,std::memory_order_release);
        _outstanding=false;_outstandingClass=0;_outstandingProvider=nullptr;_outstandingHandle={};_outstandingRecord={};
    }

    std::size_t SelectUrgent(std::uint64_t now)noexcept{
        std::size_t selected=RadioServiceClassCount;
        std::uint64_t earliest=std::numeric_limits<std::uint64_t>::max();
        for(std::size_t i=0;i<RadioServiceClassCount;++i){
            auto* head=_queues[i].Head();if(!head)continue;
            auto& record=head->template Get<RadioOutboundTransferRecord>();
            if(record.Timing.ExpiryNanoseconds<=now)continue;
            const auto cost=CostFor(record);
            if(!cost.IsValid()||!IsUrgent(record,cost,now)||!record.Provider->IsTransmitReady())continue;
            if(cost.FairnessCostUnits>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))continue;
            const auto signedCost=static_cast<std::int64_t>(cost.FairnessCostUnits);
            if(_deficit[i] < -TSchedulerProfile::MaximumPromotionDebt + signedCost)continue;
            if(record.Timing.ServiceDeadlineNanoseconds<earliest){earliest=record.Timing.ServiceDeadlineNanoseconds;selected=i;}
        }
        return selected;
    }

    std::size_t SelectNormal(bool& immediate)noexcept{
        immediate=false;
        for(std::size_t visited=0;visited<RadioServiceClassCount;++visited){
            const auto index=_cursor;
            _cursor=(_cursor+1u)%RadioServiceClassCount;
            auto* head=_queues[index].Head();
            if(!head){if(_deficit[index]>0)_deficit[index]=0;continue;}
            auto& record=head->template Get<RadioOutboundTransferRecord>();
            const auto quantum=TSchedulerProfile::Quanta[index];
            const auto max=std::numeric_limits<std::int64_t>::max();
            if(_deficit[index]>max-static_cast<std::int64_t>(quantum))_deficit[index]=max;
            else _deficit[index]+=static_cast<std::int64_t>(quantum);
            const auto cost=CostFor(record);
            if(!cost.IsValid()||cost.FairnessCostUnits>static_cast<std::uint64_t>(max))return index;
            if(!record.Provider->IsTransmitReady())continue;
            if(static_cast<std::uint64_t>(_deficit[index]>0?_deficit[index]:0)>=cost.FairnessCostUnits)return index;
            immediate=true;
        }
        return RadioServiceClassCount;
    }

    bool EncodeHead(std::size_t classIndex,std::uint64_t now,std::size_t& encoded,RadioTransmissionCost& cost)noexcept{
        auto* head=_queues[classIndex].Head();if(!head)return false;
        auto& record=head->template Get<RadioOutboundTransferRecord>();
        if(record.Timing.ExpiryNanoseconds<=now)return false;
        std::uint32_t residence=0;
        if(!TryEncodeRemainingResidenceMilliseconds(record.Timing.ExpiryNanoseconds,now,residence))return false;
        const auto providerAddress=record.Provider->LocalAddress();
        const auto chunk=RadioTransportV3MaximumFragmentPayload(record.Provider->Capabilities().MaximumPayloadBytes,providerAddress.Length);
        if(chunk==0)return false;
        const auto offset=static_cast<std::size_t>(record.NextFragmentIndex)*chunk;
        const auto total=record.Bytes.Length();if(offset>=total)return false;
        const auto remaining=total-offset;const auto fragmentBytes=remaining<chunk?remaining:chunk;
        const auto view=record.Bytes.View();
        const RadioTransportV3Header header{record.TransferId,record.NextFragmentIndex,record.FragmentCount,
            static_cast<std::uint16_t>(total),providerAddress,record.Profile.Class,residence};
        if(!EncodeRadioTransportV3Fragment(header,view.Data+offset,fragmentBytes,_scratch.data(),_scratch.size(),encoded))return false;
        cost=record.Provider->EstimateTransmissionCost(record.Destination,encoded,record.Profile);
        return cost.IsValid() &&
            (record.Profile.DeadlineTreatment!=RadioDeadlineTreatment::Promotable||cost.SupportsPromotableDeadline());
    }

    RadioSchedulerServiceResult SubmitSelected(std::size_t classIndex,std::uint64_t now,bool promoted)noexcept{
        auto* head=_queues[classIndex].Head();if(!head)return {RadioSchedulerStatus::Success,HasQueuedWork(),EarliestDeadlineLocked(now)};
        auto& record=head->template Get<RadioOutboundTransferRecord>();
        if(record.Timing.ExpiryNanoseconds<=now){ResolveTransfer(classIndex,RadioTransferTerminalStatus::Expired,{});return {RadioSchedulerStatus::Success,HasQueuedWork(),EarliestDeadlineLocked(now)};}
        std::size_t encoded=0;RadioTransmissionCost cost{};
        if(!EncodeHead(classIndex,now,encoded,cost)){
            ResolveTransfer(classIndex,RadioTransferTerminalStatus::ProviderUnavailable,{});
            return {RadioSchedulerStatus::ProviderUnavailable,HasQueuedWork(),EarliestDeadlineLocked(now)};
        }
        if(cost.FairnessCostUnits>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){
            ResolveTransfer(classIndex,RadioTransferTerminalStatus::ProviderUnavailable,{});
            return {RadioSchedulerStatus::ProviderUnavailable,HasQueuedWork(),EarliestDeadlineLocked(now)};
        }
        const auto signedCost=static_cast<std::int64_t>(cost.FairnessCostUnits);
        if(promoted){
            if(_deficit[classIndex] < -TSchedulerProfile::MaximumPromotionDebt + signedCost)
                return {RadioSchedulerStatus::Success,false,EarliestDeadlineLocked(now)};
        }else if(_deficit[classIndex]<signedCost){
            return {RadioSchedulerStatus::Success,true,EarliestDeadlineLocked(now)};
        }
        const auto sent=record.Provider->Send(record.Destination,_scratch.data(),encoded);
        if(sent.Status==RadioSendStatus::Busy){
            return {RadioSchedulerStatus::Success,false,EarliestDeadlineLocked(now)};
        }
        if(!sent){
            ResolveTransfer(classIndex,RadioTransferTerminalStatus::ProviderUnavailable,sent.Evidence);
            return {RadioSchedulerStatus::ProviderUnavailable,HasQueuedWork(),EarliestDeadlineLocked(now)};
        }
        _deficit[classIndex]-=signedCost;
        if(sent.Evidence.IsTerminal()){
            if(sent.Evidence.TransmissionFailed())ResolveTransfer(classIndex,RadioTransferTerminalStatus::TransmissionFailed,sent.Evidence);
            else AdvanceSuccessfulFragment(classIndex,sent.Evidence);
            return {RadioSchedulerStatus::Success,HasQueuedWork(),EarliestDeadlineLocked(now)};
        }
        if(!sent.DeferredTransmission){
            ResolveTransfer(classIndex,RadioTransferTerminalStatus::ProviderUnavailable,sent.Evidence);
            return {RadioSchedulerStatus::ProviderUnavailable,HasQueuedWork(),EarliestDeadlineLocked(now)};
        }
        _outstanding=true;_outstandingClass=classIndex;_outstandingProvider=record.Provider;
        _outstandingHandle=sent.DeferredTransmission;_outstandingRecord=head->Identity();
        _publishedOutstandingProvider.store(record.Provider,std::memory_order_release);
        _publishedOutstandingHandle.store(sent.DeferredTransmission.Value,std::memory_order_release);
        return {RadioSchedulerStatus::Success,false,EarliestDeadlineLocked(now)};
    }

    std::uint64_t EarliestDeadlineLocked(std::uint64_t now)const noexcept{
        std::uint64_t earliest=0;
        for(const auto& queue:_queues){
            const auto* head=queue.Head();if(!head)continue;
            const auto& record=head->template Get<RadioOutboundTransferRecord>();
            const auto candidate=record.Profile.DeadlineTreatment==RadioDeadlineTreatment::Promotable&&record.Timing.ServiceDeadlineNanoseconds>now
                ?record.Timing.ServiceDeadlineNanoseconds:record.Timing.ExpiryNanoseconds;
            if(candidate!=0&&(earliest==0||candidate<earliest))earliest=candidate;
        }
        return earliest;
    }

public:
    RadioDomainScheduler(TOutboundCapacity& capacity,RadioContentionDomainId domain)noexcept:_capacity(&capacity),_domain(domain){}
    RadioDomainScheduler(const RadioDomainScheduler&)=delete;
    RadioDomainScheduler& operator=(const RadioDomainScheduler&)=delete;

    RadioSchedulerStatus BindProvider(IRadio& provider)noexcept{
        if(_initialized)return RadioSchedulerStatus::Frozen;
        if(!_domain||provider.ContentionDomain()!=_domain||_providerCount==_providers.size()||ProviderBound(provider))
            return RadioSchedulerStatus::InvalidConfiguration;
        if(!provider.ProviderResources().HasFiniteIngressService())return RadioSchedulerStatus::InvalidConfiguration;
        _providers[_providerCount++]=&provider;return RadioSchedulerStatus::Success;
    }
    RadioSchedulerStatus Initialize(IRadioTransferResultSink* resultSink={},RadioSchedulerWakeTarget wake={})noexcept{
        if(_initialized)return RadioSchedulerStatus::Frozen;
        if(!_domain||_providerCount==0)return RadioSchedulerStatus::InvalidConfiguration;
        _resultSink=resultSink;_wake=wake;
        {std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}
        for(std::size_t i=0;i<_providerCount;++i)_providers[i]->SetRuntimeSink(this);
        _initialized=true;return RadioSchedulerStatus::Success;
    }

    RadioTransferSubmissionResult Submit(
        IRadio& provider,const RadioAddress& destination,const RadioServiceProfile& profile,
        const RadioTransferTiming& timing,const std::uint8_t* payload,std::size_t payloadBytes)noexcept{
        if(!_initialized)return {RadioSchedulerStatus::NotInitialized,0};
        if(_stopping||!ProviderBound(provider)||!profile.IsValid()||!timing.IsValidFor(profile)||
           !destination.IsValid()||(payloadBytes!=0&&payload==nullptr)||payloadBytes==0)
            return {RadioSchedulerStatus::InvalidConfiguration,0};
        const auto capabilities=provider.Capabilities();
        const auto effectiveMaximum=RadioTransportV3MaximumLogicalPayload(
            capabilities.MaximumPayloadBytes,provider.LocalAddress().Length,capabilities.MaximumLogicalTransferBytes);
        if(payloadBytes>effectiveMaximum||payloadBytes>std::numeric_limits<std::uint16_t>::max())
            return {RadioSchedulerStatus::PayloadTooLarge,0};
        const auto chunk=RadioTransportV3MaximumFragmentPayload(capabilities.MaximumPayloadBytes,provider.LocalAddress().Length);
        if(chunk==0)return {RadioSchedulerStatus::InvalidConfiguration,0};
        const auto fragmentCount=(payloadBytes+chunk-1u)/chunk;
        if(fragmentCount==0||fragmentCount>255)return {RadioSchedulerStatus::PayloadTooLarge,0};
        if(profile.DeadlineTreatment==RadioDeadlineTreatment::Promotable){
            const auto probe=provider.EstimateTransmissionCost(destination,
                RadioTransportV3HeaderBytes(provider.LocalAddress())+(payloadBytes<chunk?payloadBytes:chunk),profile);
            if(!probe.SupportsPromotableDeadline())return {RadioSchedulerStatus::InvalidConfiguration,0};
        }
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return {RadioSchedulerStatus::Busy,0};
        RadioTransferId id=0;
        if(!_ids.TryIssue([this](RadioTransferId candidate)noexcept{return ActiveTransferId(candidate);},id))
            return {RadioSchedulerStatus::ResourceUnavailable,0};
        RadioCapacityReservation reservation;
        const auto reserved=_capacity->TryAcquireTrusted(profile.Class,payloadBytes,reservation);
        if(reserved==RadioResourceStatus::Busy)return {RadioSchedulerStatus::Busy,0};
        if(reserved!=RadioResourceStatus::Success)return {RadioSchedulerStatus::ResourceUnavailable,0};
        auto target=reservation.Bytes().MutableView();if(!target||target.Capacity<payloadBytes)return {RadioSchedulerStatus::ResourceUnavailable,0};
        std::memcpy(target.Data,payload,payloadBytes);
        if(reservation.Bytes().Commit(payloadBytes)!=RadioResourceStatus::Success)return {RadioSchedulerStatus::ResourceUnavailable,0};
        RadioCapacityRecordLease lease;
        const auto built=_capacity->template Construct<RadioOutboundTransferRecord>(
            std::move(reservation),lease,&provider,destination,id,profile,timing,static_cast<std::uint8_t>(fragmentCount));
        if(built!=RadioResourceStatus::Success)return {RadioSchedulerStatus::ResourceUnavailable,0};
        const auto classIndex=ClassIndex(profile.Class);
        if(classIndex>=RadioServiceClassCount||!_queues[classIndex].Push(std::move(lease)))
            return {RadioSchedulerStatus::ResourceUnavailable,0};
        Wake();return {RadioSchedulerStatus::Success,id};
    }

    RadioSchedulerServiceResult Service(std::uint64_t now)noexcept{
        if(!_initialized)return {RadioSchedulerStatus::NotInitialized,false,0};
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return {RadioSchedulerStatus::Busy,false,0};
        ConsumePendingCompletion();
        if(_outstanding)return {RadioSchedulerStatus::Success,false,EarliestDeadlineLocked(now)};
        for(std::size_t i=0;i<RadioServiceClassCount;++i){
            auto* head=_queues[i].Head();if(!head)continue;
            if(head->template Get<RadioOutboundTransferRecord>().Timing.ExpiryNanoseconds<=now){
                ResolveTransfer(i,RadioTransferTerminalStatus::Expired,{});
                return {RadioSchedulerStatus::Success,HasQueuedWork(),EarliestDeadlineLocked(now)};
            }
        }
        const auto urgent=SelectUrgent(now);
        if(urgent<RadioServiceClassCount)return SubmitSelected(urgent,now,true);
        bool immediate=false;const auto selected=SelectNormal(immediate);
        if(selected<RadioServiceClassCount)return SubmitSelected(selected,now,false);
        return {RadioSchedulerStatus::Success,immediate&&HasReadyProviderWork(),EarliestDeadlineLocked(now)};
    }

    ManagedRadioIngressServiceResult ServiceOneInboundProvider(std::size_t maximumPackets=0)noexcept{
        ManagedRadioIngressServiceResult result{};if(!_initialized||_providerCount==0)return result;
        const auto index=_inboundProviderCursor;_inboundProviderCursor=(_inboundProviderCursor+1u)%_providerCount;
        return _providers[index]->ServiceInbound(maximumPackets);
    }
    std::uint64_t EarliestDeadline(std::uint64_t now)noexcept{
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        return lock.owns_lock()?EarliestDeadlineLocked(now):0;
    }
    std::uint64_t StaleProviderCompletions()const noexcept{return _staleProviderCompletions.load(std::memory_order_acquire);}
    bool HasOutstandingFragment()const noexcept{return _publishedOutstandingProvider.load(std::memory_order_acquire)!=nullptr;}

    RadioSchedulerStatus Shutdown()noexcept{
        if(!_initialized)return RadioSchedulerStatus::Success;
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);_stopping=true;
        for(auto* provider:_providers)if(provider)provider->SetRuntimeSink(nullptr);
        if(_outstanding)ClearOutstanding();
        for(auto& queue:_queues)queue.Drain();
        _initialized=false;return RadioSchedulerStatus::Success;
    }

    void InboundAvailable(IRadio&)noexcept override{Wake();}
    void TransmissionResolved(IRadio& provider,RadioTransmissionHandle handle,const RadioDirectLinkEvidence& evidence)noexcept override{
        if(!evidence.IsTerminal()||_publishedOutstandingProvider.load(std::memory_order_acquire)!=&provider||
           _publishedOutstandingHandle.load(std::memory_order_acquire)!=handle.Value){++_staleProviderCompletions;return;}
        _completionTransmission.store(static_cast<std::uint8_t>(evidence.Transmission),std::memory_order_relaxed);
        _completionAcknowledgement.store(static_cast<std::uint8_t>(evidence.PeerAcknowledgement),std::memory_order_relaxed);
        _completionPending.store(true,std::memory_order_release);Wake();
    }
    void TransmitReadinessChanged(IRadio&)noexcept override{Wake();}
    void LifecycleAvailabilityChanged(IRadio&)noexcept override{Wake();}
};

} // namespace ESPressio::Radio
