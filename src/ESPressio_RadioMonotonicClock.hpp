#pragma once

#include <cstdint>

#include <ESPressio_SystemPlatformClock.hpp>

namespace ESPressio::Radio {

/// <summary>Returns the canonical local monotonic coordinate used by Radio transfer expiry and R3 scheduling.</summary>
/// <remarks>
/// This is duration/deadline time only. It is never synchronized System Time and carries no semantic timestamp evidence.
/// Radio owns the physical timing boundary so higher direct-Radio composition layers can consume the coordinate through
/// their existing Radio dependency without acquiring a direct dependency on System.
/// </remarks>
inline std::uint64_t RadioMonotonicNowNanoseconds() noexcept {
    return System::Clock::Monotonic().NowNanoseconds();
}

} // namespace ESPressio::Radio
