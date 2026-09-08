#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include <ESPressio_IClockSynchronizationTarget.hpp>

namespace ESPressio::Radio {

/// <summary>Reason-specific diagnostic counters for Timing-rejected Radio clock exchanges.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - InvalidTimestampOrder (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - RemoteProcessingExceedsLocalElapsed (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - RoundTripDelayExceeded (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - Unclassified (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - RoundTripDelaySamples (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - TotalRejectedRoundTripDelayNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MinimumRejectedRoundTripDelayNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - MaximumRejectedRoundTripDelayNanoseconds (std::uint64_t): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 64 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct RadioClockSynchronizationRejectionStatistics final {
    std::uint64_t InvalidTimestampOrder{0U};
    std::uint64_t RemoteProcessingExceedsLocalElapsed{0U};
    std::uint64_t RoundTripDelayExceeded{0U};
    std::uint64_t Unclassified{0U};

    std::uint64_t RoundTripDelaySamples{0U};
    std::uint64_t TotalRejectedRoundTripDelayNanoseconds{0U};
    std::uint64_t MinimumRejectedRoundTripDelayNanoseconds{0U};
    std::uint64_t MaximumRejectedRoundTripDelayNanoseconds{0U};

    std::uint64_t ClassifiedRejectedSamples() const noexcept {
        return InvalidTimestampOrder + RemoteProcessingExceedsLocalElapsed +
            RoundTripDelayExceeded + Unclassified;
    }

    std::uint64_t MeanRejectedRoundTripDelayNanoseconds() const noexcept {
        return RoundTripDelaySamples == 0U
            ? 0U
            : TotalRejectedRoundTripDelayNanoseconds / RoundTripDelaySamples;
    }
};

