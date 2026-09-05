#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_IRadio.hpp"

namespace ESPressio::Radio {

/// <summary>Generation-safe local handle for one RadioTransport logical transfer awaiting terminal link evidence.</summary>
struct DeferredLogicalTransferHandle final {
    std::uint16_t Slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint16_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
    constexpr bool operator==(const DeferredLogicalTransferHandle& other) const noexcept {
        return Slot == other.Slot && Generation == other.Generation;
    }
};

/// <summary>RadioTransport-owned immutable identity/context for one accepted logical transfer.</summary>
struct DeferredLogicalTransferDescriptor final {
    IRadio* Radio{nullptr};
    RadioPeerHandle Peer{};
    RadioAddress Destination{};
    RadioTransferId TransferId{0};
    std::uint16_t PayloadBytes{0};

    bool IsValid() const noexcept {
        return Radio != nullptr && Destination.IsValid() && TransferId != 0U;
    }
};

/// <summary>Result of registering one accepted physical fragment with a deferred logical-transfer tracker.</summary>
enum class DeferredFragmentRegistrationResult : std::uint8_t {
    Registered,
    LogicalTransferTerminal,
    LogicalTransferUnobservable,
    DuplicateFragment,
    Invalid
};

/// <summary>Result of applying one provider terminal observation.</summary>
enum class DeferredResolutionResult : std::uint8_t {
    Pending,
    LogicalTransferTerminal,
    UnknownTransmission,
    Invalid
};

/// <summary>One terminal aggregate for a complete Radio-owned logical transfer.</summary>
struct LogicalTransferTerminalEvidence final {
    DeferredLogicalTransferHandle Transfer{};
    DeferredLogicalTransferDescriptor Descriptor{};
    RadioDirectLinkEvidence Evidence{};
};

/// <summary>
/// Capacity-erased correlation contract consumed by RadioTransport. Concrete capacity remains an explicit composition choice.
/// </summary>
class IDeferredLogicalTransferTracker {
public:
    virtual ~IDeferredLogicalTransferTracker() = default;
    virtual DeferredLogicalTransferHandle Begin(
        const DeferredLogicalTransferDescriptor& descriptor,
        std::uint8_t fragmentCount
    ) noexcept = 0;
    virtual DeferredFragmentRegistrationResult RegisterAcceptedFragment(
        DeferredLogicalTransferHandle handle,
        std::uint8_t fragmentIndex,
        IRadio& radio,
        const RadioSendResult& result,
        LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept = 0;
    virtual DeferredResolutionResult Resolve(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioDirectLinkEvidence& evidence,
        LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept = 0;
    virtual bool Release(DeferredLogicalTransferHandle handle) noexcept = 0;
    /// <summary>Releases every unresolved logical-transfer reservation during controlled transport reset/shutdown.</summary>
    virtual void Clear() noexcept = 0;
    virtual bool Contains(DeferredLogicalTransferHandle handle) const noexcept = 0;
    virtual std::size_t Size() const noexcept = 0;
};

/// <summary>
/// Fixed-capacity aggregator for provider-local deferred fragment outcomes belonging to Radio logical transfers.
/// </summary>
/// <remarks>
/// Capacity is deliberately supplied by the composition rather than hidden behind a universal Radio constant. Each
/// logical transfer may contain at most 255 physical fragments, matching the RadioTransport wire fragment-count field.
/// The tracker owns correlation only: it does not schedule retries, interpret Mesh identities or imply Mesh next-hop
/// acceptance. A logical transfer is terminal-completed only after every accepted fragment has terminal completion
/// evidence. Any terminal fragment failure makes the logical transfer terminal-failed. If any accepted fragment has
/// neither synchronous terminal evidence nor a promised deferred handle, stronger logical-transfer terminal evidence
/// cannot be established and the reservation is released as Unobservable once registration is complete.
///
/// The owning Radio execution domain must serialize Begin/Register/Resolve/Release/Clear calls. Providers must not publish
/// a promised deferred handle before the corresponding Send call has returned; RadioTransport registers that handle
/// immediately on return before yielding its serialized execution domain. Clear is reset/shutdown cleanup only and emits
/// no fabricated terminal evidence for work whose provider outcome was never established.
/// </remarks>
template<std::size_t Capacity>
class DeferredLogicalTransferTracker final : public IDeferredLogicalTransferTracker {
    static_assert(Capacity > 0U, "Deferred logical-transfer capacity must be explicit and non-zero.");
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max(), "Capacity must fit the handle slot.");

    enum class FragmentState : std::uint8_t {
        Unregistered,
        TerminalCompleted,
        TerminalFailed,
        Deferred,
        Unobservable
    };

    struct FragmentRecord final {
        FragmentState State{FragmentState::Unregistered};
        IRadio* Radio{nullptr};
        RadioTransmissionHandle Transmission{};
        RadioPeerAcknowledgement PeerAcknowledgement{RadioPeerAcknowledgement::Unavailable};
    };

    struct Record final {
        bool Used{false};
        std::uint16_t Generation{0};
        std::uint8_t FragmentCount{0};
        std::uint8_t RegisteredCount{0};
        std::uint8_t TerminalCount{0};
        bool AnyFailure{false};
        bool AnyAcknowledgementUnavailable{false};
        bool AllAcknowledged{true};
        DeferredLogicalTransferDescriptor Descriptor{};
        std::array<FragmentRecord, 255> Fragments{};

        void ResetPayload() noexcept {
            Used = false;
            FragmentCount = 0;
            RegisteredCount = 0;
            TerminalCount = 0;
            AnyFailure = false;
            AnyAcknowledgementUnavailable = false;
            AllAcknowledged = true;
            Descriptor = {};
            for (auto& fragment : Fragments) fragment = {};
        }
    };

    std::array<Record, Capacity> _records{};

    static void AdvanceGeneration(Record& record) noexcept {
        ++record.Generation;
        if (record.Generation == 0U) ++record.Generation;
    }

    Record* ResolveRecord(DeferredLogicalTransferHandle handle) noexcept {
        if (!handle || handle.Slot >= Capacity) return nullptr;
        auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation ? &record : nullptr;
    }

    void ReleaseRecord(Record& record) noexcept { record.ResetPayload(); }

    static RadioDirectLinkEvidence Aggregate(const Record& record) noexcept {
        RadioDirectLinkEvidence evidence{};
        evidence.Transmission = record.AnyFailure
            ? RadioTransmissionCompletion::Failed
            : RadioTransmissionCompletion::Completed;
        if (record.AnyFailure) {
            evidence.PeerAcknowledgement = RadioPeerAcknowledgement::Unknown;
        } else if (record.AllAcknowledged) {
            evidence.PeerAcknowledgement = RadioPeerAcknowledgement::Acknowledged;
        } else if (record.AnyAcknowledgementUnavailable) {
            evidence.PeerAcknowledgement = RadioPeerAcknowledgement::Unavailable;
        } else {
            evidence.PeerAcknowledgement = RadioPeerAcknowledgement::Unknown;
        }
        return evidence;
    }

    static bool HasUnobservableFragment(const Record& record) noexcept {
        for (std::size_t index = 0; index < record.FragmentCount; ++index) {
            if (record.Fragments[index].State == FragmentState::Unobservable) return true;
        }
        return false;
    }

public:
    DeferredLogicalTransferHandle Begin(
        const DeferredLogicalTransferDescriptor& descriptor,
        std::uint8_t fragmentCount
    ) noexcept override {
        if (!descriptor.IsValid() || fragmentCount == 0U) return {};
        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& record = _records[slot];
            if (record.Used) continue;
            AdvanceGeneration(record);
            record.ResetPayload();
            record.Used = true;
            record.FragmentCount = fragmentCount;
            record.Descriptor = descriptor;
            return {static_cast<std::uint16_t>(slot), record.Generation};
        }
        return {};
    }

