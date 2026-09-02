#pragma once

#include "ESPressio_IClockSynchronizationTarget.hpp"
#include "ESPressio_SystemPlatformClock.hpp"

namespace ESPressio::Timing {

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
