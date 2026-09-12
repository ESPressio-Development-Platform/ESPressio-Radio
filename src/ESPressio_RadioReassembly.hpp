#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include <ESPressio_Synchronization.hpp>

#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioCapacity.hpp"
#include "ESPressio_RadioWireV3.hpp"

namespace ESPressio::Radio {

enum class RadioReassemblyStatus : std::uint8_t {
    Accepted = 0,
    Duplicate,
    Complete,
    RecentlyCompleted,
    Busy,
    ResourceUnavailable,
    Malformed,
    Expired,
    NotFound,
    NotTrusted
};

/// <summary>Radio-owned metadata and complete logical byte lease for one active v3 reassembly.</summary>
struct RadioReassemblyRecord final {
    RadioByteLease Bytes;
    std::uint8_t* Fill{nullptr};
    IRadio* Provider{nullptr};
    RadioAddress Source{};
    RadioAddress Destination{};
    RadioTransferId TransferId{0};
    RadioServiceClass Service{RadioServiceClass::Invalid};
    std::uint8_t FragmentCount{0};
    std::uint16_t LogicalPayloadBytes{0};
    std::uint16_t ReceivedCount{0};
    std::uint64_t ExpiryNanoseconds{0};
    RadioPacketFlag Flags{RadioPacketFlag::None};
    std::array<std::uint8_t,32> ReceivedBitmap{};

    RadioReassemblyRecord(
        RadioByteLease&& bytes,
        std::uint8_t* fill,
        IRadio* provider,
        const RadioAddress& source,
        const RadioAddress& destination,
        RadioTransferId transferId,
        RadioServiceClass service,
        std::uint8_t fragmentCount,
        std::uint16_t logicalPayloadBytes,
        std::uint64_t expiryNanoseconds,
        RadioPacketFlag flags) noexcept
        : Bytes(std::move(bytes)), Fill(fill), Provider(provider), Source(source), Destination(destination),
          TransferId(transferId), Service(service), FragmentCount(fragmentCount),
          LogicalPayloadBytes(logicalPayloadBytes), ExpiryNanoseconds(expiryNanoseconds), Flags(flags) {}

    RadioReassemblyRecord(
        RadioByteLease&& bytes,
        std::uint8_t* fill,
        const RadioReassemblyRecord& sourceRecord) noexcept
        : Bytes(std::move(bytes)), Fill(fill), Provider(sourceRecord.Provider), Source(sourceRecord.Source),
          Destination(sourceRecord.Destination), TransferId(sourceRecord.TransferId), Service(sourceRecord.Service),
          FragmentCount(sourceRecord.FragmentCount), LogicalPayloadBytes(sourceRecord.LogicalPayloadBytes),
          ReceivedCount(sourceRecord.ReceivedCount), ExpiryNanoseconds(sourceRecord.ExpiryNanoseconds),
          Flags(sourceRecord.Flags), ReceivedBitmap(sourceRecord.ReceivedBitmap) {}

