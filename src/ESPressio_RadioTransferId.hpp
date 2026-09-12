#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

/// <summary>Bounded non-zero uint16 transfer-id issuer with an explicit recent-use exclusion window.</summary>
/// <remarks>Active-transfer exclusion is supplied by the owning runtime so the issuer does not duplicate transfer-table
/// ownership. Exhaustion fails admission rather than aliasing any active or recently completed transfer.</remarks>
template<std::size_t TRecentCapacity>
class RadioTransferIdIssuer final {
    static_assert(TRecentCapacity > 0, "Radio transfer-id recent window must be finite and non-zero");
    std::array<RadioTransferId, TRecentCapacity> _recent{};
    std::size_t _recentCursor{0};
    RadioTransferId _next{1};

    constexpr bool IsRecent(RadioTransferId id) const noexcept {
        if (id == 0) return true;
        for (const auto recent : _recent) if (recent == id) return true;
        return false;
    }

public:
    RadioTransferIdIssuer() = default;
    RadioTransferIdIssuer(const RadioTransferIdIssuer&) = delete;
    RadioTransferIdIssuer& operator=(const RadioTransferIdIssuer&) = delete;

    template<class TActivePredicate>
    bool TryIssue(TActivePredicate&& isActive, RadioTransferId& issued) noexcept {
        issued = 0;
        for (std::uint32_t inspected = 0; inspected < 0xFFFFu; ++inspected) {
            const auto candidate = _next;
            _next = static_cast<RadioTransferId>(_next + 1u);
            if (_next == 0) _next = 1;
            if (candidate == 0 || IsRecent(candidate) || isActive(candidate)) continue;
            issued = candidate;
            return true;
        }
        return false;
    }

    void RememberCompleted(RadioTransferId id) noexcept {
        if (id == 0) return;
        _recent[_recentCursor] = id;
        _recentCursor = (_recentCursor + 1u) % _recent.size();
    }

    bool WasRecentlyCompleted(RadioTransferId id) const noexcept { return IsRecent(id); }
    static constexpr std::size_t RecentCapacity() noexcept { return TRecentCapacity; }
};

} // namespace ESPressio::Radio
