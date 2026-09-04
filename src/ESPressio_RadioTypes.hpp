#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ESPressio::Radio {

/// <summary>Opaque identifier used only to correlate one Radio-owned logical link transfer.</summary>
using RadioTransferId = uint16_t;

static constexpr std::size_t MaximumRadioAddressBytes = 8;

/// <summary>Opaque link-layer address understood only by a concrete radio provider.</summary>
struct RadioAddress {
    std::array<uint8_t, MaximumRadioAddressBytes> Bytes{};
    uint8_t Length = 0;

    constexpr bool IsValid() const noexcept { return Length > 0 && Length <= MaximumRadioAddressBytes; }
    bool IsBroadcast() const noexcept {
        if (!IsValid()) return false;
        for (uint8_t i = 0; i < Length; ++i) if (Bytes[i] != 0xFFu) return false;
        return true;
    }
    static RadioAddress FromBytes(const uint8_t* bytes, uint8_t length) noexcept {
        RadioAddress result;
        if (bytes == nullptr || length == 0 || length > MaximumRadioAddressBytes) return result;
        result.Length = length;
        std::memcpy(result.Bytes.data(), bytes, length);
        return result;
    }
    static RadioAddress Broadcast(uint8_t length) noexcept {
        RadioAddress result;
        if (length == 0 || length > MaximumRadioAddressBytes) return result;
        result.Length = length;
        for (uint8_t i = 0; i < length; ++i) result.Bytes[i] = 0xFFu;
        return result;
    }
    bool operator==(const RadioAddress& other) const noexcept {
        return Length == other.Length && (Length == 0 || std::memcmp(Bytes.data(), other.Bytes.data(), Length) == 0);
    }
    bool operator!=(const RadioAddress& other) const noexcept { return !(*this == other); }
    bool operator<(const RadioAddress& other) const noexcept {
        const uint8_t common = Length < other.Length ? Length : other.Length;
        const int comparison = common == 0 ? 0 : std::memcmp(Bytes.data(), other.Bytes.data(), common);
        if (comparison < 0) return true;
        if (comparison > 0) return false;
        return Length < other.Length;
    }
};

/// <summary>Generation-safe process-local handle to one Radio-owned directly reachable peer binding.</summary>
struct RadioPeerHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};
    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const RadioPeerHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
    constexpr bool operator!=(const RadioPeerHandle& other) const noexcept { return !(*this == other); }
};
static_assert(sizeof(RadioPeerHandle) == 4, "RadioPeerHandle must remain a compact slot+generation value.");

enum class RadioCapability : uint32_t {
    None = 0,
    Broadcast = 1u << 0,
    LinkAcknowledgement = 1u << 1,
    LinkRetries = 1u << 2,
    Rssi = 1u << 3,
    ChannelSelection = 1u << 4,
    DataRateSelection = 1u << 5,
    TransmitPower = 1u << 6,
    HardwareAddressing = 1u << 7,
    ReceiveTimestamp = 1u << 8,
    TransmitTimestamp = 1u << 9,
    CarrierSense = 1u << 10,
    LinkEncryption = 1u << 11
};
constexpr RadioCapability operator|(RadioCapability a, RadioCapability b) noexcept {
    return static_cast<RadioCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr RadioCapability operator&(RadioCapability a, RadioCapability b) noexcept {
    return static_cast<RadioCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// <summary>Describes bounded facilities supplied by one concrete radio provider.</summary>
struct RadioCapabilities {
    RadioCapability Flags = RadioCapability::None;
    uint16_t MaximumPayloadBytes = 0;
    uint8_t AddressBytes = 0;
    uint16_t MaximumLogicalTransferBytes = 0;
    constexpr bool Has(RadioCapability capability) const noexcept {
        return (static_cast<uint32_t>(Flags) & static_cast<uint32_t>(capability)) == static_cast<uint32_t>(capability);
    }
};

enum class RadioPacketFlag : uint8_t {
    None = 0,
    Broadcast = 1u << 0,
    LinkAcknowledged = 1u << 1
};
constexpr RadioPacketFlag operator|(RadioPacketFlag a, RadioPacketFlag b) noexcept {
    return static_cast<RadioPacketFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool HasFlag(RadioPacketFlag flags, RadioPacketFlag flag) noexcept {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) == static_cast<uint8_t>(flag);
}

/// <summary>Borrowed physical/link packet view delivered synchronously by a concrete radio provider.</summary>
struct RadioPacketView {
    RadioAddress Source{};
    RadioAddress Destination{};
    const uint8_t* Payload = nullptr;
    std::size_t PayloadSize = 0;
    int16_t RssiDbm = 0;
    uint64_t ReceiveTimestampNanoseconds = 0;
    RadioPacketFlag Flags = RadioPacketFlag::None;
};

/// <summary>Whether a provider can prove that an accepted packet transmission actually completed.</summary>
enum class RadioTransmissionCompletion : uint8_t {
    Unknown,
    Completed
};

/// <summary>Qualified peer-acknowledgement evidence for one completed direct-link transmission.</summary>
enum class RadioPeerAcknowledgement : uint8_t {
    Unavailable,
    Unknown,
    Acknowledged
};

/// <summary>
/// Technology-independent direct-link evidence established by the provider for one Send operation.
/// </summary>
/// <remarks>
/// Evidence is deliberately separate from admission. An Accepted send with Unknown completion means only that the
/// provider accepted/submitted the packet. Completed means the provider can prove physical/link transmission completed.
/// Acknowledged is stronger still and is valid only when the technology obtained a peer/link acknowledgement. Absence of
/// acknowledgement support is Unavailable, never silently treated as failure or success.
/// </remarks>
struct RadioDirectLinkEvidence final {
    RadioTransmissionCompletion Transmission{RadioTransmissionCompletion::Unknown};
    RadioPeerAcknowledgement PeerAcknowledgement{RadioPeerAcknowledgement::Unavailable};

    constexpr bool TransmissionCompleted() const noexcept {
        return Transmission == RadioTransmissionCompletion::Completed;
    }
    constexpr bool PeerAcknowledged() const noexcept {
        return PeerAcknowledgement == RadioPeerAcknowledgement::Acknowledged;
    }

    static constexpr RadioDirectLinkEvidence CompletedWithoutPeerAcknowledgement() noexcept {
        return {RadioTransmissionCompletion::Completed, RadioPeerAcknowledgement::Unavailable};
    }
    static constexpr RadioDirectLinkEvidence CompletedAndAcknowledged() noexcept {
        return {RadioTransmissionCompletion::Completed, RadioPeerAcknowledgement::Acknowledged};
    }
};

enum class RadioSendStatus : uint8_t {
    Accepted,
    NotStarted,
    InvalidAddress,
    PayloadTooLarge,
    Busy,
    NoMemory,
    Unsupported,
    NativeFailure
};

/// <summary>Immediate admission/result plus any direct-link evidence synchronously established by the provider.</summary>
struct RadioSendResult {
    RadioSendStatus Status = RadioSendStatus::NativeFailure;
    int32_t NativeError = 0;
    RadioDirectLinkEvidence Evidence{};

    constexpr explicit operator bool() const noexcept { return Status == RadioSendStatus::Accepted; }
    static constexpr RadioSendResult Accepted(RadioDirectLinkEvidence evidence = {}) noexcept {
        return {RadioSendStatus::Accepted, 0, evidence};
    }
};

} // namespace ESPressio::Radio