/// <summary>
/// Transparent synchronization-target decorator retaining reason-specific evidence for rejected Radio clock samples.
/// </summary>
/// <remarks>
/// Timing remains authoritative: every sample is first submitted unchanged to the wrapped target. Only when Timing
/// rejects it does this decorator apply Timing::ValidateClockSynchronizationSample using the target's active config and
/// retain bounded atomic counters. No acceptance criterion is changed and no additional synchronization is introduced.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _target (Timing::IClockSynchronizationTarget<Timing::ClockTick>&): 4 bytes [0 bytes dynamic allocation]
 * - _invalidTimestampOrder (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _remoteProcessingExceedsLocalElapsed (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _roundTripDelayExceeded (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _unclassified (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _roundTripDelaySamples (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _totalRejectedRoundTripDelayNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _minimumRejectedRoundTripDelayNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * - _maximumRejectedRoundTripDelayNanoseconds (std::atomic<std::uint64_t>): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 72 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class RadioClockSynchronizationDiagnosticTarget final
    : public Timing::IClockSynchronizationTarget<Timing::ClockTick> {
private:
    Timing::IClockSynchronizationTarget<Timing::ClockTick>& _target;

    std::atomic<std::uint64_t> _invalidTimestampOrder{0U};
    std::atomic<std::uint64_t> _remoteProcessingExceedsLocalElapsed{0U};
    std::atomic<std::uint64_t> _roundTripDelayExceeded{0U};
    std::atomic<std::uint64_t> _unclassified{0U};
    std::atomic<std::uint64_t> _roundTripDelaySamples{0U};
    std::atomic<std::uint64_t> _totalRejectedRoundTripDelayNanoseconds{0U};
    std::atomic<std::uint64_t> _minimumRejectedRoundTripDelayNanoseconds{0U};
    std::atomic<std::uint64_t> _maximumRejectedRoundTripDelayNanoseconds{0U};

    static void UpdateMinimum(
        std::atomic<std::uint64_t>& target,
        std::uint64_t value
    ) noexcept {
        auto current = target.load(std::memory_order_relaxed);
        while ((current == 0U || value < current) &&
               !target.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    static void UpdateMaximum(
        std::atomic<std::uint64_t>& target,
        std::uint64_t value
    ) noexcept {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current &&
               !target.compare_exchange_weak(
                   current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    void RecordRejectedSample(
        const Timing::ClockSynchronizationSample<Timing::ClockTick>& sample
    ) noexcept {
        const auto config = _target.GetSynchronizationConfig();
        const auto validation = Timing::ValidateClockSynchronizationSample(
            sample, config.MaximumRoundTripDelayNanoseconds);

        switch (validation.RejectionReason) {
            case Timing::ClockSynchronizationSampleRejectionReason::InvalidTimestampOrder:
                _invalidTimestampOrder.fetch_add(1U, std::memory_order_relaxed);
                break;
            case Timing::ClockSynchronizationSampleRejectionReason::RemoteProcessingExceedsLocalElapsed:
                _remoteProcessingExceedsLocalElapsed.fetch_add(1U, std::memory_order_relaxed);
                break;
            case Timing::ClockSynchronizationSampleRejectionReason::RoundTripDelayExceeded:
                _roundTripDelayExceeded.fetch_add(1U, std::memory_order_relaxed);
                _roundTripDelaySamples.fetch_add(1U, std::memory_order_relaxed);
                _totalRejectedRoundTripDelayNanoseconds.fetch_add(
                    validation.RoundTripDelayNanoseconds, std::memory_order_relaxed);
                UpdateMinimum(_minimumRejectedRoundTripDelayNanoseconds,
                              validation.RoundTripDelayNanoseconds);
                UpdateMaximum(_maximumRejectedRoundTripDelayNanoseconds,
                              validation.RoundTripDelayNanoseconds);
                break;
            case Timing::ClockSynchronizationSampleRejectionReason::None:
            default:
                // This intentionally highlights any future discipline rejection rule that is not yet represented by
                // the public Timing diagnostic classifier rather than silently attributing it to the wrong cause.
                _unclassified.fetch_add(1U, std::memory_order_relaxed);
                break;
        }
    }

public:
    explicit RadioClockSynchronizationDiagnosticTarget(
        Timing::IClockSynchronizationTarget<Timing::ClockTick>& target
    ) noexcept : _target(target) {}

    Timing::ClockTick GetSynchronizationTimestampNanoseconds() const override {
        return _target.GetSynchronizationTimestampNanoseconds();
    }

    Timing::ClockSynchronizationResult<Timing::ClockTick> SubmitSynchronizationSample(
        const Timing::ClockSynchronizationSample<Timing::ClockTick>& sample,
        Timing::ClockSynchronizationAdjustmentMode adjustmentMode =
            Timing::ClockSynchronizationAdjustmentMode::SlewOnly
    ) override {
        auto result = _target.SubmitSynchronizationSample(sample, adjustmentMode);
        if (!result.Accepted) RecordRejectedSample(sample);
        return result;
    }

    Timing::ClockSynchronizationStatus<Timing::ClockTick> GetSynchronizationStatus() const override {
        return _target.GetSynchronizationStatus();
    }

    void ConfigureSynchronization(const Timing::ClockSynchronizationConfig& config) override {
        _target.ConfigureSynchronization(config);
    }

    Timing::ClockSynchronizationConfig GetSynchronizationConfig() const override {
        return _target.GetSynchronizationConfig();
    }

    void ResetSynchronization() override {
        _target.ResetSynchronization();
    }

    RadioClockSynchronizationRejectionStatistics GetRejectionStatistics() const noexcept {
        return {
            _invalidTimestampOrder.load(std::memory_order_relaxed),
            _remoteProcessingExceedsLocalElapsed.load(std::memory_order_relaxed),
            _roundTripDelayExceeded.load(std::memory_order_relaxed),
            _unclassified.load(std::memory_order_relaxed),
            _roundTripDelaySamples.load(std::memory_order_relaxed),
            _totalRejectedRoundTripDelayNanoseconds.load(std::memory_order_relaxed),
            _minimumRejectedRoundTripDelayNanoseconds.load(std::memory_order_relaxed),
            _maximumRejectedRoundTripDelayNanoseconds.load(std::memory_order_relaxed)
        };
    }

    void ResetRejectionStatistics() noexcept {
        _invalidTimestampOrder.store(0U, std::memory_order_relaxed);
        _remoteProcessingExceedsLocalElapsed.store(0U, std::memory_order_relaxed);
        _roundTripDelayExceeded.store(0U, std::memory_order_relaxed);
        _unclassified.store(0U, std::memory_order_relaxed);
        _roundTripDelaySamples.store(0U, std::memory_order_relaxed);
        _totalRejectedRoundTripDelayNanoseconds.store(0U, std::memory_order_relaxed);
        _minimumRejectedRoundTripDelayNanoseconds.store(0U, std::memory_order_relaxed);
        _maximumRejectedRoundTripDelayNanoseconds.store(0U, std::memory_order_relaxed);
    }
};

} // namespace ESPressio::Radio
