#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#include <ESPressio_Synchronization.hpp>

#include "ESPressio_RadioServiceProfile.hpp"

namespace ESPressio::Radio {

enum class RadioCapacityDirection : std::uint8_t { Inbound = 0, Outbound = 1 };

enum class RadioCapacityDomainKind : std::uint8_t {
    InfrastructurePrivate = 0,
    ClockPrivate = 1,
    CriticalPrivate = 2,
    ResponsivePrivate = 3,
    ConvergentPrivate = 4,
    BestEffortPrivate = 5,
    SharedOverflow = 6,
    UntrustedIngress = 7
};

enum class RadioResourceStatus : std::uint8_t {
    Success = 0,
    Busy,
    Exhausted,
    TooLarge,
    InvalidLease,
    InvalidLength,
    AlreadyCommitted,
    GenerationExhausted,
    InvalidConfiguration
};

struct RadioCapacityWakeTarget final {
    void* Context{nullptr};
    void (*Wake)(void*) noexcept{nullptr};
};

struct RadioByteClassShape final {
    std::size_t SlotBytes{0};
    std::size_t SlotCount{0};
};

struct RadioProtectedCapacityRequirement final {
    RadioCapacityDirection Direction{RadioCapacityDirection::Inbound};
    RadioServiceClass Service{RadioServiceClass::Invalid};
    std::size_t ConcurrentRecords{0};
    std::size_t MaximumOwnedBytes{0};
};

enum class RadioCapacityFitStatus : std::uint8_t {
    Success = 0,
    InvalidProfile,
    InsufficientRecords,
    InsufficientByteSlots
};

struct RadioCapacityFitResult final {
    RadioCapacityFitStatus Status{RadioCapacityFitStatus::InvalidProfile};
    std::size_t RequiredRecords{0};
    constexpr explicit operator bool() const noexcept { return Status == RadioCapacityFitStatus::Success; }
};

/// <summary>Deterministic largest-demand-first proof against one fixed record/byte domain.</summary>
template<std::size_t TShapeCount, std::size_t TRequirementCount>
constexpr RadioCapacityFitResult ValidateRadioCapacityFit(
    const std::array<RadioByteClassShape, TShapeCount>& shapes,
    std::size_t recordCount,
    const std::array<RadioProtectedCapacityRequirement, TRequirementCount>& requirements,
    std::size_t requirementCount = TRequirementCount) noexcept {
    if (requirementCount > TRequirementCount || recordCount == 0 || TShapeCount == 0)
        return {RadioCapacityFitStatus::InvalidProfile, 0};

    std::array<std::size_t, TShapeCount> available{};
    for (std::size_t i = 0; i < TShapeCount; ++i) {
        if (shapes[i].SlotBytes == 0 || shapes[i].SlotCount == 0 ||
            (i != 0 && shapes[i - 1].SlotBytes >= shapes[i].SlotBytes))
            return {RadioCapacityFitStatus::InvalidProfile, 0};
        available[i] = shapes[i].SlotCount;
    }

    std::array<std::size_t, TRequirementCount> remaining{};
    std::size_t requiredRecords = 0;
    for (std::size_t i = 0; i < requirementCount; ++i) {
        const auto& requirement = requirements[i];
        if (!IsValidRadioServiceClass(requirement.Service) || requirement.ConcurrentRecords == 0 ||
            requirement.MaximumOwnedBytes == 0)
            return {RadioCapacityFitStatus::InvalidProfile, 0};
        if (requiredRecords > std::numeric_limits<std::size_t>::max() - requirement.ConcurrentRecords)
            return {RadioCapacityFitStatus::InvalidProfile, 0};
        requiredRecords += requirement.ConcurrentRecords;
        remaining[i] = requirement.ConcurrentRecords;
    }
    if (requiredRecords > recordCount)
        return {RadioCapacityFitStatus::InsufficientRecords, requiredRecords};

    for (std::size_t placed = 0; placed < requiredRecords; ++placed) {
        std::size_t selected = requirementCount;
        std::size_t largest = 0;
        for (std::size_t i = 0; i < requirementCount; ++i) {
            if (remaining[i] != 0 && requirements[i].MaximumOwnedBytes > largest) {
                largest = requirements[i].MaximumOwnedBytes;
                selected = i;
            }
        }
        if (selected == requirementCount) return {RadioCapacityFitStatus::InvalidProfile, requiredRecords};
        bool fitted = false;
        for (std::size_t shape = 0; shape < TShapeCount; ++shape) {
            if (available[shape] != 0 && shapes[shape].SlotBytes >= largest) {
                --available[shape];
                --remaining[selected];
                fitted = true;
                break;
            }
        }
        if (!fitted) return {RadioCapacityFitStatus::InsufficientByteSlots, requiredRecords};
    }
    return {RadioCapacityFitStatus::Success, requiredRecords};
}

template<std::size_t TSlotBytes, std::size_t TSlotCount>
struct RadioByteClass final {
    static_assert(TSlotBytes > 0 && TSlotCount > 0, "Radio byte class must be finite and non-zero");
    static constexpr std::size_t SlotBytes = TSlotBytes;
    static constexpr std::size_t SlotCount = TSlotCount;
};

struct RadioByteLeaseIdentity final {
    std::uint16_t ClassIndex{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t SlotIndex{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return ClassIndex != std::numeric_limits<std::uint16_t>::max() &&
               SlotIndex != std::numeric_limits<std::uint16_t>::max() && Generation != 0;
    }
};

struct RadioByteView final {
    const std::uint8_t* Data{nullptr};
    std::size_t Size{0};
    constexpr explicit operator bool() const noexcept { return Data != nullptr || Size == 0; }
};

struct RadioMutableByteView final {
    std::uint8_t* Data{nullptr};
    std::size_t Capacity{0};
    constexpr explicit operator bool() const noexcept { return Data != nullptr; }
};

class RadioByteLease final {
    void* _owner{nullptr};
    bool (*_release)(void*, RadioByteLeaseIdentity) noexcept{nullptr};
    std::uint8_t* _data{nullptr};
    std::size_t _capacity{0};
    std::size_t _length{0};
    RadioByteLeaseIdentity _identity{};
    bool _sealed{false};

