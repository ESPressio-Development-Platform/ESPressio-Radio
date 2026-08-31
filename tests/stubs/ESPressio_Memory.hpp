#pragma once

#include <vector>

namespace ESPressio::System::Memory {

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
