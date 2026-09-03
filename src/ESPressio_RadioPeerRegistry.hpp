#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioTypes.hpp"

#ifndef ESPRESSIO_RADIO_MAX_PEERS
#define ESPRESSIO_RADIO_MAX_PEERS 32
#endif

namespace ESPressio::Radio {

/// <summary>Resolved Radio-owned direct-peer binding.</summary>
struct RadioPeerBinding final {
    IRadio* Interface{nullptr};
    RadioAddress Address{};

    constexpr bool IsValid() const noexcept {
        return Interface != nullptr && Address.IsValid() && !Address.IsBroadcast();
    }
};

/// <summary>Result of observing or explicitly registering one direct Radio peer.</summary>
enum class RadioPeerObserveResult : std::uint8_t {
    Observed,
    Refreshed,
    ResourceUnavailable,
    Invalid
};

/// <summary>
/// Fixed-capacity Radio-owned registry translating ephemeral direct-peer handles to provider addresses.
/// </summary>
/// <remarks>
/// Handles are process-local and generation-safe. A stale handle deterministically fails after invalidation and can
/// never resolve to a later binding that reuses the same slot. Address observations remain link-local facts only;
/// this registry never constructs or interprets DeviceIdentifier, MembershipIncarnation or Mesh routing identity.
///
/// The default capacity is an implementation bound, independently configurable through ESPRESSIO_RADIO_MAX_PEERS.
/// Each Radio technology/integration may choose a smaller finite bound appropriate to its own neighbour resources.
/// Mutation is expected from the serialized Radio/Mesh integration domain; this registry owns no task or mutex.
/// </remarks>
template<std::size_t Capacity = ESPRESSIO_RADIO_MAX_PEERS>
class RadioPeerRegistry final {
    static_assert(Capacity > 0, "Radio peer capacity must be non-zero.");
    static_assert(Capacity < std::numeric_limits<std::uint16_t>::max(),
                  "Radio peer slots must fit RadioPeerHandle.");

    struct Slot final {
        RadioPeerBinding Binding{};
        std::uint16_t Generation{0};
        bool Occupied{false};
    };

    std::array<Slot, Capacity> _slots{};
    std::size_t _size{0};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        ++current;
        if (current == 0U) ++current;
        return current;
    }

public:
    static constexpr std::size_t MaximumSize() noexcept { return Capacity; }
    constexpr std::size_t Size() const noexcept { return _size; }
    constexpr bool Empty() const noexcept { return _size == 0U; }

    /// <summary>
    /// Observes one directly reachable non-broadcast endpoint and returns its Radio-owned handle.
    /// </summary>
    RadioPeerObserveResult Observe(
        IRadio& radio,
        const RadioAddress& address,
        RadioPeerHandle& handle
    ) noexcept {
        handle = {};
        if (!address.IsValid() || address.IsBroadcast()) return RadioPeerObserveResult::Invalid;

        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = _slots[index];
            if (!slot.Occupied) continue;
            if (slot.Binding.Interface == &radio && slot.Binding.Address == address) {
                handle = RadioPeerHandle{static_cast<std::uint16_t>(index), slot.Generation};
                return RadioPeerObserveResult::Refreshed;
            }
        }

        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = _slots[index];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Binding = RadioPeerBinding{&radio, address};
            slot.Occupied = true;
            ++_size;
            handle = RadioPeerHandle{static_cast<std::uint16_t>(index), slot.Generation};
            return RadioPeerObserveResult::Observed;
        }

        return RadioPeerObserveResult::ResourceUnavailable;
    }

    /// <summary>Resolves a current generation-safe peer handle to its direct Radio binding.</summary>
    const RadioPeerBinding* Resolve(RadioPeerHandle handle) const noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        const auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return nullptr;
        return &slot.Binding;
    }

    RadioPeerBinding* Resolve(RadioPeerHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return nullptr;
        return &slot.Binding;
    }

    /// <summary>Invalidates one exact current peer handle.</summary>
    bool Invalidate(RadioPeerHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return false;
        auto& slot = _slots[handle.Slot];
        if (!slot.Occupied || slot.Generation != handle.Generation) return false;
        slot.Binding = {};
        slot.Occupied = false;
        --_size;
        return true;
    }

    /// <summary>Invalidates all peers belonging to one Radio interface and returns the number removed.</summary>
    std::size_t InvalidateInterface(IRadio& radio) noexcept {
        std::size_t removed = 0;
        for (auto& slot : _slots) {
            if (!slot.Occupied || slot.Binding.Interface != &radio) continue;
            slot.Binding = {};
            slot.Occupied = false;
            --_size;
            ++removed;
        }
        return removed;
    }

    /// <summary>Invalidates every current peer binding during controlled Radio service reset.</summary>
    void Clear() noexcept {
        for (auto& slot : _slots) {
            if (!slot.Occupied) continue;
            slot.Binding = {};
            slot.Occupied = false;
        }
        _size = 0U;
    }
};

} // namespace ESPressio::Radio
