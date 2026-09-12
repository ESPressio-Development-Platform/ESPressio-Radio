#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "ESPressio_RadioServiceProfile.hpp"
#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

static constexpr std::uint8_t RadioTransportWireMagic0 = 0xE5u;
static constexpr std::uint8_t RadioTransportWireMagic1 = 0x52u;
static constexpr std::uint8_t RadioTransportWireVersion = 3u;
static constexpr std::size_t RadioTransportV3FixedHeaderBytes = 15u;

struct RadioTransportV3Header final {
    RadioTransferId TransferId{0};
    std::uint8_t FragmentIndex{0};
    std::uint8_t FragmentCount{0};
    std::uint16_t LogicalPayloadBytes{0};
    RadioAddress Source{};
    RadioServiceClass ServiceClass{RadioServiceClass::Invalid};
    std::uint32_t RemainingResidenceMilliseconds{0};
};

struct RadioTransportV3FragmentView final {
    RadioTransportV3Header Header{};
    const std::uint8_t* FragmentPayload{nullptr};
    std::size_t FragmentPayloadBytes{0};
    std::size_t HeaderBytes{0};
};

constexpr bool RadioTransportV3HeaderIsValid(const RadioTransportV3Header& header) noexcept {
    return header.TransferId != 0 && header.FragmentCount != 0 &&
           header.FragmentIndex < header.FragmentCount && header.LogicalPayloadBytes != 0 &&
           header.Source.IsValid() && IsValidRadioServiceClass(header.ServiceClass) &&
           header.RemainingResidenceMilliseconds != 0;
}

constexpr std::size_t RadioTransportV3HeaderBytes(std::uint8_t sourceAddressBytes) noexcept {
    return sourceAddressBytes == 0 || sourceAddressBytes > MaximumRadioAddressBytes
        ? 0u : RadioTransportV3FixedHeaderBytes + sourceAddressBytes;
}

inline std::size_t RadioTransportV3HeaderBytes(const RadioAddress& source) noexcept {
    return source.IsValid() ? RadioTransportV3HeaderBytes(source.Length) : 0u;
}

constexpr std::size_t RadioTransportV3MaximumFragmentPayload(
    std::size_t physicalPayloadBytes,
    std::uint8_t sourceAddressBytes) noexcept {
    const auto header = RadioTransportV3HeaderBytes(sourceAddressBytes);
    return header == 0 || physicalPayloadBytes <= header ? 0u : physicalPayloadBytes - header;
}

constexpr std::size_t RadioTransportV3MaximumLogicalPayload(
    std::size_t physicalPayloadBytes,
    std::uint8_t sourceAddressBytes,
    std::size_t providerLogicalMaximum = 0) noexcept {
    const auto fragment = RadioTransportV3MaximumFragmentPayload(physicalPayloadBytes, sourceAddressBytes);
    if (fragment == 0) return 0;
    std::size_t maximum = fragment * 255u;
    if (maximum > std::numeric_limits<std::uint16_t>::max())
        maximum = std::numeric_limits<std::uint16_t>::max();
    if (providerLogicalMaximum != 0 && providerLogicalMaximum < maximum)
        maximum = providerLogicalMaximum;
    return maximum;
}

constexpr bool TryEncodeRemainingResidenceMilliseconds(
    std::uint64_t expiryNanoseconds,
    std::uint64_t nowNanoseconds,
    std::uint32_t& encoded) noexcept {
    encoded = 0;
    if (expiryNanoseconds <= nowNanoseconds) return false;
    const auto remaining = expiryNanoseconds - nowNanoseconds;
    if (remaining < 1'000'000ULL) return false;
    const auto milliseconds = remaining / 1'000'000ULL;
    if (milliseconds == 0 || milliseconds > std::numeric_limits<std::uint32_t>::max()) return false;
    encoded = static_cast<std::uint32_t>(milliseconds);
    return true;
}

inline void WriteRadioU16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFu);
    destination[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

inline void WriteRadioU32(std::uint8_t* destination, std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFu);
    destination[1] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    destination[2] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    destination[3] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

inline std::uint16_t ReadRadioU16(const std::uint8_t* source) noexcept {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8u);
}