    bool HasFragment(std::uint8_t index) const noexcept {
        return index < FragmentCount &&
            (ReceivedBitmap[index / 8u] & static_cast<std::uint8_t>(1u << (index % 8u))) != 0;
    }
    void MarkFragment(std::uint8_t index) noexcept {
        ReceivedBitmap[index / 8u] |= static_cast<std::uint8_t>(1u << (index % 8u));
        ++ReceivedCount;
    }
    bool IsComplete() const noexcept { return FragmentCount != 0 && ReceivedCount == FragmentCount; }
    bool IsExpired(std::uint64_t now) const noexcept { return ExpiryNanoseconds == 0 || now >= ExpiryNanoseconds; }
};

/// <summary>Move-only complete trusted logical transfer; its bytes remain owned until this object is released.</summary>
class RadioCompletedReassembly final {
    RadioCapacityRecordLease _lease{};
    template<class,std::size_t,std::size_t> friend class RadioReassemblyTable;
public:
    RadioCompletedReassembly() noexcept = default;
    RadioCompletedReassembly(const RadioCompletedReassembly&) = delete;
    RadioCompletedReassembly& operator=(const RadioCompletedReassembly&) = delete;
    RadioCompletedReassembly(RadioCompletedReassembly&&) noexcept = default;
    RadioCompletedReassembly& operator=(RadioCompletedReassembly&&) noexcept = default;
    explicit operator bool() const noexcept { return bool(_lease); }
    const RadioReassemblyRecord& Record() const noexcept { return _lease.Get<RadioReassemblyRecord>(); }
    RadioByteView Payload() const noexcept { return Record().Bytes.View(); }
    void Reset() noexcept { _lease.Reset(); }
};

/// <summary>Bounded recent-delivery window used to suppress transfer-id duplicates after reassembly release.</summary>
template<std::size_t TCapacity>
class RadioRecentDeliveryWindow final {
    static_assert(TCapacity > 0);
    struct Entry final { IRadio* Provider=nullptr; RadioAddress Source{}; RadioTransferId TransferId=0; };
    std::array<Entry,TCapacity> _entries{};
    std::size_t _cursor{0};
public:
    bool Contains(IRadio& provider,const RadioAddress& source,RadioTransferId id) const noexcept {
        for(const auto& entry:_entries)
            if(entry.Provider==&provider && entry.TransferId==id && entry.Source==source) return true;
        return false;
    }
    void Remember(IRadio& provider,const RadioAddress& source,RadioTransferId id) noexcept {
        if(id==0) return;
        _entries[_cursor]={&provider,source,id};
        _cursor=(_cursor+1u)%_entries.size();
    }
};

/// <summary>
/// Fixed reassembly table whose first fragment acquires complete record+logical-byte ownership before retaining bytes.
/// </summary>
/// <remarks>
/// The table never embeds a maximum logical payload array. Trusted transfers consume only their class-private or shared
/// Q1 domain; untrusted transfers consume only UntrustedIngress until explicit non-blocking promotion succeeds. Immutable
/// metadata disagreement or conflicting duplicate fragments releases the malformed transfer immediately.
/// </remarks>
template<class TInboundCapacity,std::size_t TMaximumActive,std::size_t TRecentCapacity>
class RadioReassemblyTable final {
    static_assert(TMaximumActive>0 && TRecentCapacity>0);
    TInboundCapacity* _capacity{nullptr};
    std::array<RadioCapacityRecordLease,TMaximumActive> _active{};
    RadioRecentDeliveryWindow<TRecentCapacity> _recent{};
    System::Synchronization::Mutex _mutex;

    static bool AddMilliseconds(std::uint64_t now,std::uint32_t milliseconds,std::uint64_t& expiry) noexcept {
        if(milliseconds==0) return false;
        const auto delta=static_cast<std::uint64_t>(milliseconds)*1'000'000ULL;
        if(now>std::numeric_limits<std::uint64_t>::max()-delta) return false;
        expiry=now+delta;
        return true;
    }

    std::size_t Find(IRadio& provider,const RadioAddress& source,RadioTransferId id) noexcept {
        for(std::size_t i=0;i<_active.size();++i){
            if(!_active[i]) continue;
            const auto& record=_active[i].template Get<RadioReassemblyRecord>();
            if(record.Provider==&provider && record.TransferId==id && record.Source==source) return i;
        }
        return _active.size();
    }
    std::size_t FreeSlot() const noexcept {
        for(std::size_t i=0;i<_active.size();++i) if(!_active[i]) return i;
        return _active.size();
    }

    static bool ValidateGeometry(
        IRadio& provider,
        const RadioTransportV3FragmentView& fragment,
        std::size_t& offset,
        std::size_t& expectedBytes) noexcept {
        const auto chunk=RadioTransportV3MaximumFragmentPayload(
            provider.Capabilities().MaximumPayloadBytes,fragment.Header.Source.Length);
        if(chunk==0) return false;
        const auto logical=static_cast<std::size_t>(fragment.Header.LogicalPayloadBytes);
        const auto expectedCount=(logical+chunk-1u)/chunk;
        if(expectedCount==0 || expectedCount>255u || expectedCount!=fragment.Header.FragmentCount) return false;
        offset=static_cast<std::size_t>(fragment.Header.FragmentIndex)*chunk;
        if(offset>=logical) return false;
        const auto remaining=logical-offset;
        expectedBytes=remaining<chunk?remaining:chunk;
        return fragment.FragmentPayloadBytes==expectedBytes;
    }

