#pragma once

#include <cstdint>

namespace ESPressio::Timing {

using ClockTick = uint64_t;

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class ClockSynchronizationState : uint8_t {
    Unsynchronized,
    Acquiring,
    Synchronized
};

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class ClockSynchronizationAdjustmentMode : uint8_t {
    SlewOnly,
    StepIfUnsynchronized,
    StepAlways
};

/**
 * ESPressio Memory Audit
 * Members:
 * - LocalRequestTransmitTime (TTick): sizeof(TTick) [0 bytes dynamic allocation]
 * - RemoteRequestReceiveTime (TTick): sizeof(TTick) [0 bytes dynamic allocation]
 * - RemoteResponseTransmitTime (TTick): sizeof(TTick) [0 bytes dynamic allocation]
 * - LocalResponseReceiveTime (TTick): sizeof(TTick) [0 bytes dynamic allocation]
 * Total Memory: sizeof(TTick) + sizeof(TTick) + sizeof(TTick) + sizeof(TTick) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
template<typename TTick = ClockTick>
struct ClockSynchronizationSample {
    TTick LocalRequestTransmitTime = 0;
    TTick RemoteRequestReceiveTime = 0;
    TTick RemoteResponseTransmitTime = 0;
    TTick LocalResponseReceiveTime = 0;
};

/**
 * ESPressio Memory Audit
 * Members:
 * - MaximumRoundTripDelayNanoseconds (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * Total Memory: 8 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
struct ClockSynchronizationConfig {
    uint64_t MaximumRoundTripDelayNanoseconds = 100000000ULL;
};

/**
 * ESPressio Memory Audit
 * Members:
 * - Accepted (bool): 1 bytes [0 bytes dynamic allocation]
 * - MeasuredOffsetNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - FilteredOffsetNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - RoundTripDelayNanoseconds (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - EstimatedDriftPpm (double): 8 bytes [0 bytes dynamic allocation]
 * - AcceptedSampleCount (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - RejectedSampleCount (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * Total Memory: 44 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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

/**
 * ESPressio Memory Audit
 * Members:
 * - State (ClockSynchronizationState): 1 bytes [0 bytes dynamic allocation]
 * - LastMeasuredOffsetNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - FilteredOffsetNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - PendingPhaseCorrectionNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - AppliedCorrectionNanoseconds (int64_t): 8 bytes [0 bytes dynamic allocation]
 * - LastRoundTripDelayNanoseconds (uint64_t): 8 bytes [0 bytes dynamic allocation]
 * - EstimatedDriftPpm (double): 8 bytes [0 bytes dynamic allocation]
 * - AcceptedSampleCount (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - RejectedSampleCount (uint32_t): 4 bytes [0 bytes dynamic allocation]
 * - LastAcceptedSampleLocalTime (TTick): sizeof(TTick) [0 bytes dynamic allocation]
 * - HasAcceptedSample (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 61 bytes known/aligned storage + sizeof(TTick) [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
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

/**
 * ESPressio Memory Audit
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
