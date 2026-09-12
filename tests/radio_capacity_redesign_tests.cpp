#include <ESPressio_RadioCapacity.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

using namespace ESPressio::Radio;

using Arena = RadioStaticByteArena<RadioByteClass<16,1>, RadioByteClass<64,1>>;
using Domain = RadioStaticCapacityDomain<192,1,Arena>;
using Inbound = RadioCapacityPlane<RadioCapacityDirection::Inbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound = RadioCapacityPlane<RadioCapacityDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;

static_assert(!std::is_copy_constructible_v<RadioByteLease>);
static_assert(!std::is_copy_constructible_v<RadioCapacityRecordLease>);

struct Record final {
    RadioByteLease Bytes;
    std::uint32_t Value;
    Record(RadioByteLease&& bytes, std::uint32_t value) noexcept
        : Bytes(std::move(bytes)), Value(value) {}
};

static unsigned WakeCount = 0;
static void Wake(void*) noexcept { ++WakeCount; }

int main() {
    constexpr std::array<RadioProtectedCapacityRequirement,2> good{{
        {RadioCapacityDirection::Inbound,RadioServiceClass::Critical,1,60},
        {RadioCapacityDirection::Inbound,RadioServiceClass::Critical,1,12}
    }};
    constexpr std::array<RadioByteClassShape,2> shapes{{{16,1},{64,1}}};
    static_assert(bool(ValidateRadioCapacityFit(shapes,2,good)));
    constexpr std::array<RadioProtectedCapacityRequirement,2> bad{{
        {RadioCapacityDirection::Inbound,RadioServiceClass::Critical,1,60},
        {RadioCapacityDirection::Inbound,RadioServiceClass::Critical,1,60}
    }};
    static_assert(ValidateRadioCapacityFit(shapes,2,bad).Status == RadioCapacityFitStatus::InsufficientByteSlots);

    Inbound inbound;
    inbound.Initialize({nullptr,&Wake});

    RadioCapacityReservation critical;
    assert(inbound.TryAcquireTrusted(RadioServiceClass::Critical,12,critical) == RadioResourceStatus::Success);
    assert(critical.Domain() == RadioCapacityDomainKind::CriticalPrivate);
    auto writable = critical.Bytes().MutableView();
    assert(writable && writable.Capacity == 16);
    writable.Data[0] = 0x5A;
    assert(critical.Bytes().Commit(1) == RadioResourceStatus::Success);
    RadioCapacityRecordLease first;
    assert(inbound.Construct<Record>(std::move(critical),first,7) == RadioResourceStatus::Success);
    assert(first.Domain() == RadioCapacityDomainKind::CriticalPrivate);
    assert(first.Get<Record>().Value == 7);
    assert(first.Get<Record>().Bytes.View().Data[0] == 0x5A);

    RadioCapacityReservation overflow;
    assert(inbound.TryAcquireTrusted(RadioServiceClass::Critical,12,overflow) == RadioResourceStatus::Success);
    assert(overflow.Domain() == RadioCapacityDomainKind::SharedOverflow);

    RadioCapacityReservation responsive;
    assert(inbound.TryAcquireTrusted(RadioServiceClass::Responsive,12,responsive) == RadioResourceStatus::Success);
    assert(responsive.Domain() == RadioCapacityDomainKind::ResponsivePrivate);

    RadioCapacityReservation quarantine;
    assert(inbound.TryAcquireUntrusted(20,quarantine) == RadioResourceStatus::Success);
    assert(quarantine.Domain() == RadioCapacityDomainKind::UntrustedIngress);

    RadioCapacityReservation protectedStillAvailable;
    assert(inbound.TryAcquireTrusted(RadioServiceClass::Infrastructure,12,protectedStillAvailable) == RadioResourceStatus::Success);
    assert(protectedStillAvailable.Domain() == RadioCapacityDomainKind::InfrastructurePrivate);

    const auto generation = inbound.Generation();
    const auto wakes = WakeCount;
    first.Reset();
    assert(inbound.Generation() == generation + 1);
    assert(WakeCount == wakes + 1);

    overflow.Reset();
    responsive.Reset();
    quarantine.Reset();
    protectedStillAvailable.Reset();

    Outbound outbound;
    outbound.Initialize();
    RadioCapacityReservation invalid;
    assert(outbound.TryAcquireUntrusted(1,invalid) == RadioResourceStatus::InvalidConfiguration);

    return 0;
}
