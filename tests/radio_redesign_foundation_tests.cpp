#include <ESPressio_RadioProviderContract.hpp>
#include <ESPressio_RadioTransferId.hpp>
#include <ESPressio_RadioWireV3.hpp>

#include <array>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Radio;

static_assert(static_cast<std::uint8_t>(RadioServiceClass::Invalid) == 0);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::Infrastructure) == 1);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::Clock) == 2);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::Critical) == 3);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::Responsive) == 4);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::Convergent) == 5);
static_assert(static_cast<std::uint8_t>(RadioServiceClass::BestEffort) == 6);
static_assert(RadioTransportV3FixedHeaderBytes == 15);
static_assert(RadioTransportV3MaximumLogicalPayload(32, 5, 4096) == 3060);
static_assert(RadioTransportV3MaximumLogicalPayload(26, 6, 4096) == 1275);

int main() {
    const RadioServiceProfile expiryOnly{
        RadioServiceClass::BestEffort,
        RadioDeadlineTreatment::ExpiryOnly,
        RadioDirectLinkEvidenceRequirement::TransmissionCompletion};
    assert(expiryOnly.IsValid());
    assert((RadioTransferTiming{10, 0}).IsValidFor(expiryOnly));
    assert(!(RadioTransferTiming{10, 9}).IsValidFor(expiryOnly));

    const RadioServiceProfile promotable{
        RadioServiceClass::Clock,
        RadioDeadlineTreatment::Promotable,
        RadioDirectLinkEvidenceRequirement::PeerAcknowledgement};
    assert((RadioTransferTiming{100, 90}).IsValidFor(promotable));
    assert(!(RadioTransferTiming{100, 101}).IsValidFor(promotable));

    const RadioTransmissionCost relative{1, 0, RadioCostEstimateQuality::RelativeOnly};
    const RadioTransmissionCost airtime{2, 5000, RadioCostEstimateQuality::ConservativeAirtime};
    assert(relative.IsValid() && !relative.SupportsPromotableDeadline());
    assert(airtime.SupportsPromotableDeadline());

    RadioReceiveTimestampEvidence finite{};
    finite.ProviderCaptureCoordinate = 77;
    finite.MonotonicNanoseconds = 1000;
    finite.ConservativeUncertaintyNanoseconds = 250;
    finite.ContinuityGeneration = 3;
    finite.Source = RadioTimestampCaptureSource::Driver;
    finite.Quality = RadioTimestampQuality::FiniteBounded;
    assert(finite.HasFiniteBound());
    assert(!finite.IsCertifiedCandidate());
    assert(finite.CapturedSystemNanoseconds() == 0);

    finite.CaptureModel.AnchorMonotonic = 900;
    finite.CaptureModel.AnchorTime = 5000;
    finite.CaptureModel.AnchorUncertainty = ESPressio::Timing::ClockUncertainty::Known(100);
    finite.HasCaptureModel = true;
    assert(finite.IsCertifiedCandidate());
    assert(finite.CapturedSystemNanoseconds() == 5100);

    const RadioReceiveTimestampEvidence unknown{};
    assert(!unknown.HasFiniteBound() && !unknown.IsCertifiedCandidate());

    const auto completed = RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement();
    const auto acked = RadioDirectLinkEvidence::CompletedAndAcknowledged();
    assert(SatisfiesDirectLinkEvidence(completed, RadioDirectLinkEvidenceRequirement::TransmissionCompletion));
    assert(!SatisfiesDirectLinkEvidence(completed, RadioDirectLinkEvidenceRequirement::PeerAcknowledgement));
    assert(SatisfiesDirectLinkEvidence(acked, RadioDirectLinkEvidenceRequirement::PeerAcknowledgement));

    const std::uint8_t sourceBytes[]{0xAA, 0xBB};
    const auto source = RadioAddress::FromBytes(sourceBytes, 2);
    RadioTransportV3Header header{
        0x1234,
        1,
        3,
        0x0201,
        source,
        RadioServiceClass::Critical,
        0x01020304};
    const std::uint8_t fragment[]{0xCC, 0xDD};
    std::array<std::uint8_t, 32> encoded{};
    std::size_t encodedBytes = 0;
    assert(EncodeRadioTransportV3Fragment(
        header, fragment, sizeof(fragment), encoded.data(), encoded.size(), encodedBytes));
    const std::array<std::uint8_t, 19> expected{
        0xE5,0x52,0x03,0x34,0x12,0x01,0x03,0x01,0x02,0x02,0x03,0x04,0x03,0x02,0x01,
        0xAA,0xBB,0xCC,0xDD};
    assert(encodedBytes == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) assert(encoded[i] == expected[i]);

    RadioTransportV3FragmentView decoded{};
    assert(DecodeRadioTransportV3Fragment(encoded.data(), encodedBytes, decoded));
    assert(decoded.Header.TransferId == header.TransferId);
    assert(decoded.Header.FragmentIndex == 1 && decoded.Header.FragmentCount == 3);
    assert(decoded.Header.LogicalPayloadBytes == 0x0201);
    assert(decoded.Header.Source == source);
    assert(decoded.Header.ServiceClass == RadioServiceClass::Critical);
    assert(decoded.Header.RemainingResidenceMilliseconds == 0x01020304);
    assert(decoded.FragmentPayloadBytes == 2 && decoded.FragmentPayload[0] == 0xCC && decoded.FragmentPayload[1] == 0xDD);

    auto changed = header;
    changed.ServiceClass = RadioServiceClass::Clock;
    assert(!RadioTransportV3ImmutableMetadataMatches(header, changed));
    changed = header;
    changed.RemainingResidenceMilliseconds -= 1;
    assert(RadioTransportV3ImmutableMetadataMatches(header, changed));

    std::uint32_t residence = 0;
    assert(TryEncodeRemainingResidenceMilliseconds(5'500'000, 2'000'000, residence));
    assert(residence == 3);
    assert(!TryEncodeRemainingResidenceMilliseconds(2'999'999, 2'000'000, residence));

    RadioTransferIdIssuer<4> issuer;
    RadioTransferId first = 0;
    assert(issuer.TryIssue([](RadioTransferId) noexcept { return false; }, first));
    assert(first == 1);
    issuer.RememberCompleted(first);
    assert(issuer.WasRecentlyCompleted(first));
    RadioTransferId second = 0;
    assert(issuer.TryIssue([&](RadioTransferId id) noexcept { return id == 2; }, second));
    assert(second == 3);

    return 0;
}