    template<class...> friend class RadioStaticByteArena;
    RadioByteLease(
        void* owner,
        bool (*release)(void*, RadioByteLeaseIdentity) noexcept,
        std::uint8_t* data,
        std::size_t capacity,
        RadioByteLeaseIdentity identity) noexcept
        : _owner(owner), _release(release), _data(data), _capacity(capacity), _identity(identity) {}

public:
    RadioByteLease() noexcept = default;
    RadioByteLease(const RadioByteLease&) = delete;
    RadioByteLease& operator=(const RadioByteLease&) = delete;
    RadioByteLease(RadioByteLease&& other) noexcept { *this = std::move(other); }
    RadioByteLease& operator=(RadioByteLease&& other) noexcept {
        if (this == &other) return *this;
        Reset();
        _owner = std::exchange(other._owner, nullptr);
        _release = std::exchange(other._release, nullptr);
        _data = std::exchange(other._data, nullptr);
        _capacity = std::exchange(other._capacity, 0);
        _length = std::exchange(other._length, 0);
        _identity = std::exchange(other._identity, {});
        _sealed = std::exchange(other._sealed, false);
        return *this;
    }
    ~RadioByteLease() { Reset(); }

    explicit operator bool() const noexcept { return _owner && _release && bool(_identity); }
    std::size_t Capacity() const noexcept { return _capacity; }
    std::size_t Length() const noexcept { return _length; }
    bool IsCommitted() const noexcept { return _sealed; }
    RadioByteLeaseIdentity Identity() const noexcept { return _identity; }
    RadioMutableByteView MutableView() noexcept {
        return (!_sealed && *this) ? RadioMutableByteView{_data, _capacity} : RadioMutableByteView{};
    }
    RadioByteView View() const noexcept {
        return (_sealed && *this) ? RadioByteView{_data, _length} : RadioByteView{};
    }
    RadioResourceStatus Commit(std::size_t actualLength) noexcept {
        if (!*this) return RadioResourceStatus::InvalidLease;
        if (_sealed) return RadioResourceStatus::AlreadyCommitted;
        if (actualLength > _capacity) return RadioResourceStatus::InvalidLength;
        _length = actualLength;
        _sealed = true;
        return RadioResourceStatus::Success;
    }
    bool Reset() noexcept {
        if (!_owner || !_release || !_identity) {
            _owner = nullptr; _release = nullptr; _data = nullptr; _capacity = 0; _length = 0; _identity = {}; _sealed = false;
            return false;
        }
        auto* owner = _owner;
        auto release = _release;
        auto identity = _identity;
        _owner = nullptr; _release = nullptr; _data = nullptr; _capacity = 0; _length = 0; _identity = {}; _sealed = false;
        return release(owner, identity);
    }
};

namespace Detail {
template<class TClass>
class RadioByteClassStorage final {
    struct Slot final {
        std::array<std::uint8_t, TClass::SlotBytes> Bytes{};
        std::uint64_t Generation{0};
        bool Occupied{false};
    };
    std::array<Slot, TClass::SlotCount> _slots{};
    System::Synchronization::Mutex _mutex;
public:
    void Initialize() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex); }
    RadioResourceStatus TryAcquire(
        std::uint16_t classIndex,
        RadioByteLeaseIdentity& identity,
        std::uint8_t*& data) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return RadioResourceStatus::Busy;
        bool generationBlocked = false;
        for (std::size_t i = 0; i < _slots.size(); ++i) {
            auto& slot = _slots[i];
            if (slot.Occupied) continue;
            if (slot.Generation == std::numeric_limits<std::uint64_t>::max()) {
                generationBlocked = true;
                continue;
            }
            ++slot.Generation;
            slot.Occupied = true;
            identity = {classIndex, static_cast<std::uint16_t>(i), slot.Generation};
            data = slot.Bytes.data();
            return RadioResourceStatus::Success;
        }
        return generationBlocked ? RadioResourceStatus::GenerationExhausted : RadioResourceStatus::Exhausted;
    }
    bool Release(std::uint16_t index, std::uint64_t generation) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if (index >= _slots.size()) return false;
        auto& slot = _slots[index];
        if (!slot.Occupied || slot.Generation != generation) return false;
        slot.Occupied = false;
        return true;
    }
};

