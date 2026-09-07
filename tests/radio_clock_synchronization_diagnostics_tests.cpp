#include <cassert>
#include <cstdint>

#include <ESPressio_RadioClockSynchronizationDiagnostics.hpp>

using namespace ESPressio;

class FakeSynchronizationTarget final
    : public Timing::IClockSynchronizationTarget<Timing::ClockTick> {
public:
    Timing::ClockSynchronizationConfig Config{};
    bool AcceptNext{false};

    Timing::ClockTick GetSynchronizationTimestampNanoseconds() const override {
        return 0U;
    }

    Timing::ClockSynchronizationResult<Timing::ClockTick> SubmitSynchronizationSample(
        const Timing::ClockSynchronizationSample<Timing::ClockTick>&,
        Timing::ClockSynchronizationAdjustmentMode
    ) override {
        Timing::ClockSynchronizationResult<Timing::ClockTick> result{};
        result.Accepted = AcceptNext;
        return result;
    }

    Timing::ClockSynchronizationStatus<Timing::ClockTick> GetSynchronizationStatus() const override {
        return {};
    }

    void ConfigureSynchronization(const Timing::ClockSynchronizationConfig& config) override {
        Config = config;
    }

    Timing::ClockSynchronizationConfig GetSynchronizationConfig() const override {
        return Config;
    }

    void ResetSynchronization() override {}
};

static Timing::ClockSynchronizationSample<Timing::ClockTick> Baseline() {
    Timing::ClockSynchronizationSample<Timing::ClockTick> sample{};
    sample.LocalRequestTransmitTime = 1000000ULL;
    sample.RemoteRequestReceiveTime = 1200000ULL;
    sample.RemoteResponseTransmitTime = 1250000ULL;
    sample.LocalResponseReceiveTime = 1600000ULL;
    return sample;
}

int main() {
    FakeSynchronizationTarget target;
    target.Config.MaximumRoundTripDelayNanoseconds = 1000000ULL;
    Radio::RadioClockSynchronizationDiagnosticTarget diagnostics(target);

    target.AcceptNext = true;
    const auto accepted = diagnostics.SubmitSynchronizationSample(Baseline());
    assert(accepted.Accepted);
    assert(diagnostics.GetRejectionStatistics().ClassifiedRejectedSamples() == 0U);

    target.AcceptNext = false;

    auto invalidOrder = Baseline();
    invalidOrder.LocalResponseReceiveTime = invalidOrder.LocalRequestTransmitTime - 1ULL;
    assert(!diagnostics.SubmitSynchronizationSample(invalidOrder).Accepted);

    auto impossibleProcessing = Baseline();
    impossibleProcessing.LocalResponseReceiveTime = 1200000ULL;
    impossibleProcessing.RemoteRequestReceiveTime = 1200000ULL;
    impossibleProcessing.RemoteResponseTransmitTime = 1500000ULL;
    assert(!diagnostics.SubmitSynchronizationSample(impossibleProcessing).Accepted);

    auto excessiveDelay = Baseline();
    excessiveDelay.LocalResponseReceiveTime = 2600000ULL;
    assert(!diagnostics.SubmitSynchronizationSample(excessiveDelay).Accepted);

    auto stats = diagnostics.GetRejectionStatistics();
    assert(stats.InvalidTimestampOrder == 1U);
    assert(stats.RemoteProcessingExceedsLocalElapsed == 1U);
    assert(stats.RoundTripDelayExceeded == 1U);
    assert(stats.Unclassified == 0U);
    assert(stats.RoundTripDelaySamples == 1U);
    assert(stats.MinimumRejectedRoundTripDelayNanoseconds == 1550000ULL);
    assert(stats.MaximumRejectedRoundTripDelayNanoseconds == 1550000ULL);
    assert(stats.MeanRejectedRoundTripDelayNanoseconds() == 1550000ULL);
    assert(stats.ClassifiedRejectedSamples() == 3U);

    // Deliberately force the wrapped target to reject a structurally admissible sample. This must remain visible as an
    // unclassified rejection so a future Timing rule cannot silently disappear into the wrong Radio diagnostic bucket.
    assert(!diagnostics.SubmitSynchronizationSample(Baseline()).Accepted);
    stats = diagnostics.GetRejectionStatistics();
    assert(stats.Unclassified == 1U);
    assert(stats.ClassifiedRejectedSamples() == 4U);

    diagnostics.ResetRejectionStatistics();
    stats = diagnostics.GetRejectionStatistics();
    assert(stats.ClassifiedRejectedSamples() == 0U);
    assert(stats.RoundTripDelaySamples == 0U);
    return 0;
}
