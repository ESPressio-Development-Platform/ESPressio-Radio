#pragma once

#include <chrono>
#include <cstdint>

namespace ESPressio::System::Clock {

/**
 * ESPressio Memory Audit
 * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
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