template<class First, class... Rest>
struct RadioStrictAscendingClasses
    : std::bool_constant<((First::SlotBytes < Rest::SlotBytes) && ...) &&
                         RadioStrictAscendingClasses<Rest...>::value> {};
template<class Last>
struct RadioStrictAscendingClasses<Last> : std::true_type {};
}

template<class... TClasses>
class RadioStaticByteArena final {
    static_assert(sizeof...(TClasses) > 0, "Radio byte arena requires at least one size class");
    static_assert(Detail::RadioStrictAscendingClasses<TClasses...>::value,
                  "Radio byte classes must be strictly ascending by slot size");
    std::tuple<Detail::RadioByteClassStorage<TClasses>...> _classes{};

    static bool ReleaseThunk(void* owner, RadioByteLeaseIdentity identity) noexcept {
        return static_cast<RadioStaticByteArena*>(owner)->Release(identity);
    }
    template<std::size_t Index = 0>
    bool ReleaseAt(RadioByteLeaseIdentity identity) noexcept {
        if constexpr (Index == sizeof...(TClasses)) return false;
        else if (Index == identity.ClassIndex)
            return std::get<Index>(_classes).Release(identity.SlotIndex, identity.Generation);
        else return ReleaseAt<Index + 1>(identity);
    }
    template<std::size_t Index = 0>
    RadioResourceStatus TryAcquireAt(
        std::size_t requested,
        RadioByteLease& output,
        RadioResourceStatus previous = RadioResourceStatus::Exhausted) noexcept {
        if constexpr (Index == sizeof...(TClasses)) return previous;
        else {
            using C = std::tuple_element_t<Index, std::tuple<TClasses...>>;
            if (requested > C::SlotBytes) return TryAcquireAt<Index + 1>(requested, output, previous);
            RadioByteLeaseIdentity identity{};
            std::uint8_t* data = nullptr;
            const auto status = std::get<Index>(_classes).TryAcquire(
                static_cast<std::uint16_t>(Index), identity, data);
            if (status == RadioResourceStatus::Success) {
                output = RadioByteLease(this, &ReleaseThunk, data, C::SlotBytes, identity);
                return status;
            }
            if (status == RadioResourceStatus::Busy) return status;
            if (status == RadioResourceStatus::GenerationExhausted) previous = status;
            return TryAcquireAt<Index + 1>(requested, output, previous);
        }
    }
public:
    RadioStaticByteArena() = default;
    RadioStaticByteArena(const RadioStaticByteArena&) = delete;
    RadioStaticByteArena& operator=(const RadioStaticByteArena&) = delete;
    void Initialize() noexcept {
        std::apply([](auto&... value) { (value.Initialize(), ...); }, _classes);
    }
    static constexpr std::size_t ClassCount = sizeof...(TClasses);
    static constexpr std::size_t LargestSlotBytes() noexcept {
        return std::tuple_element_t<sizeof...(TClasses) - 1, std::tuple<TClasses...>>::SlotBytes;
    }
    static constexpr std::array<RadioByteClassShape, ClassCount> Shapes() noexcept {
        return {{{TClasses::SlotBytes, TClasses::SlotCount}...}};
    }
    RadioResourceStatus TryAcquire(std::size_t requested, RadioByteLease& output) noexcept {
        if (output) return RadioResourceStatus::InvalidLease;
        if (requested > LargestSlotBytes()) return RadioResourceStatus::TooLarge;
        return TryAcquireAt(requested, output);
    }
    bool Release(RadioByteLeaseIdentity identity) noexcept {
        if (!identity || identity.ClassIndex >= sizeof...(TClasses)) return false;
        return ReleaseAt(identity);
    }
};

