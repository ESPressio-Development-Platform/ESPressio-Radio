#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <ESPressio_SystemPlatformClock.hpp>

#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioClockWireV1.hpp"
#include "ESPressio_RadioReassembly.hpp"
#include "ESPressio_RadioWireV3.hpp"

namespace ESPressio::Radio {

/// <summary>Fixed Clock-frame sink owned by Radio Clock orchestration.</summary>
class IRadioClockFrameSink {
public:
    virtual ~IRadioClockFrameSink() = default;
    virtual void OnRadioClockFrame(
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp) noexcept = 0;
};

/// <summary>Fixed notification that one trusted v3 logical transfer has become complete.</summary>
class IRadioReassemblyReadySink {
public:
    virtual ~IRadioReassemblyReadySink() = default;
    virtual void RadioReassemblyReady(
        IRadio& provider,
        const RadioAddress& source,
        RadioTransferId transferId,
        RadioServiceClass service) noexcept = 0;
};

/// <summary>Optional fixed trust classifier used before v3 bytes enter protected trusted Q1 domains.</summary>
struct RadioIngressTrustTarget final {
    void* Context{nullptr};
    bool (*IsTrusted)(
        void*,
        IRadio&,
        const RadioPacketView&,
        const RadioTransportV3FragmentView&) noexcept{nullptr};

    bool Evaluate(
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioTransportV3FragmentView& fragment) const noexcept {
        return IsTrusted && IsTrusted(Context,provider,packet,fragment);
    }
};

struct RadioIngressRouterStatistics final {
    std::uint64_t ClockFrames{0};
    std::uint64_t V3Fragments{0};
    std::uint64_t MalformedFrames{0};
    std::uint64_t ResourceRejectedFrames{0};
};

/// <summary>
/// Sole managed-provider packet receiver for one Radio composition: compact Clock frames first, v3 reassembly otherwise.
/// </summary>
/// <remarks>
/// Providers install exactly this Radio-owned receiver rather than competing Clock/transport receivers. Clock magic is
/// consumed as Clock even when malformed so corrupt Clock frames cannot fall through and masquerade as v3. Ordinary v3
/// traffic enters the fixed reassembly table; untrusted ingress remains quarantined unless the configured fixed trust
/// classifier explicitly admits it. The physical receive monotonic coordinate is preferred for residence accounting so
/// provider/service queue delay never silently extends a sender's remaining residence budget.
/// </remarks>
template<class TReassemblyTable>
class RadioIngressRouter final : public IRadioReceiver {
    TReassemblyTable* _reassembly{nullptr};
    IRadioClockFrameSink* _clock{nullptr};
    IRadioReassemblyReadySink* _ready{nullptr};
    RadioIngressTrustTarget _trust{};
    std::atomic<std::uint64_t> _clockFrames{0};
    std::atomic<std::uint64_t> _v3Fragments{0};
    std::atomic<std::uint64_t> _malformed{0};
    std::atomic<std::uint64_t> _resourceRejected{0};

    static bool HasClockDiscriminator(const RadioPacketView& packet) noexcept {
        if(!packet.Payload || packet.PayloadSize<4) return false;
        const auto magic=static_cast<std::uint16_t>(packet.Payload[0]) |
            (static_cast<std::uint16_t>(packet.Payload[1])<<8u);
        if(magic!=RadioClockWireV1::Magic || packet.Payload[2]!=RadioClockWireV1::Version) return false;
        const auto type=static_cast<RadioClockMessageType>(packet.Payload[3]);
        return type==RadioClockMessageType::Request || type==RadioClockMessageType::Response;
    }

public:
    explicit RadioIngressRouter(TReassemblyTable& reassembly) noexcept : _reassembly(&reassembly) {}

    void Configure(
        IRadioClockFrameSink* clock,
        IRadioReassemblyReadySink* ready,
        RadioIngressTrustTarget trust={}) noexcept {
        _clock=clock;
        _ready=ready;
        _trust=trust;
    }

    void Attach(IRadio& provider) noexcept { provider.SetReceiver(this); }
    void Detach(IRadio& provider) noexcept { provider.SetReceiver(nullptr); }

    void OnRadioPacket(
        IRadio& provider,
        const RadioPacketView& packet,
        const RadioReceiveTimestampEvidence& timestamp) noexcept override {
        if(HasClockDiscriminator(packet)) {
            _clockFrames.fetch_add(1,std::memory_order_relaxed);
            if(_clock) _clock->OnRadioClockFrame(provider,packet,timestamp);
            else _resourceRejected.fetch_add(1,std::memory_order_relaxed);
            return;
        }

        RadioTransportV3FragmentView fragment{};
        if(!DecodeRadioTransportV3Fragment(packet.Payload,packet.PayloadSize,fragment)) {
            _malformed.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        _v3Fragments.fetch_add(1,std::memory_order_relaxed);

        std::uint64_t now=timestamp.MonotonicNanoseconds;
        if(now==0) now=packet.ReceiveTimestampNanoseconds;
        if(now==0) now=System::Clock::Monotonic().NowNanoseconds();
        const bool trusted=_trust.Evaluate(provider,packet,fragment);
        const auto status=_reassembly->Accept(provider,packet,fragment,now,trusted);
        if(status==RadioReassemblyStatus::Complete && trusted && _ready) {
            _ready->RadioReassemblyReady(
                provider,fragment.Header.Source,fragment.Header.TransferId,fragment.Header.ServiceClass);
        } else if(status==RadioReassemblyStatus::Busy || status==RadioReassemblyStatus::ResourceUnavailable) {
            _resourceRejected.fetch_add(1,std::memory_order_relaxed);
        } else if(status==RadioReassemblyStatus::Malformed || status==RadioReassemblyStatus::Expired) {
            _malformed.fetch_add(1,std::memory_order_relaxed);
        }
    }

    RadioIngressRouterStatistics Statistics() const noexcept {
        return {
            _clockFrames.load(std::memory_order_acquire),
            _v3Fragments.load(std::memory_order_acquire),
            _malformed.load(std::memory_order_acquire),
            _resourceRejected.load(std::memory_order_acquire)};
    }
};

} // namespace ESPressio::Radio
