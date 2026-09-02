#pragma once

#include <cstdint>

namespace ESPressio::Timing {

using ClockTick = uint64_t;

enum class ClockSynchronizationState : uint8_t {
    Unsynchronized,
    Acquiring,
    Synchronized
};

enum class ClockSynchronizationAdjustmentMode : uint8_t {
    SlewOnly,
    StepIfUnsynchronized,
    StepAlways
};

template<typename TTick = ClockTick>
struct ClockSynchronizationSample {
    TTick LocalRequestTransmitTime = 0;
    TTick RemoteRequestReceiveTime = 0;
    TTick RemoteResponseTransmitTime = 0;
    TTick LocalResponseReceiveTime = 0;
};

struct ClockSynchronizationConfig {
    uint64_t MaximumRoundTripDelayNanoseconds = 100000000ULL;
};

template<typename TTick = ClockTick>
struct ClockSynchronizationResult {
    bool Accepted = false;
    int64_t MeasuredOffsetNanoseconds = 0;
    int64_t FilteredOffsetNanoseconds = 0;
    uint64_t RoundTripDelayNanoseconds = 0;
    double EstimatedDriftPpm = 0.0;
    uint32_t AcceptedSampleCount = 0;
    uint32_t RejectedSampleCount = 0;
};

template<typename TTick = ClockTick>
struct ClockSynchronizationStatus {
    ClockSynchronizationState State = ClockSynchronizationState::Unsynchronized;
    int64_t LastMeasuredOffsetNanoseconds = 0;
    int64_t FilteredOffsetNanoseconds = 0;
    int64_t PendingPhaseCorrectionNanoseconds = 0;
    int64_t AppliedCorrectionNanoseconds = 0;
    uint64_t LastRoundTripDelayNanoseconds = 0;
    double EstimatedDriftPpm = 0.0;
    uint32_t AcceptedSampleCount = 0;
    uint32_t RejectedSampleCount = 0;
    TTick LastAcceptedSampleLocalTime = 0;
    bool HasAcceptedSample = false;
};

template<typename TTick = ClockTick>
class IClockSynchronizationTarget {
public:
    virtual ~IClockSynchronizationTarget() = default;
    virtual TTick GetSynchronizationTimestampNanoseconds() const = 0;
    virtual ClockSynchronizationResult<TTick> SubmitSynchronizationSample(
        const ClockSynchronizationSample<TTick>& sample,
        ClockSynchronizationAdjustmentMode adjustmentMode = ClockSynchronizationAdjustmentMode::SlewOnly
    ) = 0;
    virtual ClockSynchronizationStatus<TTick> GetSynchronizationStatus() const = 0;
    virtual void ConfigureSynchronization(const ClockSynchronizationConfig& config) = 0;
    virtual ClockSynchronizationConfig GetSynchronizationConfig() const = 0;
    virtual void ResetSynchronization() = 0;
};

} // namespace ESPressio::Timing