    RadioReassemblyStatus AcceptInto(
        RadioReassemblyRecord& record,
        const RadioTransportV3FragmentView& fragment,
        std::uint64_t now) noexcept {
        if(record.IsExpired(now)) return RadioReassemblyStatus::Expired;
        if(record.TransferId!=fragment.Header.TransferId || record.Source!=fragment.Header.Source ||
           record.FragmentCount!=fragment.Header.FragmentCount ||
           record.LogicalPayloadBytes!=fragment.Header.LogicalPayloadBytes ||
           record.Service!=fragment.Header.ServiceClass) return RadioReassemblyStatus::Malformed;
        std::uint64_t candidateExpiry=0;
        if(!AddMilliseconds(now,fragment.Header.RemainingResidenceMilliseconds,candidateExpiry))
            return RadioReassemblyStatus::Expired;
        if(candidateExpiry<record.ExpiryNanoseconds) record.ExpiryNanoseconds=candidateExpiry;
        std::size_t offset=0,expectedBytes=0;
        if(!ValidateGeometry(*record.Provider,fragment,offset,expectedBytes)) return RadioReassemblyStatus::Malformed;
        if(record.HasFragment(fragment.Header.FragmentIndex)){
            if(expectedBytes!=0 && std::memcmp(record.Fill+offset,fragment.FragmentPayload,expectedBytes)!=0)
                return RadioReassemblyStatus::Malformed;
            return record.IsComplete()?RadioReassemblyStatus::Complete:RadioReassemblyStatus::Duplicate;
        }
        if(expectedBytes!=0) std::memcpy(record.Fill+offset,fragment.FragmentPayload,expectedBytes);
        record.MarkFragment(fragment.Header.FragmentIndex);
        return record.IsComplete()?RadioReassemblyStatus::Complete:RadioReassemblyStatus::Accepted;
    }

    RadioReassemblyStatus Start(
        std::size_t slot,
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioTransportV3FragmentView& fragment,
        std::uint64_t now,
        bool trusted) noexcept {
        std::uint64_t expiry=0;
        if(!AddMilliseconds(now,fragment.Header.RemainingResidenceMilliseconds,expiry))
            return RadioReassemblyStatus::Expired;
        if(fragment.Header.LogicalPayloadBytes>
           RadioTransportV3MaximumLogicalPayload(provider.Capabilities().MaximumPayloadBytes,
                                                  fragment.Header.Source.Length,
                                                  provider.Capabilities().MaximumLogicalTransferBytes))
            return RadioReassemblyStatus::Malformed;
        std::size_t offset=0,expectedBytes=0;
        if(!ValidateGeometry(provider,fragment,offset,expectedBytes)) return RadioReassemblyStatus::Malformed;

        RadioCapacityReservation reservation;
        const auto reserved=trusted
            ?_capacity->TryAcquireTrusted(fragment.Header.ServiceClass,fragment.Header.LogicalPayloadBytes,reservation)
            :_capacity->TryAcquireUntrusted(fragment.Header.LogicalPayloadBytes,reservation);
        if(reserved==RadioResourceStatus::Busy) return RadioReassemblyStatus::Busy;
        if(reserved!=RadioResourceStatus::Success) return RadioReassemblyStatus::ResourceUnavailable;
        auto mutableBytes=reservation.Bytes().MutableView();
        if(!mutableBytes || mutableBytes.Capacity<fragment.Header.LogicalPayloadBytes)
            return RadioReassemblyStatus::ResourceUnavailable;
        auto* fill=mutableBytes.Data;
        if(reservation.Bytes().Commit(fragment.Header.LogicalPayloadBytes)!=RadioResourceStatus::Success)
            return RadioReassemblyStatus::ResourceUnavailable;
        RadioCapacityRecordLease lease;
        const auto built=_capacity->template Construct<RadioReassemblyRecord>(
            std::move(reservation),lease,fill,&provider,fragment.Header.Source,packet.Destination,
            fragment.Header.TransferId,fragment.Header.ServiceClass,fragment.Header.FragmentCount,
            fragment.Header.LogicalPayloadBytes,expiry,packet.Flags);
        if(built!=RadioResourceStatus::Success) return RadioReassemblyStatus::ResourceUnavailable;
        _active[slot]=std::move(lease);
        return AcceptInto(_active[slot].template Get<RadioReassemblyRecord>(),fragment,now);
    }

public:
    explicit RadioReassemblyTable(TInboundCapacity& capacity) noexcept:_capacity(&capacity) {}
    RadioReassemblyTable(const RadioReassemblyTable&)=delete;
    RadioReassemblyTable& operator=(const RadioReassemblyTable&)=delete;

