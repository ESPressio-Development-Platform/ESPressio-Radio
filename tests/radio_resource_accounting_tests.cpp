#include <ESPressio_RadioResources.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace ESPressio::Radio;

struct RuntimeObject { std::array<std::uint8_t,17> Data{}; };
struct PeerObject { std::array<std::uint8_t,19> Data{}; };
struct InboundCapacityObject { std::array<std::uint8_t,23> Data{}; };
struct OutboundCapacityObject { std::array<std::uint8_t,29> Data{}; };
struct ReassemblyObject { std::array<std::uint8_t,31> Data{}; };
struct SchedulerObject { std::array<std::uint8_t,37> Data{}; };
struct DomainRuntimeObject { std::array<std::uint8_t,41> Data{}; };
struct IngressRouterObject { std::array<std::uint8_t,43> Data{}; };
struct ClockCoordinatorObject { std::array<std::uint8_t,47> Data{}; };

int main() {
    RadioResourceAccountingConfiguration configuration{};
    configuration.ProviderRegistrySlots=3;
    configuration.PeerRegistrySlots=8;
    configuration.InboundDomains[0]={RadioCapacityDomainKind::InfrastructurePrivate,2,3,192};
    configuration.InboundDomains[6]={RadioCapacityDomainKind::SharedOverflow,4,5,1280};
    configuration.InboundDomains[7]={RadioCapacityDomainKind::UntrustedIngress,1,2,512};
    configuration.OutboundDomains[1]={RadioCapacityDomainKind::ClockPrivate,2,2,128};
    configuration.OutboundDomains[5]={RadioCapacityDomainKind::BestEffortPrivate,6,6,1536};
    configuration.OutboundDomains[6]={RadioCapacityDomainKind::SharedOverflow,3,3,768};
    configuration.MaximumActiveReassemblies=7;
    configuration.RecentInboundTransferIds=16;
    configuration.OutboundQueueDepthPerClass=4;
    configuration.RecentOutboundTransferIds=12;
    configuration.PhysicalEncodeScratchBytesPerDomain=300;
    configuration.ProviderDeferredCompletionSlotsPerDomain=1;
    configuration.DomainTaskStackBytes=4096;
    configuration.WakeSignalObjectBytes=24;
    configuration.ClockExchangeCorrelationSlots=1;

    constexpr std::size_t expectedStatic=sizeof(RuntimeObject)+sizeof(InboundCapacityObject)+
        sizeof(OutboundCapacityObject)+sizeof(ReassemblyObject)+sizeof(SchedulerObject)+
        sizeof(DomainRuntimeObject)+sizeof(IngressRouterObject)+sizeof(ClockCoordinatorObject);

    const auto report=MakeRadioResourceAccounting<
        RuntimeObject,PeerObject,InboundCapacityObject,OutboundCapacityObject,ReassemblyObject,
        SchedulerObject,DomainRuntimeObject,IngressRouterObject,ClockCoordinatorObject>(configuration);

    assert(report.ProviderRegistryPointerBytes==3*sizeof(IRadio*));
    assert(report.PeerRegistryObjectBytes==sizeof(PeerObject));
    assert(report.InboundConfiguredRecordSlots==7);
    assert(report.InboundConfiguredByteSlots==10);
    assert(report.InboundConfiguredBytePayloadBytes==1984);
    assert(report.OutboundConfiguredRecordSlots==11);
    assert(report.OutboundConfiguredByteSlots==11);
    assert(report.OutboundConfiguredBytePayloadBytes==2432);
    assert(report.MaximumActiveReassemblies==7);
    assert(report.ReassemblyMetadataBytesPerActive==sizeof(RadioReassemblyRecord));
    assert(report.ReassemblyBitmapBytesPerActive==32);
    assert(report.RecentInboundTransferIdBytes==16*sizeof(RadioTransferId));
    assert(report.OutboundLogicalTransferRecordBytes==sizeof(RadioOutboundTransferRecord));
    assert(report.OutboundClassQueueLeaseBytes==RadioServiceClassCount*4*sizeof(RadioCapacityRecordLease));
    assert(report.OutboundClassQueueIndexBytes==RadioServiceClassCount*3*sizeof(std::size_t));
    assert(report.RecentOutboundTransferIdBytes==12*sizeof(RadioTransferId));
    assert(report.PhysicalEncodeScratchBytesPerDomain==300);
    assert(report.ProviderDeferredCompletionSlotsPerDomain==1);
    assert(report.DomainTaskStackBytes==4096);
    assert(report.WakeSignalObjectBytes==24);
    assert(report.ClockExchangeCorrelationSlots==1);
    assert(report.RadioOwnedStaticObjectBytes==expectedStatic);
    return 0;
}
