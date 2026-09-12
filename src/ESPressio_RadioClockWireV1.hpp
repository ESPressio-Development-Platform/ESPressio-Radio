#pragma once

#include <cstddef>
#include <cstdint>

#include <ESPressio_ClockUncertainty.hpp>
#include <ESPressio_TimeReliability.hpp>

#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

/// <summary>Compact single-physical-frame K1/K2 direct-Radio synchronization wire.</summary>
/// <remarks>
/// The requester retains T1 and the selected reference identity locally, so neither is redundantly returned on wire.
/// A response carries correlation, reference-side T2 System time, exact bounded T2->T3 System and raw-monotonic
/// processing durations, plus reference/capture quality facts. Reconstructing T3 from the transmitted System duration
/// preserves the four-System-timestamp equation, while the separate monotonic duration preserves K1 structural
/// chronology without fabricating a remote monotonic coordinate. The exact response remains 32 bytes.
/// </remarks>
struct RadioClockWireV1 final {
    static constexpr std::uint16_t Magic = 0x4352u; // little-endian "RC"
    static constexpr std::uint8_t Version = 1u;
    static constexpr std::size_t HeaderBytes = 8u;
    static constexpr std::size_t MaximumRequestBytes = HeaderBytes + 1u + MaximumRadioAddressBytes;
    static constexpr std::size_t ResponseBytes = 32u;
    static constexpr std::uint32_t UnknownUncertainty24 = 0x00FFFFFFu;
    static constexpr std::uint32_t MaximumKnownUncertainty24 = UnknownUncertainty24 - 1u;
};
static_assert(RadioClockWireV1::MaximumRequestBytes == 17u,
              "Clock request must remain compact for the smallest certified provider");
static_assert(RadioClockWireV1::ResponseBytes == 32u,
              "Clock response must remain one nRF24-class physical packet");

enum class RadioClockMessageType : std::uint8_t { Request = 1, Response = 2 };
/// <summary>Wire quality label mapped to Timing capture quality only by the Clock orchestration layer.</summary>
enum class RadioClockCaptureQuality : std::uint8_t {
    Invalid = 0,
    Hardware = 1,
    SoftwareBounded = 2,
    SoftwareUnbounded = 3
};

struct RadioClockRequestV1 final {
    std::uint32_t Sequence{0};
    RadioAddress ReplyAddress{};
};

struct RadioClockResponseV1 final {
    std::uint32_t Sequence{0};
    std::uint64_t T2SystemNanoseconds{0};
    std::uint32_t RemoteSystemProcessingNanoseconds{0};
    std::uint32_t RemoteMonotonicProcessingNanoseconds{0};
    Timing::TimeReliability ReferenceReliability{Timing::TimeReliability::Unqualified};
    RadioClockCaptureQuality CaptureQuality{RadioClockCaptureQuality::Invalid};
    Timing::ClockUncertainty ReferenceUncertainty{};
    Timing::ClockUncertainty CaptureUncertainty{};
};

namespace Detail {
inline std::uint16_t ReadClockU16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8u);
}
inline std::uint32_t ReadClockU24(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u);
}
inline std::uint32_t ReadClockU32(const std::uint8_t* p) noexcept {
    std::uint32_t value=0;
    for(std::size_t i=0;i<4;++i) value|=static_cast<std::uint32_t>(p[i])<<(8u*i);
    return value;
}
inline std::uint64_t ReadClockU64(const std::uint8_t* p) noexcept {
    std::uint64_t value=0;
    for(std::size_t i=0;i<8;++i) value|=static_cast<std::uint64_t>(p[i])<<(8u*i);
    return value;
}
inline void WriteClockU16(std::uint8_t* p,std::uint16_t value) noexcept {
    p[0]=static_cast<std::uint8_t>(value&0xffu);
    p[1]=static_cast<std::uint8_t>((value>>8u)&0xffu);
}
inline void WriteClockU24(std::uint8_t* p,std::uint32_t value) noexcept {
    p[0]=static_cast<std::uint8_t>(value&0xffu);
    p[1]=static_cast<std::uint8_t>((value>>8u)&0xffu);
    p[2]=static_cast<std::uint8_t>((value>>16u)&0xffu);
}
inline void WriteClockU32(std::uint8_t* p,std::uint32_t value) noexcept {
    for(std::size_t i=0;i<4;++i) p[i]=static_cast<std::uint8_t>((value>>(8u*i))&0xffu);
}
inline void WriteClockU64(std::uint8_t* p,std::uint64_t value) noexcept {
    for(std::size_t i=0;i<8;++i) p[i]=static_cast<std::uint8_t>((value>>(8u*i))&0xffu);
}
inline bool EncodeClockUncertainty24(const Timing::ClockUncertainty& value,std::uint32_t& encoded) noexcept {
    if(!value.IsKnown){encoded=RadioClockWireV1::UnknownUncertainty24;return true;}
    if(value.Nanoseconds>RadioClockWireV1::MaximumKnownUncertainty24)return false;
    encoded=static_cast<std::uint32_t>(value.Nanoseconds);return true;
}
inline Timing::ClockUncertainty DecodeClockUncertainty24(std::uint32_t encoded) noexcept {
    return encoded==RadioClockWireV1::UnknownUncertainty24
        ? Timing::ClockUncertainty{}
        : Timing::ClockUncertainty::Known(encoded);
}
inline bool ValidClockCaptureQuality(RadioClockCaptureQuality value) noexcept {
    return value==RadioClockCaptureQuality::Hardware ||
           value==RadioClockCaptureQuality::SoftwareBounded ||
           value==RadioClockCaptureQuality::SoftwareUnbounded;
}
inline bool EncodeClockHeader(
    std::uint8_t* output,std::size_t capacity,RadioClockMessageType type,std::uint32_t sequence) noexcept {
    if(!output||capacity<RadioClockWireV1::HeaderBytes||sequence==0)return false;
    WriteClockU16(output,RadioClockWireV1::Magic);
    output[2]=RadioClockWireV1::Version;
    output[3]=static_cast<std::uint8_t>(type);
    WriteClockU32(output+4,sequence);
    return true;
}
inline bool DecodeClockHeader(
    const std::uint8_t* input,std::size_t bytes,RadioClockMessageType expected,std::uint32_t& sequence) noexcept {
    if(!input||bytes<RadioClockWireV1::HeaderBytes||ReadClockU16(input)!=RadioClockWireV1::Magic||
       input[2]!=RadioClockWireV1::Version||input[3]!=static_cast<std::uint8_t>(expected))return false;
    sequence=ReadClockU32(input+4);
    return sequence!=0;
}
} // namespace Detail

