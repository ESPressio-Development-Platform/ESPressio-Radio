#pragma once

#include <cstdint>
#include <string>

// Host-only Arduino compatibility surface required by ESPressio-Units headers during contract compilation.
class String {
    std::string _value;
public:
    String() = default;
    String(const char* value) : _value(value ? value : "") {}
    String(const std::string& value) : _value(value) {}
    String& operator+=(const char* value) { if(value) _value += value; return *this; }
    String& operator+=(const String& value) { _value += value._value; return *this; }
    const char* c_str() const noexcept { return _value.c_str(); }
    std::size_t length() const noexcept { return _value.length(); }
};
