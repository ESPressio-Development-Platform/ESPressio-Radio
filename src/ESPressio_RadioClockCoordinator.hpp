#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <ESPressio_IClockSynchronizationTarget.hpp>

#include "ESPressio_RadioClockWireV1.hpp"
#include "ESPressio_RadioDomainService.hpp"
#include "ESPressio_RadioIngressRouter.hpp"
#include "ESPressio_RadioScheduler.hpp"

namespace ESPressio::Radio {

enum class RadioClockCoordinatorMode : std::uint8_t {
    Disabled = 0,
    Client = 1,
    Reference = 2,
    ClientAndReference = 3
};

struct RadioClockCoordinatorConfig final {
    RadioClockCoordinatorMode Mode{RadioClockCoordinatorMode::Disabled};
    RadioAddress ReferencePeer{};
    std::uint64_t ReferenceIdentity{0};
    Timing::ClockCaptureQuality LocalTransmitCaptureQuality{Timing::ClockCaptureQuality::SoftwareUnbounded};
    Timing::ClockUncertainty LocalTransmitCaptureUncertainty{};
    std::uint64_t SchedulerSafetyGuardNanoseconds{250'000};
    std::uint64_t ReferenceProcessingAllowanceNanoseconds{500'000};
    std::uint64_t ReferenceResponseServiceBudgetNanoseconds{1'000'000};
};

struct RadioClockCoordinatorStatistics final {
    std::uint64_t ClientCampaignsQueued{0};
    std::uint64_t ClientRequestsCompleted{0};
    std::uint64_t ClientRequestFailures{0};
    std::uint64_t ResponsesReceived{0};
    std::uint64_t ObservationsAccepted{0};
    std::uint64_t ObservationsRejected{0};
    std::uint64_t ResponseTimeouts{0};
    std::uint64_t ReferenceRequestsReceived{0};
    std::uint64_t ReferenceResponsesQueued{0};
    std::uint64_t ReferenceResponsesCompleted{0};
    std::uint64_t ReferenceResponseFailures{0};
    std::uint64_t IgnoredFrames{0};
    std::uint64_t TimestampContinuityResets{0};
    std::uint64_t SchedulerDeadlineMisses{0};
};

enum class RadioClockCoordinatorStatus : std::uint8_t {
    Success = 0,
    AlreadyInitialized,
    InvalidConfiguration,
    ProviderProfileUnsupported,
    TimingConfigurationRejected
};

/// <summary>
/// Bounded direct-neighbour Clock orchestration on the normal Radio Clock-Q1/R3 path.
/// </summary>
/// <remarks>
/// This object owns no Task, queue or dynamic observer graph. RadioDomainRuntime calls its bounded ServiceDomain()
/// quantum on the existing contention-domain Task. Client cadence comes only from Timing's
/// NextRequiredSynchronizationMonotonic; there is no fixed one-second loop. At most one client exchange and one
/// reference response are retained. Compact frames are admitted through RadioDomainScheduler::SubmitClockFrame(), so
/// Clock uses the same DRR/EDF promotion debt, provider cost, Busy/completion and contention-domain serialization as
/// every other class.
///
/// T1/T3 software captures happen from the scheduler's late-prepare thunk immediately before provider Send(). RX T2/T4
/// use provider capture-time clock-model evidence when available. A later current-System-minus-elapsed reconstruction is
/// never performed. Unbounded/service-time evidence remains useful for acquisition/diagnostics but Timing receives it as
/// SoftwareUnbounded and therefore cannot certify Synchronized/Holdover from that evidence.
///
/// The selected reference identity is trusted composition context rather than redundant wire data. The 32-byte response
/// carries T2 System time plus separate T2->T3 System and remote-monotonic durations; the latter gives K1 genuine remote
/// chronology without pretending remote monotonic coordinates share the client's epoch.
/// </remarks>
template<class TScheduler>
class RadioClockCoordinator final :
    public IRadioClockFrameSink,
    public IRadioDomainServiceExtension,
    public IRadioTransferResultSink {

    struct PendingReferenceResponse final {
        bool Active{false};
        bool Queued{false};
        std::uint32_t Sequence{0};
        RadioAddress Destination{};
        Timing::ClockTimestampCapture<> T2{};
        std::uint64_t ServiceDeadlineNanoseconds{0};
        std::uint64_t ExpiryNanoseconds{0};
    };

    TScheduler* _scheduler{nullptr};
    IRadio* _provider{nullptr};
    Timing::IClockSynchronizationTarget* _target{nullptr};
    IRadioTransferResultSink* _downstreamResults{nullptr};
    RadioClockCoordinatorConfig _config{};
    RadioDomainServiceWakeTarget _wake{};
    bool _initialized{false};

    std::uint32_t _nextSequence{1};
    std::uint32_t _pendingSequence{0};
    RadioTransferId _requestTransferId{0};
    Timing::ClockTimestampCapture<> _pendingT1{};
    std::uint64_t _responseExpiryNanoseconds{0};
    std::uint64_t _lastRecordedTimingDeadline{0};
    std::uint32_t _receiveContinuityGeneration{0};

    PendingReferenceResponse _referenceResponse{};
    RadioTransferId _responseTransferId{0};
    RadioClockCoordinatorStatistics _statistics{};

    static bool IsClientMode(RadioClockCoordinatorMode mode) noexcept {
        return mode==RadioClockCoordinatorMode::Client || mode==RadioClockCoordinatorMode::ClientAndReference;
    }
    static bool IsReferenceMode(RadioClockCoordinatorMode mode) noexcept {
        return mode==RadioClockCoordinatorMode::Reference || mode==RadioClockCoordinatorMode::ClientAndReference;
    }
    static std::uint64_t AddSaturating(std::uint64_t a,std::uint64_t b) noexcept {
        return b>std::numeric_limits<std::uint64_t>::max()-a
            ? std::numeric_limits<std::uint64_t>::max() : a+b;
    }
    static std::uint64_t SubtractFloor(std::uint64_t value,std::uint64_t amount,std::uint64_t floor) noexcept {
        return value>amount ? (value-amount<floor?floor:value-amount) : floor;
    }
    static std::uint64_t Earlier(std::uint64_t first,std::uint64_t second) noexcept {
        if(first==0) return second;
        if(second==0) return first;
        return first<second?first:second;
    }
    static RadioServiceProfile ClockProfile() noexcept {
        return {RadioServiceClass::Clock,RadioDeadlineTreatment::Promotable,
                RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    }
    static bool ValidLocalTransmitCapture(const RadioClockCoordinatorConfig& config) noexcept {
        if(config.LocalTransmitCaptureQuality==Timing::ClockCaptureQuality::SoftwareUnbounded)
            return !config.LocalTransmitCaptureUncertainty.IsKnown;
        if(config.LocalTransmitCaptureQuality==Timing::ClockCaptureQuality::SoftwareBounded)
            return config.LocalTransmitCaptureUncertainty.IsKnown;
        return false;
    }
    static Timing::ClockCaptureQuality ToTimingQuality(RadioClockCaptureQuality quality) noexcept {
        switch(quality) {
            case RadioClockCaptureQuality::Hardware: return Timing::ClockCaptureQuality::Hardware;
            case RadioClockCaptureQuality::SoftwareBounded: return Timing::ClockCaptureQuality::SoftwareBounded;
            case RadioClockCaptureQuality::SoftwareUnbounded: return Timing::ClockCaptureQuality::SoftwareUnbounded;
            default: return Timing::ClockCaptureQuality::Invalid;
        }
    }
    static RadioClockCaptureQuality ToWireQuality(Timing::ClockCaptureQuality quality) noexcept {
        switch(quality) {
            case Timing::ClockCaptureQuality::Hardware: return RadioClockCaptureQuality::Hardware;
            case Timing::ClockCaptureQuality::SoftwareBounded: return RadioClockCaptureQuality::SoftwareBounded;
            case Timing::ClockCaptureQuality::SoftwareUnbounded: return RadioClockCaptureQuality::SoftwareUnbounded;
            default: return RadioClockCaptureQuality::Invalid;
        }
    }
    static Timing::ClockCaptureQuality WeakestCaptureQuality(
        Timing::ClockCaptureQuality first,Timing::ClockCaptureQuality second) noexcept {
        if(first==Timing::ClockCaptureQuality::Invalid || second==Timing::ClockCaptureQuality::Invalid)
            return Timing::ClockCaptureQuality::Invalid;
        if(first==Timing::ClockCaptureQuality::SoftwareUnbounded || second==Timing::ClockCaptureQuality::SoftwareUnbounded)
            return Timing::ClockCaptureQuality::SoftwareUnbounded;
        if(first==Timing::ClockCaptureQuality::Hardware && second==Timing::ClockCaptureQuality::Hardware)
            return Timing::ClockCaptureQuality::Hardware;
        return Timing::ClockCaptureQuality::SoftwareBounded;
    }
    static Timing::ClockUncertainty MaximumUncertainty(
        Timing::ClockUncertainty first,Timing::ClockUncertainty second) noexcept {
        if(!first.IsKnown || !second.IsKnown) return {};
        return Timing::ClockUncertainty::Known(first.Nanoseconds>second.Nanoseconds?first.Nanoseconds:second.Nanoseconds);
    }
    static Timing::ClockUncertainty WireRepresentable(Timing::ClockUncertainty value) noexcept {
        if(value.IsKnown && value.Nanoseconds>RadioClockWireV1::MaximumKnownUncertainty24) return {};
        return value;
    }
    void Wake() noexcept { if(_wake.Wake) _wake.Wake(_wake.Context); }

    Timing::ClockTimestampCapture<> CaptureReceive(
        const RadioReceiveTimestampEvidence& evidence) const noexcept {
        if(evidence.MonotonicNanoseconds!=0 && evidence.HasHistoricalModel()) {
            Timing::ClockTimestampCapture<> capture{};
            capture.SystemTimeNanoseconds=evidence.CaptureModel.Evaluate(evidence.MonotonicNanoseconds);
            capture.MonotonicTimeNanoseconds=evidence.MonotonicNanoseconds;
            if(evidence.HasFiniteBound()) {
                capture.Uncertainty=Timing::ClockUncertainty::Known(evidence.ConservativeUncertaintyNanoseconds);
                capture.Quality=evidence.Source==RadioTimestampCaptureSource::Hardware
                    ?Timing::ClockCaptureQuality::Hardware
                    :Timing::ClockCaptureQuality::SoftwareBounded;
            } else {
                capture.Quality=Timing::ClockCaptureQuality::SoftwareUnbounded;
            }
            return capture;
        }
        // Explicit acquisition-only fallback. This is a current service-time capture, never a fabricated historical one.
        return _target->CaptureSynchronizationTimestamp(Timing::ClockCaptureQuality::SoftwareUnbounded,{});
    }

    void ObserveContinuity(const RadioReceiveTimestampEvidence& evidence) noexcept {
        if(evidence.ContinuityGeneration==0) return;
        if(_receiveContinuityGeneration==0) {
            _receiveContinuityGeneration=evidence.ContinuityGeneration;
            return;
        }
        if(_receiveContinuityGeneration==evidence.ContinuityGeneration) return;
        _receiveContinuityGeneration=evidence.ContinuityGeneration;
        if(IsClientMode(_config.Mode)) {
            _target->ResetSynchronization();
            ++_statistics.TimestampContinuityResets;
        }
    }

    void ClearClientObservationState() noexcept {
        _pendingSequence=0;
        _pendingT1={};
        _responseExpiryNanoseconds=0;
    }
    void ClearReferenceResponse() noexcept {
        _referenceResponse={};
        _responseTransferId=0;
    }

    static bool PrepareRequestThunk(void* context,std::uint8_t* frame,std::size_t bytes,std::uint64_t now) noexcept {
        return static_cast<RadioClockCoordinator*>(context)->PrepareRequest(frame,bytes,now);
    }
    bool PrepareRequest(std::uint8_t*,std::size_t bytes,std::uint64_t) noexcept {
        if(!_initialized || _pendingSequence==0 || bytes<RadioClockWireV1::HeaderBytes) return false;
        _pendingT1=_target->CaptureSynchronizationTimestamp(
            _config.LocalTransmitCaptureQuality,_config.LocalTransmitCaptureUncertainty);
        if(_pendingT1.Quality==Timing::ClockCaptureQuality::Invalid || _pendingT1.MonotonicTimeNanoseconds==0) return false;
        const auto profile=_target->GetSynchronizationProfile();
        _responseExpiryNanoseconds=AddSaturating(
            _pendingT1.MonotonicTimeNanoseconds,profile.MaximumAcceptedRoundTripDelayNanoseconds);
        return true;
    }

    static bool PrepareResponseThunk(void* context,std::uint8_t* frame,std::size_t bytes,std::uint64_t now) noexcept {
        return static_cast<RadioClockCoordinator*>(context)->PrepareResponse(frame,bytes,now);
    }
    bool PrepareResponse(std::uint8_t* frame,std::size_t bytes,std::uint64_t) noexcept {
        if(!_initialized || !_referenceResponse.Active || !_referenceResponse.Queued ||
           bytes!=RadioClockWireV1::ResponseBytes) return false;
        const auto t3=_target->CaptureSynchronizationTimestamp(
            _config.LocalTransmitCaptureQuality,_config.LocalTransmitCaptureUncertainty);
        const auto& t2=_referenceResponse.T2;
        if(t2.Quality==Timing::ClockCaptureQuality::Invalid || t3.Quality==Timing::ClockCaptureQuality::Invalid ||
           t3.SystemTimeNanoseconds<t2.SystemTimeNanoseconds ||
           t3.MonotonicTimeNanoseconds<t2.MonotonicTimeNanoseconds) return false;
        const auto systemDelta=t3.SystemTimeNanoseconds-t2.SystemTimeNanoseconds;
        const auto monotonicDelta=t3.MonotonicTimeNanoseconds-t2.MonotonicTimeNanoseconds;
        if(systemDelta>std::numeric_limits<std::uint32_t>::max() ||
           monotonicDelta>std::numeric_limits<std::uint32_t>::max()) return false;

        const auto status=_target->GetSynchronizationStatus();
        RadioClockResponseV1 response{};
        response.Sequence=_referenceResponse.Sequence;
        response.T2SystemNanoseconds=t2.SystemTimeNanoseconds;
        response.RemoteSystemProcessingNanoseconds=static_cast<std::uint32_t>(systemDelta);
        response.RemoteMonotonicProcessingNanoseconds=static_cast<std::uint32_t>(monotonicDelta);
        response.ReferenceReliability=status.Reliability;
        const auto combinedQuality=WeakestCaptureQuality(t2.Quality,t3.Quality);
        response.CaptureQuality=ToWireQuality(combinedQuality);
        response.ReferenceUncertainty=WireRepresentable(status.CurrentUncertainty);
        response.CaptureUncertainty=WireRepresentable(MaximumUncertainty(t2.Uncertainty,t3.Uncertainty));
        return response.CaptureQuality!=RadioClockCaptureQuality::Invalid &&
               EncodeRadioClockResponseV1(response,frame,bytes);
    }

    bool QueueClientRequest(std::uint64_t now,std::uint64_t timingDeadline) noexcept {
        if(_pendingSequence!=0 || _requestTransferId!=0) return false;
        const auto localAddress=_provider->LocalAddress();
        std::uint32_t sequence=_nextSequence++;
        if(sequence==0) sequence=_nextSequence++;
        if(sequence==0) sequence=1;
        RadioClockRequestV1 request{sequence,localAddress};
        std::array<std::uint8_t,RadioClockWireV1::MaximumRequestBytes> frame{};
        std::size_t frameBytes=0;
        if(!EncodeRadioClockRequestV1(request,frame.data(),frame.size(),frameBytes)) return false;

        const auto clockProfile=ClockProfile();
        const auto profile=_target->GetSynchronizationProfile();
        const auto requestCost=_provider->EstimateTransmissionCost(_config.ReferencePeer,frameBytes,clockProfile);
        const auto responseCost=_provider->EstimateTransmissionCost(_config.ReferencePeer,RadioClockWireV1::ResponseBytes,clockProfile);
        if(!requestCost.SupportsPromotableDeadline() || !responseCost.SupportsPromotableDeadline()) return false;
        auto allowance=AddSaturating(requestCost.ConservativeAirtimeNanoseconds,responseCost.ConservativeAirtimeNanoseconds);
        allowance=AddSaturating(allowance,_config.ReferenceProcessingAllowanceNanoseconds);
        allowance=AddSaturating(allowance,_config.SchedulerSafetyGuardNanoseconds);

        std::uint64_t serviceDeadline=timingDeadline;
        std::uint64_t expiry=0;
        if(timingDeadline<=now) {
            serviceDeadline=now==0?1:now;
            expiry=AddSaturating(serviceDeadline,profile.MaximumAcceptedRoundTripDelayNanoseconds);
        } else {
            serviceDeadline=SubtractFloor(timingDeadline,allowance,now==0?1:now);
            expiry=AddSaturating(timingDeadline,profile.MaximumAcceptedRoundTripDelayNanoseconds);
        }
        if(expiry==0 || serviceDeadline==0 || serviceDeadline>expiry) return false;

        _pendingSequence=sequence;
        _pendingT1={};
        _responseExpiryNanoseconds=0;
        const auto submitted=_scheduler->SubmitClockFrame(
            *_provider,_config.ReferencePeer,clockProfile,{expiry,serviceDeadline},frame.data(),frameBytes,
            {this,&PrepareRequestThunk});
        if(!submitted) {
            ClearClientObservationState();
            return false;
        }
        _requestTransferId=submitted.TransferId;
        ++_statistics.ClientCampaignsQueued;
        return true;
    }

    bool QueueReferenceResponse(std::uint64_t now) noexcept {
        if(!_referenceResponse.Active || _referenceResponse.Queued || _responseTransferId!=0) return false;
        if(now>=_referenceResponse.ExpiryNanoseconds) {
            ++_statistics.ReferenceResponseFailures;
            ClearReferenceResponse();
            return false;
        }
        if(now>_referenceResponse.ServiceDeadlineNanoseconds) {
            ++_statistics.ReferenceResponseFailures;
            ClearReferenceResponse();
            return false;
        }
        RadioClockResponseV1 placeholder{};
        placeholder.Sequence=_referenceResponse.Sequence;
        placeholder.ReferenceReliability=Timing::TimeReliability::Unqualified;
        placeholder.CaptureQuality=RadioClockCaptureQuality::SoftwareUnbounded;
        std::array<std::uint8_t,RadioClockWireV1::ResponseBytes> frame{};
        if(!EncodeRadioClockResponseV1(placeholder,frame.data(),frame.size())) return false;
        const auto submitted=_scheduler->SubmitClockFrame(
            *_provider,_referenceResponse.Destination,ClockProfile(),
            {_referenceResponse.ExpiryNanoseconds,_referenceResponse.ServiceDeadlineNanoseconds},
            frame.data(),frame.size(),{this,&PrepareResponseThunk});
        if(!submitted) return false;
        _responseTransferId=submitted.TransferId;
        _referenceResponse.Queued=true;
        ++_statistics.ReferenceResponsesQueued;
        return true;
    }

    void AcceptReferenceRequest(
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp,
        const RadioClockRequestV1& request) noexcept {
        if(!IsReferenceMode(_config.Mode) || _referenceResponse.Active) {
            ++_statistics.IgnoredFrames; return;
        }
        if(packet.Source.IsValid() && packet.Source!=request.ReplyAddress) {
            ++_statistics.IgnoredFrames; return;
        }
        const auto capabilities=_provider->Capabilities();
        if(capabilities.AddressBytes!=0 && request.ReplyAddress.Length!=capabilities.AddressBytes) {
            ++_statistics.IgnoredFrames; return;
        }
        const auto t2=CaptureReceive(timestamp);
        if(t2.Quality==Timing::ClockCaptureQuality::Invalid || t2.MonotonicTimeNanoseconds==0) {
            ++_statistics.IgnoredFrames; return;
        }
        const auto profile=_target->GetSynchronizationProfile();
        const auto now=t2.MonotonicTimeNanoseconds;
        auto serviceDeadline=AddSaturating(now,_config.ReferenceResponseServiceBudgetNanoseconds);
        const auto expiry=AddSaturating(now,profile.MaximumAcceptedRoundTripDelayNanoseconds);
        if(serviceDeadline>expiry) serviceDeadline=expiry;
        _referenceResponse.Active=true;
        _referenceResponse.Sequence=request.Sequence;
        _referenceResponse.Destination=packet.Source.IsValid()?packet.Source:request.ReplyAddress;
        _referenceResponse.T2=t2;
        _referenceResponse.ServiceDeadlineNanoseconds=serviceDeadline;
        _referenceResponse.ExpiryNanoseconds=expiry;
        ++_statistics.ReferenceRequestsReceived;
        Wake();
    }

    void AcceptClientResponse(
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp,
        const RadioClockResponseV1& response) noexcept {
        if(!IsClientMode(_config.Mode) || _pendingSequence==0 || response.Sequence!=_pendingSequence ||
           _pendingT1.Quality==Timing::ClockCaptureQuality::Invalid) {
            ++_statistics.IgnoredFrames; return;
        }
        if(packet.Source.IsValid() && packet.Source!=_config.ReferencePeer) {
            ++_statistics.IgnoredFrames; return;
        }
        const auto t4=CaptureReceive(timestamp);
        if(t4.Quality==Timing::ClockCaptureQuality::Invalid || t4.MonotonicTimeNanoseconds==0) {
            ++_statistics.IgnoredFrames; return;
        }
        if(response.T2SystemNanoseconds>
           std::numeric_limits<std::uint64_t>::max()-response.RemoteSystemProcessingNanoseconds) {
            ++_statistics.IgnoredFrames; return;
        }

        Timing::ClockSynchronizationObservation<> observation{};
        observation.T1=_pendingT1;
        observation.T2.SystemTimeNanoseconds=response.T2SystemNanoseconds;
        observation.T2.MonotonicTimeNanoseconds=1;
        observation.T2.Quality=ToTimingQuality(response.CaptureQuality);
        observation.T2.Uncertainty=response.CaptureUncertainty;
        observation.T3.SystemTimeNanoseconds=response.T2SystemNanoseconds+response.RemoteSystemProcessingNanoseconds;
        observation.T3.MonotonicTimeNanoseconds=1ULL+response.RemoteMonotonicProcessingNanoseconds;
        observation.T3.Quality=observation.T2.Quality;
        observation.T3.Uncertainty=response.CaptureUncertainty;
        observation.T4=t4;
        observation.ReferenceIdentity=_config.ReferenceIdentity;
        observation.ReferenceReliability=response.ReferenceReliability;
        observation.ReferenceUncertainty=response.ReferenceUncertainty;

        ClearClientObservationState();
        ++_statistics.ResponsesReceived;
        const auto result=_target->SubmitSynchronizationObservation(observation);
        if(result.Accepted) ++_statistics.ObservationsAccepted;
        else ++_statistics.ObservationsRejected;
    }

public:
    RadioClockCoordinator(
        TScheduler& scheduler,
        IRadio& provider,
        Timing::IClockSynchronizationTarget& target,
        IRadioTransferResultSink* downstreamResults=nullptr) noexcept
        : _scheduler(&scheduler),_provider(&provider),_target(&target),_downstreamResults(downstreamResults) {}

    RadioClockCoordinatorStatus Initialize(const RadioClockCoordinatorConfig& config) noexcept {
        if(_initialized) return RadioClockCoordinatorStatus::AlreadyInitialized;
        if(config.Mode==RadioClockCoordinatorMode::Disabled || !ValidLocalTransmitCapture(config) ||
           config.ReferenceResponseServiceBudgetNanoseconds==0) return RadioClockCoordinatorStatus::InvalidConfiguration;
        const auto capabilities=_provider->Capabilities();
        if(capabilities.MaximumPayloadBytes<RadioClockWireV1::ResponseBytes || !_provider->LocalAddress().IsValid())
            return RadioClockCoordinatorStatus::ProviderProfileUnsupported;
        const auto timingProfile=_target->GetSynchronizationProfile();
        if(timingProfile.MaximumAcceptedRoundTripDelayNanoseconds==0 ||
           timingProfile.MaximumAcceptedRoundTripDelayNanoseconds>std::numeric_limits<std::uint32_t>::max())
            return RadioClockCoordinatorStatus::InvalidConfiguration;
        if(IsClientMode(config.Mode)) {
            if(!config.ReferencePeer.IsValid() || config.ReferenceIdentity==0 ||
               (capabilities.AddressBytes!=0 && config.ReferencePeer.Length!=capabilities.AddressBytes))
                return RadioClockCoordinatorStatus::InvalidConfiguration;
            if(_target->SelectSynchronizationReference(config.ReferenceIdentity)!=Timing::ClockConfigurationStatus::Success)
                return RadioClockCoordinatorStatus::TimingConfigurationRejected;
            _target->SetSynchronizationActivity(true,true);
        }
        _config=config;
        _initialized=true;
        Wake();
        return RadioClockCoordinatorStatus::Success;
    }

    void Shutdown() noexcept {
        if(!_initialized) return;
        if(IsClientMode(_config.Mode)) _target->SetSynchronizationActivity(false,false);
        _initialized=false;
        _requestTransferId=0;
        _responseTransferId=0;
        ClearClientObservationState();
        ClearReferenceResponse();
        _wake={};
    }

    void SetDomainWakeTarget(RadioDomainServiceWakeTarget target) noexcept override {
        _wake=target;
        if(_initialized) Wake();
    }

    RadioDomainExtensionServiceResult ServiceDomain(std::uint64_t now) noexcept override {
        if(!_initialized) return {};
        std::uint64_t earliest=0;

        if(_referenceResponse.Active && !_referenceResponse.Queued) {
            (void)QueueReferenceResponse(now);
            if(_referenceResponse.Active && !_referenceResponse.Queued && now<_referenceResponse.ServiceDeadlineNanoseconds)
                earliest=Earlier(earliest,_referenceResponse.ServiceDeadlineNanoseconds);
        }

        if(_pendingSequence!=0 && _responseExpiryNanoseconds!=0) {
            if(now>=_responseExpiryNanoseconds) {
                ++_statistics.ResponseTimeouts;
                ClearClientObservationState();
            } else earliest=Earlier(earliest,_responseExpiryNanoseconds);
        }

        if(IsClientMode(_config.Mode) && _pendingSequence==0 && _requestTransferId==0) {
            const auto status=_target->GetSynchronizationStatus();
            if(status.HasSynchronizationDeadline && status.NextRequiredSynchronizationMonotonic!=0) {
                const auto localAddress=_provider->LocalAddress();
                const auto requestBytes=RadioClockWireV1::HeaderBytes+1u+localAddress.Length;
                const auto requestCost=_provider->EstimateTransmissionCost(_config.ReferencePeer,requestBytes,ClockProfile());
                const auto responseCost=_provider->EstimateTransmissionCost(_config.ReferencePeer,RadioClockWireV1::ResponseBytes,ClockProfile());
                if(requestCost.SupportsPromotableDeadline() && responseCost.SupportsPromotableDeadline()) {
                    auto allowance=AddSaturating(requestCost.ConservativeAirtimeNanoseconds,responseCost.ConservativeAirtimeNanoseconds);
                    allowance=AddSaturating(allowance,_config.ReferenceProcessingAllowanceNanoseconds);
                    allowance=AddSaturating(allowance,_config.SchedulerSafetyGuardNanoseconds);
                    const auto queueAt=SubtractFloor(status.NextRequiredSynchronizationMonotonic,allowance,1);
                    if(now>=queueAt) {
                        if(now>=status.NextRequiredSynchronizationMonotonic &&
                           _lastRecordedTimingDeadline!=status.NextRequiredSynchronizationMonotonic) {
                            _target->RecordSynchronizationDeadlineMiss();
                            _lastRecordedTimingDeadline=status.NextRequiredSynchronizationMonotonic;
                            ++_statistics.SchedulerDeadlineMisses;
                        }
                        (void)QueueClientRequest(now,status.NextRequiredSynchronizationMonotonic);
                        // If admission is presently unavailable, do not spin on a past deadline. Concrete capacity/
                        // provider lifecycle wakes re-enter this extension; otherwise the retained Timing state waits.
                    } else earliest=Earlier(earliest,queueAt);
                }
            }
        }
        return {false,earliest};
    }

    void OnRadioClockFrame(
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp) noexcept override {
        if(!_initialized || &provider!=_provider || !packet.Payload || packet.PayloadSize<RadioClockWireV1::HeaderBytes) {
            ++_statistics.IgnoredFrames; return;
        }
        ObserveContinuity(timestamp);
        if(packet.Payload[3]==static_cast<std::uint8_t>(RadioClockMessageType::Request)) {
            RadioClockRequestV1 request{};
            if(!DecodeRadioClockRequestV1(packet.Payload,packet.PayloadSize,request)) {
                ++_statistics.IgnoredFrames; return;
            }
            AcceptReferenceRequest(packet,timestamp,request);
            return;
        }
        if(packet.Payload[3]==static_cast<std::uint8_t>(RadioClockMessageType::Response)) {
            RadioClockResponseV1 response{};
            if(!DecodeRadioClockResponseV1(packet.Payload,packet.PayloadSize,response)) {
                ++_statistics.IgnoredFrames; return;
            }
            AcceptClientResponse(packet,timestamp,response);
            return;
        }
        ++_statistics.IgnoredFrames;
    }

    void RadioTransferResolved(const RadioTransferTerminalResult& result) noexcept override {
        if(_requestTransferId!=0 && result.TransferId==_requestTransferId) {
            _requestTransferId=0;
            if(result.Status==RadioTransferTerminalStatus::Completed) ++_statistics.ClientRequestsCompleted;
            else {
                ++_statistics.ClientRequestFailures;
                ClearClientObservationState();
            }
            return;
        }
        if(_responseTransferId!=0 && result.TransferId==_responseTransferId) {
            if(result.Status==RadioTransferTerminalStatus::Completed) ++_statistics.ReferenceResponsesCompleted;
            else ++_statistics.ReferenceResponseFailures;
            ClearReferenceResponse();
            return;
        }
        if(_downstreamResults) _downstreamResults->RadioTransferResolved(result);
    }

    RadioClockCoordinatorStatistics Statistics() const noexcept { return _statistics; }
    bool HasPendingClientExchange() const noexcept { return _pendingSequence!=0 || _requestTransferId!=0; }
    bool HasPendingReferenceResponse() const noexcept { return _referenceResponse.Active || _responseTransferId!=0; }
};

} // namespace ESPressio::Radio
