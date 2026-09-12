#include <ESPressio_RadioClockWireV1.hpp>
#include <ESPressio_RadioProviderContract.hpp>

#include <array>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Radio;

int main(){
    static_assert(RadioClockWireV1::MaximumRequestBytes==17u);
    static_assert(RadioClockWireV1::ResponseBytes==32u);

    const std::uint8_t addressBytes[5]{1,2,3,4,5};
    RadioClockRequestV1 request{0x11223344u,RadioAddress::FromBytes(addressBytes,5)};
    std::array<std::uint8_t,RadioClockWireV1::MaximumRequestBytes> requestWire{};
    std::size_t requestBytes=0;
    assert(EncodeRadioClockRequestV1(request,requestWire.data(),requestWire.size(),requestBytes));
    assert(requestBytes==14u);
    RadioClockRequestV1 decodedRequest{};
    assert(DecodeRadioClockRequestV1(requestWire.data(),requestBytes,decodedRequest));
    assert(decodedRequest.Sequence==request.Sequence&&decodedRequest.ReplyAddress==request.ReplyAddress);
    assert(!DecodeRadioClockRequestV1(requestWire.data(),requestBytes-1u,decodedRequest));

    RadioClockResponseV1 response{};
    response.Sequence=0x55667788u;
    response.T2SystemNanoseconds=0x0102030405060708ULL;
    response.RemoteSystemProcessingNanoseconds=0x11223344u;
    response.RemoteMonotonicProcessingNanoseconds=0x55667788u;
    response.ReferenceReliability=ESPressio::Timing::TimeReliability::Synchronized;
    response.CaptureQuality=RadioClockCaptureQuality::Hardware;
    response.ReferenceUncertainty=ESPressio::Timing::ClockUncertainty::Known(450'000u);
    response.CaptureUncertainty=ESPressio::Timing::ClockUncertainty::Known(12'345u);
    std::array<std::uint8_t,RadioClockWireV1::ResponseBytes> responseWire{};
    assert(EncodeRadioClockResponseV1(response,responseWire.data(),responseWire.size()));
    RadioClockResponseV1 decodedResponse{};
    assert(DecodeRadioClockResponseV1(responseWire.data(),responseWire.size(),decodedResponse));
    assert(decodedResponse.Sequence==response.Sequence);
    assert(decodedResponse.T2SystemNanoseconds==response.T2SystemNanoseconds);
    assert(decodedResponse.RemoteSystemProcessingNanoseconds==response.RemoteSystemProcessingNanoseconds);
    assert(decodedResponse.RemoteMonotonicProcessingNanoseconds==response.RemoteMonotonicProcessingNanoseconds);
    assert(decodedResponse.ReferenceReliability==response.ReferenceReliability);
    assert(decodedResponse.CaptureQuality==response.CaptureQuality);
    assert(decodedResponse.ReferenceUncertainty.IsKnown&&decodedResponse.ReferenceUncertainty.Nanoseconds==450'000u);
    assert(decodedResponse.CaptureUncertainty.IsKnown&&decodedResponse.CaptureUncertainty.Nanoseconds==12'345u);

    // Exact vector proves the 32-byte packing and independent System/monotonic remote processing durations.
    assert(responseWire[0]==0x52&&responseWire[1]==0x43&&responseWire[2]==1&&responseWire[3]==2);
    assert(responseWire[16]==0x44&&responseWire[17]==0x33&&responseWire[18]==0x22&&responseWire[19]==0x11);
    assert(responseWire[20]==0x88&&responseWire[21]==0x77&&responseWire[22]==0x66&&responseWire[23]==0x55);

    response.ReferenceUncertainty={};
    assert(EncodeRadioClockResponseV1(response,responseWire.data(),responseWire.size()));
    assert(DecodeRadioClockResponseV1(responseWire.data(),responseWire.size(),decodedResponse));
    assert(!decodedResponse.ReferenceUncertainty.IsKnown);
    response.CaptureUncertainty=ESPressio::Timing::ClockUncertainty::Known(RadioClockWireV1::UnknownUncertainty24);
    assert(!EncodeRadioClockResponseV1(response,responseWire.data(),responseWire.size()));

    ESPressio::Timing::ClockModelSnapshot model{};
    model.AnchorMonotonic=100;
    model.AnchorTime=1000;
    model.AnchorUncertainty=ESPressio::Timing::ClockUncertainty::Known(50);
    RadioReceiveTimestampEvidence evidence{};
    evidence.ProviderCaptureCoordinate=77;
    evidence.MonotonicNanoseconds=150;
    evidence.ConservativeUncertaintyNanoseconds=25;
    evidence.ContinuityGeneration=3;
    evidence.Source=RadioTimestampCaptureSource::Hardware;
    evidence.Quality=RadioTimestampQuality::FiniteBounded;
    evidence.CaptureModel=model;
    evidence.HasCaptureModel=true;
    assert(evidence.IsCertifiedCandidate());
    assert(evidence.CapturedSystemNanoseconds()==1050);

    evidence.HasCaptureModel=false;
    assert(!evidence.IsCertifiedCandidate());
    assert(evidence.CapturedSystemNanoseconds()==0);
}