struct RadioCapacityRecordIdentity final {
    RadioCapacityDirection Direction{RadioCapacityDirection::Inbound};
    RadioCapacityDomainKind Domain{RadioCapacityDomainKind::InfrastructurePrivate};
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t Generation{0};
    constexpr explicit operator bool() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0;
    }
};

class RadioCapacityRecordLease final {
    void* _owner{nullptr};
    void (*_destroyRelease)(void*, std::byte*, std::uint16_t, std::uint64_t) noexcept{nullptr};
    std::byte* _storage{nullptr};
    std::size_t _recordBytes{0};
    std::uint16_t _slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t _generation{0};
    RadioCapacityDirection _direction{RadioCapacityDirection::Inbound};
    RadioCapacityDomainKind _domain{RadioCapacityDomainKind::InfrastructurePrivate};

    template<std::size_t, std::size_t, class> friend class RadioStaticCapacityDomain;
    RadioCapacityRecordLease(
        void* owner,
        void (*destroyRelease)(void*, std::byte*, std::uint16_t, std::uint64_t) noexcept,
        std::byte* storage,
        std::size_t recordBytes,
        std::uint16_t slot,
        std::uint64_t generation,
        RadioCapacityDirection direction,
        RadioCapacityDomainKind domain) noexcept
        : _owner(owner), _destroyRelease(destroyRelease), _storage(storage), _recordBytes(recordBytes),
          _slot(slot), _generation(generation), _direction(direction), _domain(domain) {}
public:
    RadioCapacityRecordLease() noexcept = default;
    RadioCapacityRecordLease(const RadioCapacityRecordLease&) = delete;
    RadioCapacityRecordLease& operator=(const RadioCapacityRecordLease&) = delete;
    RadioCapacityRecordLease(RadioCapacityRecordLease&& other) noexcept { *this = std::move(other); }
    RadioCapacityRecordLease& operator=(RadioCapacityRecordLease&& other) noexcept {
        if (this == &other) return *this;
        Reset();
        _owner = std::exchange(other._owner, nullptr);
        _destroyRelease = std::exchange(other._destroyRelease, nullptr);
        _storage = std::exchange(other._storage, nullptr);
        _recordBytes = std::exchange(other._recordBytes, 0);
        _slot = std::exchange(other._slot, std::numeric_limits<std::uint16_t>::max());
        _generation = std::exchange(other._generation, 0);
        _direction = other._direction;
        _domain = other._domain;
        return *this;
    }
    ~RadioCapacityRecordLease() { Reset(); }
    explicit operator bool() const noexcept { return _owner && _destroyRelease && _storage && _generation != 0; }
    template<class T> T& Get() noexcept { return *std::launder(reinterpret_cast<T*>(_storage)); }
    template<class T> const T& Get() const noexcept { return *std::launder(reinterpret_cast<const T*>(_storage)); }
    RadioCapacityRecordIdentity Identity() const noexcept { return {_direction, _domain, _slot, _generation}; }
    RadioCapacityDomainKind Domain() const noexcept { return _domain; }
    std::size_t RecordBytes() const noexcept { return _recordBytes; }
    void Reset() noexcept {
        if (!*this) {
            _owner = nullptr; _destroyRelease = nullptr; _storage = nullptr; _recordBytes = 0;
            _slot = std::numeric_limits<std::uint16_t>::max(); _generation = 0;
            return;
        }
        auto* owner = _owner;
        auto destroyRelease = _destroyRelease;
        auto* storage = _storage;
        const auto slot = _slot;
        const auto generation = _generation;
        _owner = nullptr; _destroyRelease = nullptr; _storage = nullptr; _recordBytes = 0;
        _slot = std::numeric_limits<std::uint16_t>::max(); _generation = 0;
        destroyRelease(owner, storage, slot, generation);
    }
};

