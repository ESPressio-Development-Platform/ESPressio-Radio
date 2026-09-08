#pragma once

#include "ESPressio_IClockSynchronizationTarget.hpp"
#include "ESPressio_SystemPlatformClock.hpp"

namespace ESPressio::Timing {

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
template<typename TTime = void>
class SystemClock final : public IClockSynchronizationTarget<ClockTick> {
public:
    static SystemClock& GetInstance() {
        static SystemClock instance;
        return instance;
    }

    ClockTick GetSynchronizationTimestampNanoseconds() const override {
        return System::Clock::Monotonic().NowNanoseconds();
    }

    ClockSynchronizationResult<ClockTick> SubmitSynchronizationSample(
        const ClockSynchronizationSample<ClockTick>&,
        ClockSynchronizationAdjustmentMode = ClockSynchronizationAdjustmentMode::SlewOnly
    ) override {
        ClockSynchronizationResult<ClockTick> result;
        result.Accepted = true;
        return result;
    }

    ClockSynchronizationStatus<ClockTick> GetSynchronizationStatus() const override {
        return {};
    }

    void ConfigureSynchronization(const ClockSynchronizationConfig&) override {}
    ClockSynchronizationConfig GetSynchronizationConfig() const override { return {}; }
    void ResetSynchronization() override {}
};

} // namespace ESPressio::Timing
