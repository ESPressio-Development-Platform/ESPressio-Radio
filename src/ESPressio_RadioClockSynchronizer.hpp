#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <ESPressio_IClockSynchronizationTarget.hpp>
#include <ESPressio_SystemClock.hpp>
#include <ESPressio_SystemPlatformClock.hpp>
#include <ESPressio_Synchronization.hpp>

#include "ESPressio_IRadio.hpp"

namespace ESPressio::Radio {

enum class RadioClockSynchronizationMode : uint8_t {
    Disabled = 0,
    Client = 1,
    Reference = 2,
    ClientAndReference = 3
};

struct RadioClockSynchronizationConfig {
    RadioClockSynchronizationMode Mode = RadioClockSynchronizationMode::Disabled;
    RadioAddress ReferencePeer{};
    uint32_t SynchronizationIntervalMilliseconds = 1000;
    Timing::ClockSynchronizationAdjustmentMode AdjustmentMode =
        Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
    bool RequireReceiveTimestamp = false;
};

struct RadioClockSynchronizationStatistics {
    uint64_t RequestsAttempted = 0;
    uint64_t RequestsSent = 0;
    uint64_t RequestsReceived = 0;
    uint64_t ResponsesSent = 0;
    uint64_t ResponsesReceived = 0;
    uint64_t SendFailures = 0;
    uint64_t IgnoredFrames = 0;
    uint64_t SamplesAccepted = 0;
    uint64_t SamplesRejected = 0;
    uint64_t TimestampFallbacks = 0;
};

/// <summary>Performs four-timestamp clock synchronization directly over any ESPressio IRadio implementation.</summary>
/// <remarks>
/// The synchronizer is independent of RadioTransport routing/fragmentation and Foundation-Type semantics. Its largest wire
/// representation is exactly 32 bytes, so even an nRF24-class radio can carry the complete T1/T2/T3 response atomically.
/// RadioTransport sees these link-local control packets first through RadioWorker and ignores them because their wire magic
/// differs from RadioTransport framing; the synchronizer then consumes them through IRadioPacketObserver.
///
/// T1 is captured immediately before request transmission. T2 and T4 are recovered from provider-captured monotonic receive
/// timestamps where available. T3 is captured immediately before response transmission. Timing remains solely responsible
/// for offset/delay estimation, filtering, drift learning and clock discipline.
///
/// Requests also carry the client's opaque RadioAddress. Radios such as nRF24 do not expose the transmitter address on RX;
/// when packet.Source is unavailable the reference therefore replies to this embedded address. When Source is available it
/// takes precedence and must agree with the embedded address.
///
/// ReceiveTimestampNanoseconds is expected to use the active System::Clock::Monotonic() nanosecond domain. Providers that
/// cannot supply such a timestamp may leave it zero; unless RequireReceiveTimestamp is true, synchronization falls back to
/// the RadioWorker observer-callback time with reduced precision.
/// </remarks>
class RadioClockSynchronizer final : public IRadioPacketObserver {
private:
    enum class MessageType : uint8_t { Request = 1, Response = 2 };

    static constexpr uint16_t WireMagic = 0x5953u;
    static constexpr uint8_t WireVersion = 1u;
    static constexpr std::size_t HeaderBytes = 8u;
    static constexpr std::size_t RequestTimestampOffset = HeaderBytes;
    static constexpr std::size_t RequestAddressLengthOffset = 16u;
    static constexpr std::size_t RequestAddressOffset = 17u;
    static constexpr std::size_t RequestBytes = RequestAddressOffset + MaximumRadioAddressBytes;
    static constexpr std::size_t ResponseBytes = 32u;

    IRadio* _radio = nullptr;
    Timing::IClockSynchronizationTarget<Timing::ClockTick>* _target = nullptr;
    RadioClockSynchronizationConfig _config{};
    Observable::ObserverHandlePtr _packetSubscription{};

    std::atomic<uint32_t> _nextSequence{1};
    std::atomic<uint32_t> _pendingSequence{0};
    std::atomic<uint64_t> _lastRequestMonotonicNanoseconds{0};
    std::atomic<bool> _initialized{false};

    std::atomic<uint64_t> _requestsAttempted{0};
    std::atomic<uint64_t> _requestsSent{0};
    std::atomic<uint64_t> _requestsReceived{0};
    std::atomic<uint64_t> _responsesSent{0};
    std::atomic<uint64_t> _responsesReceived{0};
    std::atomic<uint64_t> _sendFailures{0};
    std::atomic<uint64_t> _ignoredFrames{0};
    std::atomic<uint64_t> _samplesAccepted{0};
    std::atomic<uint64_t> _samplesRejected{0};
    mutable std::atomic<uint64_t> _timestampFallbacks{0};
    mutable System::Synchronization::Mutex _stateMutex;