class RadioCapacityReservation final {
    void* _owner{nullptr};
    void (*_rollback)(void*, std::uint16_t, std::uint64_t) noexcept{nullptr};
    std::byte* _storage{nullptr};
    std::size_t _recordBytes{0};
    std::uint16_t _slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint64_t _generation{0};
    RadioByteLease _bytes{};
    RadioCapacityDirection _direction{RadioCapacityDirection::Inbound};
    RadioCapacityDomainKind _domain{RadioCapacityDomainKind::InfrastructurePrivate};

    template<std::size_t, std::size_t, class> friend class RadioStaticCapacityDomain;
    RadioCapacityReservation(
        void* owner,
        void (*rollback)(void*, std::uint16_t, std::uint64_t) noexcept,
        std::byte* storage,
        std::size_t recordBytes,
        std::uint16_t slot,
        std::uint64_t generation,
        RadioByteLease&& bytes,
        RadioCapacityDirection direction,
        RadioCapacityDomainKind domain) noexcept
        : _owner(owner), _rollback(rollback), _storage(storage), _recordBytes(recordBytes), _slot(slot),
          _generation(generation), _bytes(std::move(bytes)), _direction(direction), _domain(domain) {}
public:
    RadioCapacityReservation() noexcept = default;
    RadioCapacityReservation(const RadioCapacityReservation&) = delete;
    RadioCapacityReservation& operator=(const RadioCapacityReservation&) = delete;
    RadioCapacityReservation(RadioCapacityReservation&& other) noexcept { *this = std::move(other); }
    RadioCapacityReservation& operator=(RadioCapacityReservation&& other) noexcept {
        if (this == &other) return *this;
        Reset();
        _owner = std::exchange(other._owner, nullptr);
        _rollback = std::exchange(other._rollback, nullptr);
        _storage = std::exchange(other._storage, nullptr);
        _recordBytes = std::exchange(other._recordBytes, 0);
        _slot = std::exchange(other._slot, std::numeric_limits<std::uint16_t>::max());
        _generation = std::exchange(other._generation, 0);
        _bytes = std::move(other._bytes);
        _direction = other._direction;
        _domain = other._domain;
        return *this;
    }
    ~RadioCapacityReservation() { Reset(); }
    explicit operator bool() const noexcept { return _owner && _rollback && _storage && _generation != 0 && bool(_bytes); }
    RadioByteLease& Bytes() noexcept { return _bytes; }
    const RadioByteLease& Bytes() const noexcept { return _bytes; }
    RadioCapacityDomainKind Domain() const noexcept { return _domain; }
    void Reset() noexcept {
        if (!*this) {
            _bytes.Reset(); _owner = nullptr; _rollback = nullptr; _storage = nullptr; _recordBytes = 0;
            _slot = std::numeric_limits<std::uint16_t>::max(); _generation = 0;
            return;
        }
        _bytes.Reset();
        auto* owner = _owner;
        auto rollback = _rollback;
        const auto slot = _slot;
        const auto generation = _generation;
        _owner = nullptr; _rollback = nullptr; _storage = nullptr; _recordBytes = 0;
        _slot = std::numeric_limits<std::uint16_t>::max(); _generation = 0;
        rollback(owner, slot, generation);
    }
};

struct RadioCapacityReleaseTarget final {
    void* Context{nullptr};
    void (*Release)(void*, RadioCapacityDomainKind) noexcept{nullptr};
};

