#pragma once

#include "ESPressio_DeferredLogicalTransferTracker.hpp"
#include "ESPressio_RadioObservers.hpp"

namespace ESPressio::Radio {

/// <summary>Consumes one terminal aggregate after all observable fragments of a logical transfer become terminal.</summary>
class ILogicalTransferTerminalObserver {
public:
    virtual ~ILogicalTransferTerminalObserver() = default;
    virtual void OnLogicalTransferTerminal(const LogicalTransferTerminalEvidence& terminal) = 0;
};

/// <summary>
/// Narrow observer bridge from provider-local deferred transmission outcomes into Radio-owned logical-transfer tracking.
/// </summary>
/// <remarks>
/// This bridge owns no capacity, queue, task or retry policy. The injected tracker owns bounded correlation state. The
/// provider-local transmission handle is resolved only against the IRadio instance which emitted it. A callback is made
/// only when the tracker establishes one terminal aggregate for the logical transfer. Unknown/stale handles are ignored.
/// The owning Radio execution domain must serialize tracker registration and observer resolution.
/// </remarks>
class DeferredLogicalTransferObserverBridge final : public IRadioTransmissionObserver {
    IDeferredLogicalTransferTracker& _tracker;
    ILogicalTransferTerminalObserver& _observer;

public:
    DeferredLogicalTransferObserverBridge(
        IDeferredLogicalTransferTracker& tracker,
        ILogicalTransferTerminalObserver& observer
    ) noexcept : _tracker(tracker), _observer(observer) {}

    void OnRadioTransmissionResolved(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioAddress&,
        std::size_t,
        const RadioDirectLinkEvidence& evidence
    ) override {
        LogicalTransferTerminalEvidence terminal;
        if (_tracker.Resolve(radio, transmission, evidence, &terminal) ==
            DeferredResolutionResult::LogicalTransferTerminal) {
            _observer.OnLogicalTransferTerminal(terminal);
        }
    }
};

} // namespace ESPressio::Radio