    RadioReassemblyStatus Accept(
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioTransportV3FragmentView& fragment,
        std::uint64_t now,
        bool trusted) noexcept {
        if(!packet.Destination.IsValid() || !fragment.Header.Source.IsValid() ||
           (packet.Source.IsValid() && packet.Source!=fragment.Header.Source)) return RadioReassemblyStatus::Malformed;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return RadioReassemblyStatus::Busy;
        if(_recent.Contains(provider,fragment.Header.Source,fragment.Header.TransferId))
            return RadioReassemblyStatus::RecentlyCompleted;
        const auto existing=Find(provider,fragment.Header.Source,fragment.Header.TransferId);
        if(existing!=_active.size()){
            auto& record=_active[existing].template Get<RadioReassemblyRecord>();
            const auto status=AcceptInto(record,fragment,now);
            if(status==RadioReassemblyStatus::Malformed || status==RadioReassemblyStatus::Expired)
                _active[existing].Reset();
            return status;
        }
        const auto free=FreeSlot();
        if(free==_active.size()) return RadioReassemblyStatus::ResourceUnavailable;
        return Start(free,provider,packet,fragment,now,trusted);
    }

    RadioReassemblyStatus PromoteCompleted(
        IRadio& provider,
        const RadioAddress& source,
        RadioTransferId transferId,
        RadioServiceClass validatedService) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return RadioReassemblyStatus::Busy;
        const auto index=Find(provider,source,transferId);
        if(index==_active.size()) return RadioReassemblyStatus::NotFound;
        auto& existing=_active[index].template Get<RadioReassemblyRecord>();
        if(!existing.IsComplete()) return RadioReassemblyStatus::NotFound;
        if(existing.Service!=validatedService) return RadioReassemblyStatus::Malformed;
        if(_active[index].Domain()!=RadioCapacityDomainKind::UntrustedIngress)
            return RadioReassemblyStatus::Complete;

        RadioCapacityReservation reservation;
        const auto reserved=_capacity->TryAcquireTrusted(validatedService,existing.LogicalPayloadBytes,reservation);
        if(reserved==RadioResourceStatus::Busy) return RadioReassemblyStatus::Busy;
        if(reserved!=RadioResourceStatus::Success) return RadioReassemblyStatus::ResourceUnavailable;
        auto mutableBytes=reservation.Bytes().MutableView();
        if(!mutableBytes || mutableBytes.Capacity<existing.LogicalPayloadBytes)
            return RadioReassemblyStatus::ResourceUnavailable;
        auto* fill=mutableBytes.Data;
        const auto oldView=existing.Bytes.View();
        if(oldView.Size!=existing.LogicalPayloadBytes) return RadioReassemblyStatus::Malformed;
        if(oldView.Size!=0) std::memcpy(fill,oldView.Data,oldView.Size);
        if(reservation.Bytes().Commit(oldView.Size)!=RadioResourceStatus::Success)
            return RadioReassemblyStatus::ResourceUnavailable;
        RadioCapacityRecordLease promoted;
        const auto built=_capacity->template Construct<RadioReassemblyRecord>(
            std::move(reservation),promoted,fill,existing);
        if(built!=RadioResourceStatus::Success) return RadioReassemblyStatus::ResourceUnavailable;
        _active[index]=std::move(promoted);
        return RadioReassemblyStatus::Complete;
    }

    RadioReassemblyStatus TakeCompleteTrusted(
        IRadio& provider,
        const RadioAddress& source,
        RadioTransferId transferId,
        RadioCompletedReassembly& output) noexcept {
        if(output) return RadioReassemblyStatus::ResourceUnavailable;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return RadioReassemblyStatus::Busy;
        const auto index=Find(provider,source,transferId);
        if(index==_active.size()) return RadioReassemblyStatus::NotFound;
        auto& record=_active[index].template Get<RadioReassemblyRecord>();
        if(!record.IsComplete()) return RadioReassemblyStatus::NotFound;
        if(_active[index].Domain()==RadioCapacityDomainKind::UntrustedIngress)
            return RadioReassemblyStatus::NotTrusted;
        _recent.Remember(provider,source,transferId);
        output._lease=std::move(_active[index]);
        return RadioReassemblyStatus::Complete;
    }

    std::size_t Expire(std::uint64_t now) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        std::size_t released=0;
        for(auto& lease:_active){
            if(!lease) continue;
            if(lease.template Get<RadioReassemblyRecord>().IsExpired(now)){
                lease.Reset();
                ++released;
            }
        }
        return released;
    }
};

} // namespace ESPressio::Radio
