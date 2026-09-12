#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioCapacity.hpp"
#include "ESPressio_RadioReassembly.hpp"
#include "ESPressio_RadioScheduler.hpp"
#include "ESPressio_RadioServiceProfile.hpp"
#include "ESPressio_RadioTransferId.hpp"

namespace ESPressio::Radio {

/// <summary>Exact configured record/byte-slot reservation for one Q1 capacity domain.</summary>
struct RadioCapacityDomainResourceAccounting final {
    RadioCapacityDomainKind Domain{RadioCapacityDomainKind::InfrastructurePrivate};
    std::size_t RecordSlots{0};
    std::size_t ByteSlots{0};
    std::size_t BytePayloadBytes{0};
};

/// <summary>Configuration facts needed to decompose one concrete Radio deployment without guessing hidden resources.</summary>
struct RadioResourceAccountingConfiguration final {
    std::size_t ProviderRegistrySlots{0};
    std::size_t PeerRegistrySlots{0};
    std::array<RadioCapacityDomainResourceAccounting,8> InboundDomains{};
    std::array<RadioCapacityDomainResourceAccounting,8> OutboundDomains{};
    std::size_t MaximumActiveReassemblies{0};
    std::size_t RecentInboundTransferIds{0};
    std::size_t OutboundQueueDepthPerClass{0};
    std::size_t RecentOutboundTransferIds{0};
    std::size_t PhysicalEncodeScratchBytesPerDomain{0};
    std::size_t ProviderDeferredCompletionSlotsPerDomain{0};
    std::size_t DomainTaskStackBytes{0};
    std::size_t WakeSignalObjectBytes{0};
    std::size_t ClockExchangeCorrelationSlots{0};
};

/// <summary>Deterministic accounting snapshot for one statically composed Radio runtime.</summary>
struct RadioResourceAccountingSnapshot final {
    std::size_t ProviderRegistrySlots{0};
    std::size_t ProviderRegistryPointerBytes{0};
    std::size_t PeerRegistrySlots{0};
    std::size_t PeerRegistryObjectBytes{0};
    std::array<RadioCapacityDomainResourceAccounting,8> InboundDomains{};
    std::array<RadioCapacityDomainResourceAccounting,8> OutboundDomains{};
    std::size_t InboundCapacityObjectBytes{0};
    std::size_t OutboundCapacityObjectBytes{0};
    std::size_t InboundConfiguredRecordSlots{0};
    std::size_t OutboundConfiguredRecordSlots{0};
    std::size_t InboundConfiguredByteSlots{0};
    std::size_t OutboundConfiguredByteSlots{0};
    std::size_t InboundConfiguredBytePayloadBytes{0};
    std::size_t OutboundConfiguredBytePayloadBytes{0};
    std::size_t ReassemblyObjectBytes{0};
    std::size_t MaximumActiveReassemblies{0};
    std::size_t ReassemblyMetadataBytesPerActive{0};
    std::size_t ReassemblyBitmapBytesPerActive{0};
    std::size_t RecentInboundTransferIdBytes{0};
    std::size_t OutboundLogicalTransferRecordBytes{0};
    std::size_t OutboundClassQueueLeaseBytes{0};
    std::size_t OutboundClassQueueIndexBytes{0};
    std::size_t RecentOutboundTransferIdBytes{0};
    std::size_t DrrDeficitCursorBytes{0};
    std::size_t SchedulerObjectBytes{0};
    std::size_t PhysicalEncodeScratchBytesPerDomain{0};
    std::size_t ProviderDeferredCompletionSlotsPerDomain{0};
    std::size_t DomainRuntimeObjectBytes{0};
    std::size_t DomainTaskStackBytes{0};
    std::size_t WakeSignalObjectBytes{0};
    std::size_t IngressRouterObjectBytes{0};
    std::size_t ClockCoordinatorObjectBytes{0};
    std::size_t ClockExchangeCorrelationSlots{0};
    std::size_t RadioRuntimeObjectBytes{0};
    /// Object footprint of the supplied composition only. Task stack, provider-owned queues/retries and externally
    /// allocated platform synchronization implementations are reported separately so they cannot be double-counted.
    std::size_t RadioOwnedStaticObjectBytes{0};
};

struct RadioProviderHiddenResourceAccounting final {
    std::size_t ProviderObjectBytes{0};
    RadioProviderResourceProfile DeclaredProfile{};
    std::size_t AdditionalKnownQueueBytes{0};
    std::size_t AdditionalKnownRetryStateBytes{0};
};

namespace Detail {
inline constexpr std::size_t SumRecordSlots(const std::array<RadioCapacityDomainResourceAccounting,8>& domains) noexcept {
    std::size_t total=0; for(const auto& domain:domains) total+=domain.RecordSlots; return total;
}
inline constexpr std::size_t SumByteSlots(const std::array<RadioCapacityDomainResourceAccounting,8>& domains) noexcept {
    std::size_t total=0; for(const auto& domain:domains) total+=domain.ByteSlots; return total;
}
inline constexpr std::size_t SumBytePayloadBytes(const std::array<RadioCapacityDomainResourceAccounting,8>& domains) noexcept {
    std::size_t total=0; for(const auto& domain:domains) total+=domain.BytePayloadBytes; return total;
}
}

/// <summary>
/// Builds an exact object-footprint report for the concrete types used by one Radio deployment plus explicit configured
/// capacity facts that type erasure/provider hardware prevents Radio from discovering safely at runtime.
/// </summary>
template<class TRadioRuntime,
         class TPeerRegistry,
         class TInboundCapacity,
         class TOutboundCapacity,
         class TReassembly,
         class TScheduler,
         class TDomainRuntime,
         class TIngressRouter,
         class TClockCoordinator>
constexpr RadioResourceAccountingSnapshot MakeRadioResourceAccounting(
    const RadioResourceAccountingConfiguration& configuration) noexcept {
    RadioResourceAccountingSnapshot result{};
    result.ProviderRegistrySlots=configuration.ProviderRegistrySlots;
    result.ProviderRegistryPointerBytes=configuration.ProviderRegistrySlots*sizeof(IRadio*);
    result.PeerRegistrySlots=configuration.PeerRegistrySlots;
    result.PeerRegistryObjectBytes=sizeof(TPeerRegistry);
    result.InboundDomains=configuration.InboundDomains;
    result.OutboundDomains=configuration.OutboundDomains;
    result.InboundCapacityObjectBytes=sizeof(TInboundCapacity);
    result.OutboundCapacityObjectBytes=sizeof(TOutboundCapacity);
    result.InboundConfiguredRecordSlots=Detail::SumRecordSlots(configuration.InboundDomains);
    result.OutboundConfiguredRecordSlots=Detail::SumRecordSlots(configuration.OutboundDomains);
    result.InboundConfiguredByteSlots=Detail::SumByteSlots(configuration.InboundDomains);
    result.OutboundConfiguredByteSlots=Detail::SumByteSlots(configuration.OutboundDomains);
    result.InboundConfiguredBytePayloadBytes=Detail::SumBytePayloadBytes(configuration.InboundDomains);
    result.OutboundConfiguredBytePayloadBytes=Detail::SumBytePayloadBytes(configuration.OutboundDomains);
    result.ReassemblyObjectBytes=sizeof(TReassembly);
    result.MaximumActiveReassemblies=configuration.MaximumActiveReassemblies;
    result.ReassemblyMetadataBytesPerActive=sizeof(RadioReassemblyRecord);
    result.ReassemblyBitmapBytesPerActive=sizeof(std::array<std::uint8_t,32>);
    result.RecentInboundTransferIdBytes=configuration.RecentInboundTransferIds*sizeof(RadioTransferId);
    result.OutboundLogicalTransferRecordBytes=sizeof(RadioOutboundTransferRecord);
    result.OutboundClassQueueLeaseBytes=RadioServiceClassCount*configuration.OutboundQueueDepthPerClass*sizeof(RadioCapacityRecordLease);
    result.OutboundClassQueueIndexBytes=RadioServiceClassCount*3u*sizeof(std::size_t);
    result.RecentOutboundTransferIdBytes=configuration.RecentOutboundTransferIds*sizeof(RadioTransferId);
    result.DrrDeficitCursorBytes=RadioServiceClassCount*sizeof(std::int64_t)+sizeof(std::size_t);
    result.SchedulerObjectBytes=sizeof(TScheduler);
    result.PhysicalEncodeScratchBytesPerDomain=configuration.PhysicalEncodeScratchBytesPerDomain;
    result.ProviderDeferredCompletionSlotsPerDomain=configuration.ProviderDeferredCompletionSlotsPerDomain;
    result.DomainRuntimeObjectBytes=sizeof(TDomainRuntime);
    result.DomainTaskStackBytes=configuration.DomainTaskStackBytes;
    result.WakeSignalObjectBytes=configuration.WakeSignalObjectBytes;
    result.IngressRouterObjectBytes=sizeof(TIngressRouter);
    result.ClockCoordinatorObjectBytes=sizeof(TClockCoordinator);
    result.ClockExchangeCorrelationSlots=configuration.ClockExchangeCorrelationSlots;
    result.RadioRuntimeObjectBytes=sizeof(TRadioRuntime);
    result.RadioOwnedStaticObjectBytes=sizeof(TRadioRuntime)+sizeof(TInboundCapacity)+sizeof(TOutboundCapacity)+
        sizeof(TReassembly)+sizeof(TScheduler)+sizeof(TDomainRuntime)+sizeof(TIngressRouter)+sizeof(TClockCoordinator);
    return result;
}

template<class TProvider>
RadioProviderHiddenResourceAccounting MakeRadioProviderResourceAccounting(
    const TProvider& provider,
    std::size_t additionalKnownQueueBytes=0,
    std::size_t additionalKnownRetryStateBytes=0) noexcept {
    return {sizeof(TProvider),provider.ProviderResources(),additionalKnownQueueBytes,additionalKnownRetryStateBytes};
}

} // namespace ESPressio::Radio
