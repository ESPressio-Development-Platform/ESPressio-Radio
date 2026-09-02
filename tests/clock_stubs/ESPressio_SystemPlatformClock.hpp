#pragma once

#include <chrono>
#include <cstdint>

namespace ESPressio::System::Clock {

class NativeTestMonotonicClock final {
public:
    uint64_t NowNanoseconds() const noexcept {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()
        );
    }
};

inline NativeTestMonotonicClock& Monotonic() noexcept {
    static NativeTestMonotonicClock clock;
    return clock;
}

} // namespace ESPressio::System::Clock