template<std::size_t TRecordBytes, std::size_t TRecordCount, class TByteArena>
class RadioStaticCapacityDomain final {
    static_assert(TRecordBytes > 0 && TRecordCount > 0, "Radio capacity domain must be finite and non-zero");
    struct Slot final {
        alignas(std::max_align_t) std::array<std::byte, TRecordBytes> Storage{};
        std::uint64_t Generation{0};
        bool Occupied{false};
    };
    std::array<Slot, TRecordCount> _slots{};
    TByteArena _bytes{};
    System::Synchronization::Mutex _mutex;
    RadioCapacityDirection _direction{RadioCapacityDirection::Inbound};
    RadioCapacityDomainKind _kind{RadioCapacityDomainKind::InfrastructurePrivate};
    RadioCapacityReleaseTarget _releaseTarget{};

    static void RollbackThunk(void* owner, std::uint16_t slot, std::uint64_t generation) noexcept {
        static_cast<RadioStaticCapacityDomain*>(owner)->ReleaseSlot(slot, generation, true);
    }
    template<class TRecord>
    static void DestroyReleaseThunk(
        void* owner,
        std::byte* storage,
        std::uint16_t slot,
        std::uint64_t generation) noexcept {
        static_assert(std::is_nothrow_destructible_v<TRecord>, "Radio retained record destructor must be noexcept");
        std::launder(reinterpret_cast<TRecord*>(storage))->~TRecord();
        static_cast<RadioStaticCapacityDomain*>(owner)->ReleaseSlot(slot, generation, true);
    }
    bool ReleaseSlot(std::uint16_t slotIndex, std::uint64_t generation, bool notify) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if (slotIndex >= _slots.size()) return false;
            auto& slot = _slots[slotIndex];
            if (!slot.Occupied || slot.Generation != generation) return false;
            slot.Occupied = false;
        }
        if (notify && _releaseTarget.Release) _releaseTarget.Release(_releaseTarget.Context, _kind);
        return true;
    }
public:
    RadioStaticCapacityDomain() = default;
    RadioStaticCapacityDomain(const RadioStaticCapacityDomain&) = delete;
    RadioStaticCapacityDomain& operator=(const RadioStaticCapacityDomain&) = delete;
    void Initialize(
        RadioCapacityDirection direction,
        RadioCapacityDomainKind kind,
        RadioCapacityReleaseTarget releaseTarget = {}) noexcept {
        _direction = direction;
        _kind = kind;
        _releaseTarget = releaseTarget;
        { std::lock_guard<System::Synchronization::Mutex> lock(_mutex); }
        _bytes.Initialize();
    }
    RadioResourceStatus TryReserve(std::size_t bytes, RadioCapacityReservation& output) noexcept {
        if (output) return RadioResourceStatus::InvalidLease;
        std::uint16_t slotIndex = 0;
        std::uint64_t generation = 0;
        std::byte* storage = nullptr;
        {
            std::unique_lock<System::Synchronization::Mutex> lock(_mutex, std::try_to_lock);
            if (!lock.owns_lock()) return RadioResourceStatus::Busy;
            bool generationBlocked = false;
            bool found = false;
            for (std::size_t i = 0; i < _slots.size(); ++i) {
                auto& slot = _slots[i];
                if (slot.Occupied) continue;
                if (slot.Generation == std::numeric_limits<std::uint64_t>::max()) {
                    generationBlocked = true;
                    continue;
                }
                ++slot.Generation;
                slot.Occupied = true;
                slotIndex = static_cast<std::uint16_t>(i);
                generation = slot.Generation;
                storage = slot.Storage.data();
                found = true;
                break;
            }
            if (!found) return generationBlocked ? RadioResourceStatus::GenerationExhausted : RadioResourceStatus::Exhausted;
        }
        RadioByteLease bytesLease;
        const auto byteStatus = _bytes.TryAcquire(bytes, bytesLease);
        if (byteStatus != RadioResourceStatus::Success) {
            ReleaseSlot(slotIndex, generation, true);
            return byteStatus;
        }
        output = RadioCapacityReservation(
            this, &RollbackThunk, storage, TRecordBytes, slotIndex, generation, std::move(bytesLease), _direction, _kind);
        return RadioResourceStatus::Success;
    }
    template<class TRecord, class... Args>
    RadioResourceStatus Construct(
        RadioCapacityReservation&& reservation,
        RadioCapacityRecordLease& output,
        Args&&... args) noexcept {
        static_assert(sizeof(TRecord) <= TRecordBytes, "Radio retained record exceeds configured slot bytes");
        static_assert(alignof(TRecord) <= alignof(std::max_align_t), "Over-aligned Radio retained records are unsupported");
        static_assert(std::is_nothrow_constructible_v<TRecord, RadioByteLease&&, Args...>,
                      "Radio retained record construction must be noexcept");
        if (output || !reservation || reservation._owner != this) return RadioResourceStatus::InvalidLease;
        if (!reservation._bytes.IsCommitted()) return RadioResourceStatus::InvalidLength;
        auto* storage = reservation._storage;
        const auto slot = reservation._slot;
        const auto generation = reservation._generation;
        const auto direction = reservation._direction;
        const auto domain = reservation._domain;
        new (storage) TRecord(std::move(reservation._bytes), std::forward<Args>(args)...);
        reservation._owner = nullptr;
        reservation._rollback = nullptr;
        reservation._storage = nullptr;
        reservation._recordBytes = 0;
        reservation._slot = std::numeric_limits<std::uint16_t>::max();
        reservation._generation = 0;
        output = RadioCapacityRecordLease(
            this, &DestroyReleaseThunk<TRecord>, storage, TRecordBytes, slot, generation, direction, domain);
        return RadioResourceStatus::Success;
    }
    static constexpr std::size_t RecordCount() noexcept { return TRecordCount; }
    static constexpr std::size_t RecordBytes() noexcept { return TRecordBytes; }
    static constexpr std::size_t LargestSlotBytes() noexcept { return TByteArena::LargestSlotBytes(); }
    static constexpr auto ByteShapes() noexcept { return TByteArena::Shapes(); }
    template<std::size_t N>
    static constexpr RadioCapacityFitResult ValidateRequirements(
        const std::array<RadioProtectedCapacityRequirement, N>& requirements,
        std::size_t count = N) noexcept {
        return ValidateRadioCapacityFit(TByteArena::Shapes(), TRecordCount, requirements, count);
    }
};