inline bool EncodeRadioClockRequestV1(
    const RadioClockRequestV1& request,std::uint8_t* output,std::size_t capacity,std::size_t& written) noexcept {
    written=0;
    if(!request.ReplyAddress.IsValid())return false;
    const auto needed=RadioClockWireV1::HeaderBytes+1u+request.ReplyAddress.Length;
    if(capacity<needed||!Detail::EncodeClockHeader(output,capacity,RadioClockMessageType::Request,request.Sequence))return false;
    output[8]=request.ReplyAddress.Length;
    for(std::size_t i=0;i<request.ReplyAddress.Length;++i)output[9+i]=request.ReplyAddress.Bytes[i];
    written=needed;return true;
}

inline bool DecodeRadioClockRequestV1(
    const std::uint8_t* input,std::size_t bytes,RadioClockRequestV1& request) noexcept {
    std::uint32_t sequence=0;
    if(!Detail::DecodeClockHeader(input,bytes,RadioClockMessageType::Request,sequence)||bytes<9)return false;
    const auto length=input[8];
    if(length==0||length>MaximumRadioAddressBytes||bytes!=9u+length)return false;
    const auto address=RadioAddress::FromBytes(input+9,length);
    if(!address.IsValid())return false;
    request={sequence,address};return true;
}

inline bool EncodeRadioClockResponseV1(
    const RadioClockResponseV1& response,std::uint8_t* output,std::size_t capacity) noexcept {
    if(capacity<RadioClockWireV1::ResponseBytes||!Timing::IsValidTimeReliability(response.ReferenceReliability)||
       !Detail::ValidClockCaptureQuality(response.CaptureQuality))return false;
    std::uint32_t referenceUncertainty=0,captureUncertainty=0;
    if(!Detail::EncodeClockUncertainty24(response.ReferenceUncertainty,referenceUncertainty)||
       !Detail::EncodeClockUncertainty24(response.CaptureUncertainty,captureUncertainty)||
       !Detail::EncodeClockHeader(output,capacity,RadioClockMessageType::Response,response.Sequence))return false;
    Detail::WriteClockU64(output+8,response.T2SystemNanoseconds);
    Detail::WriteClockU32(output+16,response.RemoteSystemProcessingNanoseconds);
    Detail::WriteClockU32(output+20,response.RemoteMonotonicProcessingNanoseconds);
    output[24]=static_cast<std::uint8_t>(response.ReferenceReliability);
    output[25]=static_cast<std::uint8_t>(response.CaptureQuality);
    Detail::WriteClockU24(output+26,referenceUncertainty);
    Detail::WriteClockU24(output+29,captureUncertainty);
    return true;
}

inline bool DecodeRadioClockResponseV1(
    const std::uint8_t* input,std::size_t bytes,RadioClockResponseV1& response) noexcept {
    if(bytes!=RadioClockWireV1::ResponseBytes)return false;
    std::uint32_t sequence=0;
    if(!Detail::DecodeClockHeader(input,bytes,RadioClockMessageType::Response,sequence))return false;
    const auto reliability=static_cast<Timing::TimeReliability>(input[24]);
    const auto quality=static_cast<RadioClockCaptureQuality>(input[25]);
    if(!Timing::IsValidTimeReliability(reliability)||!Detail::ValidClockCaptureQuality(quality))return false;
    response.Sequence=sequence;
    response.T2SystemNanoseconds=Detail::ReadClockU64(input+8);
    response.RemoteSystemProcessingNanoseconds=Detail::ReadClockU32(input+16);
    response.RemoteMonotonicProcessingNanoseconds=Detail::ReadClockU32(input+20);
    response.ReferenceReliability=reliability;
    response.CaptureQuality=quality;
    response.ReferenceUncertainty=Detail::DecodeClockUncertainty24(Detail::ReadClockU24(input+26));
    response.CaptureUncertainty=Detail::DecodeClockUncertainty24(Detail::ReadClockU24(input+29));
    return true;
}

} // namespace ESPressio::Radio
