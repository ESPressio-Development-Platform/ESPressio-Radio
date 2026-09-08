#pragma once

#include <vector>

namespace ESPressio::System::Memory {

/**
 * ESPressio Memory Audit
 * Underlying storage: 1 bytes
 * Total Memory: 1 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
enum class MemoryPolicy : unsigned char {
    Automatic = 0,
    Internal,
    ExternalPreferred,
    ExternalRequired
};

template<typename T, MemoryPolicy = MemoryPolicy::Automatic>
using Vector = std::vector<T>;

template<MemoryPolicy P = MemoryPolicy::Automatic>
using ByteVector = Vector<unsigned char, P>;

} // namespace ESPressio::System::Memory