    static uint16_t ReadU16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(p[0]) |
            (static_cast<uint16_t>(p[1]) << 8u);
    }

    static uint32_t ReadU32(const uint8_t* p) noexcept {
        uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            value |= static_cast<uint32_t>(p[index]) << (8u * index);
        }
        return value;
    }

    static uint64_t ReadU64(const uint8_t* p) noexcept {
        uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<uint64_t>(p[index]) << (8u * index);
        }
        return value;
    }

    static void WriteU16(uint8_t* p, uint16_t value) noexcept {
        p[0] = static_cast<uint8_t>(value & 0xFFu);
        p[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    }

    static void WriteU32(uint8_t* p, uint32_t value) noexcept {
        for (std::size_t index = 0; index < 4; ++index) {
            p[index] = static_cast<uint8_t>((value >> (8u * index)) & 0xFFu);
        }
    }

    static void WriteU64(uint8_t* p, uint64_t value) noexcept {
        for (std::size_t index = 0; index < 8; ++index) {
            p[index] = static_cast<uint8_t>((value >> (8u * index)) & 0xFFu);
        }
    }

    static bool IsClientMode(RadioClockSynchronizationMode mode) noexcept {
        return mode == RadioClockSynchronizationMode::Client ||
            mode == RadioClockSynchronizationMode::ClientAndReference;
    }

    static bool IsReferenceMode(RadioClockSynchronizationMode mode) noexcept {
        return mode == RadioClockSynchronizationMode::Reference ||
            mode == RadioClockSynchronizationMode::ClientAndReference;
    }

    RadioClockSynchronizationConfig ConfigSnapshot() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_stateMutex);
        return _config;
    }

    bool DecodeHeader(const RadioPacketView& packet, MessageType& type, uint32_t& sequence) const noexcept {
        if (packet.Payload == nullptr || packet.PayloadSize < HeaderBytes) return false;
        if (ReadU16(packet.Payload) != WireMagic || packet.Payload[2] != WireVersion) return false;
        if (packet.Payload[3] == static_cast<uint8_t>(MessageType::Request)) {
            type = MessageType::Request;
        } else if (packet.Payload[3] == static_cast<uint8_t>(MessageType::Response)) {
            type = MessageType::Response;
        } else {
            return false;
        }
        sequence = ReadU32(packet.Payload + 4);
        return sequence != 0;
    }

    static RadioAddress DecodeRequestAddress(const uint8_t* payload) noexcept {
        const uint8_t length = payload[RequestAddressLengthOffset];
        if (length == 0 || length > MaximumRadioAddressBytes) return {};
        return RadioAddress::FromBytes(payload + RequestAddressOffset, length);
    }

    uint64_t RecoverSystemTimestamp(const RadioPacketView& packet) const {
        const uint64_t nowSystem = _target->GetSynchronizationTimestampNanoseconds();
        const uint64_t receiveMonotonic = packet.ReceiveTimestampNanoseconds;
        if (receiveMonotonic == 0) {
            _timestampFallbacks.fetch_add(1, std::memory_order_relaxed);
            return nowSystem;
        }
        const uint64_t nowMonotonic = System::Clock::Monotonic().NowNanoseconds();
        if (nowMonotonic <= receiveMonotonic) return nowSystem;
        const uint64_t elapsed = nowMonotonic - receiveMonotonic;
        return nowSystem >= elapsed ? nowSystem - elapsed : 0;
    }

    void ProcessRequest(
        IRadio& radio,
        const RadioPacketView& packet,
        uint32_t sequence,
        const RadioClockSynchronizationConfig& config
    ) {
        if (!IsReferenceMode(config.Mode) || packet.PayloadSize != RequestBytes) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const RadioAddress embeddedSource = DecodeRequestAddress(packet.Payload);
        if (!embeddedSource.IsValid()) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        RadioAddress responseDestination = embeddedSource;
        if (packet.Source.IsValid()) {
            if (packet.Source != embeddedSource) {
                _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            responseDestination = packet.Source;
        }

        const RadioCapabilities capabilities = radio.Capabilities();
        if (capabilities.AddressBytes != 0 && responseDestination.Length != capabilities.AddressBytes) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        _requestsReceived.fetch_add(1, std::memory_order_relaxed);
        const uint64_t t1 = ReadU64(packet.Payload + RequestTimestampOffset);
        const uint64_t t2 = RecoverSystemTimestamp(packet);

        uint8_t response[ResponseBytes]{};
        WriteU16(response, WireMagic);
        response[2] = WireVersion;
        response[3] = static_cast<uint8_t>(MessageType::Response);
        WriteU32(response + 4, sequence);
        WriteU64(response + 8, t1);
        WriteU64(response + 16, t2);
        const uint64_t t3 = _target->GetSynchronizationTimestampNanoseconds();
        WriteU64(response + 24, t3);

        const RadioSendResult sent = radio.Send(responseDestination, response, sizeof(response));
        if (sent) _responsesSent.fetch_add(1, std::memory_order_relaxed);
        else _sendFailures.fetch_add(1, std::memory_order_relaxed);
    }

    void ProcessResponse(
        const RadioPacketView& packet,
        uint32_t sequence,
        const RadioClockSynchronizationConfig& config
    ) {
        if (!IsClientMode(config.Mode) || packet.PayloadSize != ResponseBytes) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (
            packet.Source.IsValid() &&
            config.ReferencePeer.IsValid() &&
            packet.Source != config.ReferencePeer
        ) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const uint32_t pending = _pendingSequence.load(std::memory_order_acquire);
        if (pending == 0 || pending != sequence) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        _pendingSequence.store(0, std::memory_order_release);
        _responsesReceived.fetch_add(1, std::memory_order_relaxed);

        Timing::ClockSynchronizationSample<Timing::ClockTick> sample;
        sample.LocalRequestTransmitTime = ReadU64(packet.Payload + 8);
        sample.RemoteRequestReceiveTime = ReadU64(packet.Payload + 16);
        sample.RemoteResponseTransmitTime = ReadU64(packet.Payload + 24);
        sample.LocalResponseReceiveTime = RecoverSystemTimestamp(packet);

        const auto result = _target->SubmitSynchronizationSample(sample, config.AdjustmentMode);
        if (result.Accepted) _samplesAccepted.fetch_add(1, std::memory_order_relaxed);
        else _samplesRejected.fetch_add(1, std::memory_order_relaxed);
    }

public:
    explicit RadioClockSynchronizer(
        IRadio& radio,
        Timing::IClockSynchronizationTarget<Timing::ClockTick>* target = nullptr
    ) :
        _radio(&radio),
        _target(
            target == nullptr
                ? static_cast<Timing::IClockSynchronizationTarget<Timing::ClockTick>*>(
                    &Timing::SystemClock<>::GetInstance()
                  )
                : target
        ) {}

    ~RadioClockSynchronizer() override { Shutdown(); }

    RadioClockSynchronizer(const RadioClockSynchronizer&) = delete;
    RadioClockSynchronizer& operator=(const RadioClockSynchronizer&) = delete;
    RadioClockSynchronizer(RadioClockSynchronizer&&) = delete;
    RadioClockSynchronizer& operator=(RadioClockSynchronizer&&) = delete;

    bool Initialize(const RadioClockSynchronizationConfig& config) {
        Shutdown();
        if (_radio == nullptr || _target == nullptr) return false;

        const RadioCapabilities capabilities = _radio->Capabilities();
        if (capabilities.MaximumPayloadBytes < ResponseBytes) return false;
        if (config.RequireReceiveTimestamp && !capabilities.Has(RadioCapability::ReceiveTimestamp)) {
            return false;
        }
        if (IsClientMode(config.Mode)) {
            const RadioAddress localAddress = _radio->LocalAddress();
            if (!config.ReferencePeer.IsValid() || !localAddress.IsValid()) return false;
            if (
                capabilities.AddressBytes != 0 &&
                (config.ReferencePeer.Length != capabilities.AddressBytes ||
                 localAddress.Length != capabilities.AddressBytes)
            ) {
                return false;
            }
        }

        {
            std::lock_guard<System::Synchronization::Mutex> lock(_stateMutex);
            _config = config;
        }

        try {
            _packetSubscription = _radio->Observers().Subscribe<IRadioPacketObserver>(this);
        } catch (...) {
            _packetSubscription.reset();
            return false;
        }
        if (!_packetSubscription) return false;

        _pendingSequence.store(0, std::memory_order_release);
        _lastRequestMonotonicNanoseconds.store(0, std::memory_order_release);
        _initialized.store(true, std::memory_order_release);
        return true;
    }

    void Shutdown() noexcept {
        _initialized.store(false, std::memory_order_release);
        _pendingSequence.store(0, std::memory_order_release);
        _lastRequestMonotonicNanoseconds.store(0, std::memory_order_release);
        _packetSubscription.reset();
    }

    RadioSendResult RequestSynchronization() {
        _requestsAttempted.fetch_add(1, std::memory_order_relaxed);
        if (!_initialized.load(std::memory_order_acquire) || _radio == nullptr || !_radio->IsStarted()) {
            _sendFailures.fetch_add(1, std::memory_order_relaxed);
            return {RadioSendStatus::NotStarted, 0};
        }

        const RadioClockSynchronizationConfig config = ConfigSnapshot();
        const RadioAddress localAddress = _radio->LocalAddress();
        if (!IsClientMode(config.Mode) || !config.ReferencePeer.IsValid() || !localAddress.IsValid()) {
            _sendFailures.fetch_add(1, std::memory_order_relaxed);
            return {RadioSendStatus::InvalidAddress, 0};
        }

        uint32_t sequence = _nextSequence.fetch_add(1, std::memory_order_relaxed);
        if (sequence == 0) {
            sequence = _nextSequence.fetch_add(1, std::memory_order_relaxed);
            if (sequence == 0) sequence = 1;
        }

        uint8_t request[RequestBytes]{};
        WriteU16(request, WireMagic);
        request[2] = WireVersion;
        request[3] = static_cast<uint8_t>(MessageType::Request);
        WriteU32(request + 4, sequence);
        const uint64_t t1 = _target->GetSynchronizationTimestampNanoseconds();
        WriteU64(request + RequestTimestampOffset, t1);
        request[RequestAddressLengthOffset] = localAddress.Length;
        std::memcpy(request + RequestAddressOffset, localAddress.Bytes.data(), localAddress.Length);
        _pendingSequence.store(sequence, std::memory_order_release);

        _lastRequestMonotonicNanoseconds.store(
            System::Clock::Monotonic().NowNanoseconds(),
            std::memory_order_release
        );

        const RadioSendResult result = _radio->Send(config.ReferencePeer, request, sizeof(request));
        if (result) {
            _requestsSent.fetch_add(1, std::memory_order_relaxed);
        } else {
            _pendingSequence.store(0, std::memory_order_release);
            _sendFailures.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    void Update() {
        if (!_initialized.load(std::memory_order_acquire)) return;
        const RadioClockSynchronizationConfig config = ConfigSnapshot();
        if (!IsClientMode(config.Mode) || config.SynchronizationIntervalMilliseconds == 0) return;

        const uint64_t now = System::Clock::Monotonic().NowNanoseconds();
        const uint64_t interval =
            static_cast<uint64_t>(config.SynchronizationIntervalMilliseconds) * 1000000ULL;
        const uint64_t last = _lastRequestMonotonicNanoseconds.load(std::memory_order_acquire);
        if (last == 0 || now - last >= interval) (void)RequestSynchronization();
    }

    void OnRadioPacketReceived(IRadio& radio, const RadioPacketView& packet) override {
        if (!_initialized.load(std::memory_order_acquire) || &radio != _radio) return;

        MessageType type = MessageType::Request;
        uint32_t sequence = 0;
        if (!DecodeHeader(packet, type, sequence)) return;

        const RadioClockSynchronizationConfig config = ConfigSnapshot();
        if (config.RequireReceiveTimestamp && packet.ReceiveTimestampNanoseconds == 0) {
            _ignoredFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        if (type == MessageType::Request) ProcessRequest(radio, packet, sequence, config);
        else ProcessResponse(packet, sequence, config);
    }

    bool GetIsInitialized() const noexcept {
        return _initialized.load(std::memory_order_acquire);
    }

    RadioClockSynchronizationConfig GetConfig() const { return ConfigSnapshot(); }

    Timing::ClockSynchronizationStatus<Timing::ClockTick> GetSynchronizationStatus() const {
        return _target->GetSynchronizationStatus();
    }

    RadioClockSynchronizationStatistics GetStatistics() const noexcept {
        return {
            _requestsAttempted.load(std::memory_order_relaxed),
            _requestsSent.load(std::memory_order_relaxed),
            _requestsReceived.load(std::memory_order_relaxed),
            _responsesSent.load(std::memory_order_relaxed),
            _responsesReceived.load(std::memory_order_relaxed),
            _sendFailures.load(std::memory_order_relaxed),
            _ignoredFrames.load(std::memory_order_relaxed),
            _samplesAccepted.load(std::memory_order_relaxed),
            _samplesRejected.load(std::memory_order_relaxed),
            _timestampFallbacks.load(std::memory_order_relaxed)
        };
    }

    void ResetStatistics() noexcept {
        _requestsAttempted.store(0, std::memory_order_relaxed);
        _requestsSent.store(0, std::memory_order_relaxed);
        _requestsReceived.store(0, std::memory_order_relaxed);
        _responsesSent.store(0, std::memory_order_relaxed);
        _responsesReceived.store(0, std::memory_order_relaxed);
        _sendFailures.store(0, std::memory_order_relaxed);
        _ignoredFrames.store(0, std::memory_order_relaxed);
        _samplesAccepted.store(0, std::memory_order_relaxed);
        _samplesRejected.store(0, std::memory_order_relaxed);
        _timestampFallbacks.store(0, std::memory_order_relaxed);
    }
};

} // namespace ESPressio::Radio