    DeferredFragmentRegistrationResult RegisterAcceptedFragment(
        DeferredLogicalTransferHandle handle,
        std::uint8_t fragmentIndex,
        IRadio& radio,
        const RadioSendResult& result,
        LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept override {
        auto* record = ResolveRecord(handle);
        if (record == nullptr || fragmentIndex >= record->FragmentCount || !result) {
            return DeferredFragmentRegistrationResult::Invalid;
        }
        auto& fragment = record->Fragments[fragmentIndex];
        if (fragment.State != FragmentState::Unregistered) {
            return DeferredFragmentRegistrationResult::DuplicateFragment;
        }

        fragment.Radio = &radio;
        fragment.PeerAcknowledgement = result.Evidence.PeerAcknowledgement;
        ++record->RegisteredCount;

        if (result.Evidence.IsTerminal()) {
            fragment.State = result.Evidence.TransmissionFailed()
                ? FragmentState::TerminalFailed
                : FragmentState::TerminalCompleted;
            ++record->TerminalCount;
            record->AnyFailure = record->AnyFailure || result.Evidence.TransmissionFailed();
            record->AllAcknowledged = record->AllAcknowledged && result.Evidence.PeerAcknowledged();
            record->AnyAcknowledgementUnavailable = record->AnyAcknowledgementUnavailable ||
                result.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable;
        } else if (result.DeferredTransmission) {
            fragment.State = FragmentState::Deferred;
            fragment.Transmission = result.DeferredTransmission;
        } else {
            fragment.State = FragmentState::Unobservable;
            record->AllAcknowledged = false;
            record->AnyAcknowledgementUnavailable = record->AnyAcknowledgementUnavailable ||
                result.Evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable;
        }

        if (record->RegisteredCount != record->FragmentCount) return DeferredFragmentRegistrationResult::Registered;
        if (HasUnobservableFragment(*record)) {
            ReleaseRecord(*record);
            return DeferredFragmentRegistrationResult::LogicalTransferUnobservable;
        }
        if (record->TerminalCount == record->FragmentCount || record->AnyFailure) {
            if (terminal != nullptr) *terminal = {handle, record->Descriptor, Aggregate(*record)};
            ReleaseRecord(*record);
            return DeferredFragmentRegistrationResult::LogicalTransferTerminal;
        }
        return DeferredFragmentRegistrationResult::Registered;
    }

    DeferredResolutionResult Resolve(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioDirectLinkEvidence& evidence,
        LogicalTransferTerminalEvidence* terminal = nullptr
    ) noexcept override {
        if (!transmission || !evidence.IsTerminal()) return DeferredResolutionResult::Invalid;

        for (std::size_t slot = 0; slot < Capacity; ++slot) {
            auto& record = _records[slot];
            if (!record.Used) continue;
            for (std::size_t index = 0; index < record.FragmentCount; ++index) {
                auto& fragment = record.Fragments[index];
                if (fragment.State != FragmentState::Deferred || fragment.Radio != &radio ||
                    fragment.Transmission != transmission) continue;

                fragment.State = evidence.TransmissionFailed()
                    ? FragmentState::TerminalFailed
                    : FragmentState::TerminalCompleted;
                fragment.Transmission = {};
                fragment.PeerAcknowledgement = evidence.PeerAcknowledgement;
                ++record.TerminalCount;
                record.AnyFailure = record.AnyFailure || evidence.TransmissionFailed();
                record.AllAcknowledged = record.AllAcknowledged && evidence.PeerAcknowledged();
                record.AnyAcknowledgementUnavailable = record.AnyAcknowledgementUnavailable ||
                    evidence.PeerAcknowledgement == RadioPeerAcknowledgement::Unavailable;

                if (record.AnyFailure ||
                    (record.RegisteredCount == record.FragmentCount && record.TerminalCount == record.FragmentCount)) {
                    const DeferredLogicalTransferHandle handle{static_cast<std::uint16_t>(slot), record.Generation};
                    if (terminal != nullptr) *terminal = {handle, record.Descriptor, Aggregate(record)};
                    ReleaseRecord(record);
                    return DeferredResolutionResult::LogicalTransferTerminal;
                }
                return DeferredResolutionResult::Pending;
            }
        }
        return DeferredResolutionResult::UnknownTransmission;
    }

    bool Release(DeferredLogicalTransferHandle handle) noexcept override {
        auto* record = ResolveRecord(handle);
        if (record == nullptr) return false;
        ReleaseRecord(*record);
        return true;
    }

    void Clear() noexcept override {
        for (auto& record : _records) {
            if (record.Used) ReleaseRecord(record);
        }
    }

    bool Contains(DeferredLogicalTransferHandle handle) const noexcept override {
        if (!handle || handle.Slot >= Capacity) return false;
        const auto& record = _records[handle.Slot];
        return record.Used && record.Generation == handle.Generation;
    }

    std::size_t Size() const noexcept override {
        std::size_t count = 0;
        for (const auto& record : _records) if (record.Used) ++count;
        return count;
    }
};

} // namespace ESPressio::Radio