inline std::uint32_t ReadRadioU32(const std::uint8_t* source) noexcept {
    return static_cast<std::uint32_t>(source[0]) |
           (static_cast<std::uint32_t>(source[1]) << 8u) |
           (static_cast<std::uint32_t>(source[2]) << 16u) |
           (static_cast<std::uint32_t>(source[3]) << 24u);
}

inline bool EncodeRadioTransportV3Fragment(
    const RadioTransportV3Header& header,
    const std::uint8_t* fragmentPayload,
    std::size_t fragmentPayloadBytes,
    std::uint8_t* output,
    std::size_t outputCapacity,
    std::size_t& encodedBytes) noexcept {
    encodedBytes = 0;
    if (!RadioTransportV3HeaderIsValid(header) || output == nullptr ||
        (fragmentPayloadBytes != 0 && fragmentPayload == nullptr)) return false;
    const auto headerBytes = RadioTransportV3HeaderBytes(header.Source);
    if (headerBytes == 0 || outputCapacity < headerBytes || fragmentPayloadBytes > outputCapacity - headerBytes ||
        fragmentPayloadBytes > header.LogicalPayloadBytes) return false;
    output[0] = RadioTransportWireMagic0;
    output[1] = RadioTransportWireMagic1;
    output[2] = RadioTransportWireVersion;
    WriteRadioU16(output + 3, header.TransferId);
    output[5] = header.FragmentIndex;
    output[6] = header.FragmentCount;
    WriteRadioU16(output + 7, header.LogicalPayloadBytes);
    output[9] = header.Source.Length;
    output[10] = static_cast<std::uint8_t>(header.ServiceClass);
    WriteRadioU32(output + 11, header.RemainingResidenceMilliseconds);
    std::memcpy(output + RadioTransportV3FixedHeaderBytes, header.Source.Bytes.data(), header.Source.Length);
    if (fragmentPayloadBytes != 0)
        std::memcpy(output + headerBytes, fragmentPayload, fragmentPayloadBytes);
    encodedBytes = headerBytes + fragmentPayloadBytes;
    return true;
}

inline bool DecodeRadioTransportV3Fragment(
    const std::uint8_t* input,
    std::size_t inputBytes,
    RadioTransportV3FragmentView& decoded) noexcept {
    decoded = {};
    if (input == nullptr || inputBytes < RadioTransportV3FixedHeaderBytes ||
        input[0] != RadioTransportWireMagic0 || input[1] != RadioTransportWireMagic1 ||
        input[2] != RadioTransportWireVersion) return false;
    RadioTransportV3Header header{};
    header.TransferId = ReadRadioU16(input + 3);
    header.FragmentIndex = input[5];
    header.FragmentCount = input[6];
    header.LogicalPayloadBytes = ReadRadioU16(input + 7);
    const auto sourceLength = input[9];
    header.ServiceClass = static_cast<RadioServiceClass>(input[10]);
    header.RemainingResidenceMilliseconds = ReadRadioU32(input + 11);
    const auto headerBytes = RadioTransportV3HeaderBytes(sourceLength);
    if (headerBytes == 0 || inputBytes < headerBytes) return false;
    header.Source = RadioAddress::FromBytes(input + RadioTransportV3FixedHeaderBytes, sourceLength);
    if (!RadioTransportV3HeaderIsValid(header)) return false;
    const auto payloadBytes = inputBytes - headerBytes;
    if (payloadBytes > header.LogicalPayloadBytes) return false;
    decoded.Header = header;
    decoded.FragmentPayload = input + headerBytes;
    decoded.FragmentPayloadBytes = payloadBytes;
    decoded.HeaderBytes = headerBytes;
    return true;
}

constexpr bool RadioTransportV3ImmutableMetadataMatches(
    const RadioTransportV3Header& established,
    const RadioTransportV3Header& candidate) noexcept {
    return established.TransferId == candidate.TransferId &&
           established.FragmentCount == candidate.FragmentCount &&
           established.LogicalPayloadBytes == candidate.LogicalPayloadBytes &&
           established.Source == candidate.Source &&
           established.ServiceClass == candidate.ServiceClass;
}

} // namespace ESPressio::Radio