namespace Detail {
template<RadioCapacityDirection TDirection, class TUntrusted>
struct RadioUntrustedHolder {};
template<class TUntrusted>
struct RadioUntrustedHolder<RadioCapacityDirection::Inbound, TUntrusted> { TUntrusted Domain{}; };
}

template<RadioCapacityDirection TDirection,
         class TInfrastructure, class TClock, class TCritical, class TResponsive,
         class TConvergent, class TBestEffort, class TShared, class TUntrusted = void>
class RadioCapacityPlane final : private Detail::RadioUntrustedHolder<TDirection, TUntrusted> {
    static_assert(TDirection == RadioCapacityDirection::Outbound || !std::is_void_v<TUntrusted>,
                  "Inbound Radio capacity requires an UntrustedIngress domain");
    using UntrustedHolder = Detail::RadioUntrustedHolder<TDirection, TUntrusted>;
    TInfrastructure _infrastructure{};
    TClock _clock{};
    TCritical _critical{};
    TResponsive _responsive{};
    TConvergent _convergent{};
    TBestEffort _bestEffort{};
    TShared _shared{};
    std::atomic<std::uint64_t> _generation{1};
    RadioCapacityWakeTarget _wake{};

    static RadioCapacityDomainKind PrivateKind(RadioServiceClass service) noexcept {
        switch (service) {
            case RadioServiceClass::Infrastructure: return RadioCapacityDomainKind::InfrastructurePrivate;
            case RadioServiceClass::Clock: return RadioCapacityDomainKind::ClockPrivate;
            case RadioServiceClass::Critical: return RadioCapacityDomainKind::CriticalPrivate;
            case RadioServiceClass::Responsive: return RadioCapacityDomainKind::ResponsivePrivate;
            case RadioServiceClass::Convergent: return RadioCapacityDomainKind::ConvergentPrivate;
            case RadioServiceClass::BestEffort: return RadioCapacityDomainKind::BestEffortPrivate;
            default: return RadioCapacityDomainKind::SharedOverflow;
        }
    }
    static void ReleasedThunk(void* context, RadioCapacityDomainKind) noexcept {
        static_cast<RadioCapacityPlane*>(context)->OnRelease();
    }
    void OnRelease() noexcept {
        auto current = _generation.load(std::memory_order_relaxed);
        while (current != std::numeric_limits<std::uint64_t>::max() &&
               !_generation.compare_exchange_weak(current, current + 1, std::memory_order_release, std::memory_order_relaxed)) {}
        if (_wake.Wake) _wake.Wake(_wake.Context);
    }
    RadioResourceStatus TryPrivate(
        RadioServiceClass service,
        std::size_t bytes,
        RadioCapacityReservation& output) noexcept {
        switch (service) {
            case RadioServiceClass::Infrastructure: return _infrastructure.TryReserve(bytes, output);
            case RadioServiceClass::Clock: return _clock.TryReserve(bytes, output);
            case RadioServiceClass::Critical: return _critical.TryReserve(bytes, output);
            case RadioServiceClass::Responsive: return _responsive.TryReserve(bytes, output);
            case RadioServiceClass::Convergent: return _convergent.TryReserve(bytes, output);
            case RadioServiceClass::BestEffort: return _bestEffort.TryReserve(bytes, output);
            default: return RadioResourceStatus::InvalidConfiguration;
        }
    }
public:
    RadioCapacityPlane() = default;
    RadioCapacityPlane(const RadioCapacityPlane&) = delete;
    RadioCapacityPlane& operator=(const RadioCapacityPlane&) = delete;
    void Initialize(RadioCapacityWakeTarget wake = {}) noexcept {
        _wake = wake;
        const RadioCapacityReleaseTarget release{this, &ReleasedThunk};
        _infrastructure.Initialize(TDirection, RadioCapacityDomainKind::InfrastructurePrivate, release);
        _clock.Initialize(TDirection, RadioCapacityDomainKind::ClockPrivate, release);
        _critical.Initialize(TDirection, RadioCapacityDomainKind::CriticalPrivate, release);
        _responsive.Initialize(TDirection, RadioCapacityDomainKind::ResponsivePrivate, release);
        _convergent.Initialize(TDirection, RadioCapacityDomainKind::ConvergentPrivate, release);
        _bestEffort.Initialize(TDirection, RadioCapacityDomainKind::BestEffortPrivate, release);
        _shared.Initialize(TDirection, RadioCapacityDomainKind::SharedOverflow, release);
        if constexpr (TDirection == RadioCapacityDirection::Inbound)
            static_cast<UntrustedHolder&>(*this).Domain.Initialize(
                TDirection, RadioCapacityDomainKind::UntrustedIngress, release);
    }
    std::uint64_t Generation() const noexcept { return _generation.load(std::memory_order_acquire); }
    RadioResourceStatus TryAcquireTrusted(
        RadioServiceClass service,
        std::size_t bytes,
        RadioCapacityReservation& output) noexcept {
        if (!IsValidRadioServiceClass(service)) return RadioResourceStatus::InvalidConfiguration;
        const auto privateStatus = TryPrivate(service, bytes, output);
        if (privateStatus == RadioResourceStatus::Success || privateStatus == RadioResourceStatus::Busy ||
            privateStatus == RadioResourceStatus::GenerationExhausted) return privateStatus;
        if (output) return RadioResourceStatus::InvalidLease;
        return _shared.TryReserve(bytes, output);
    }
    RadioResourceStatus TryAcquireUntrusted(std::size_t bytes, RadioCapacityReservation& output) noexcept {
        if constexpr (TDirection == RadioCapacityDirection::Inbound)
            return static_cast<UntrustedHolder&>(*this).Domain.TryReserve(bytes, output);
        (void)bytes; (void)output;
        return RadioResourceStatus::InvalidConfiguration;
    }
    template<class TRecord, class... Args>
    RadioResourceStatus Construct(
        RadioCapacityReservation&& reservation,
        RadioCapacityRecordLease& output,
        Args&&... args) noexcept {
        switch (reservation.Domain()) {
            case RadioCapacityDomainKind::InfrastructurePrivate:
                return _infrastructure.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::ClockPrivate:
                return _clock.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::CriticalPrivate:
                return _critical.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::ResponsivePrivate:
                return _responsive.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::ConvergentPrivate:
                return _convergent.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::BestEffortPrivate:
                return _bestEffort.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::SharedOverflow:
                return _shared.template Construct<TRecord>(std::move(reservation), output, std::forward<Args>(args)...);
            case RadioCapacityDomainKind::UntrustedIngress:
                if constexpr (TDirection == RadioCapacityDirection::Inbound)
                    return static_cast<UntrustedHolder&>(*this).Domain.template Construct<TRecord>(
                        std::move(reservation), output, std::forward<Args>(args)...);
                break;
        }
        return RadioResourceStatus::InvalidConfiguration;
    }
};

} // namespace ESPressio::Radio
