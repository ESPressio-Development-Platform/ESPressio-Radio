#pragma once

#include <utility>

#include <ESPressio_Memory.hpp>

#include "ESPressio_RadioEvents.hpp"
#include "ESPressio_RadioObservers.hpp"
#include "ESPressio_RadioTransport.hpp"

namespace ESPressio::Event {

/// <summary>
/// Optional adapter that subscribes to ESPressio Radio observer surfaces and emits corresponding asynchronous Events.
/// </summary>
/// <remarks>
/// The bridge is intentionally optional: Radio remains transport-only and has no requirement that an Event bridge be
/// instantiated. Borrowed packet/message payloads are copied exactly once into externally preferred event-owned storage
/// because Event delivery outlives the synchronous observer callback.
/// </remarks>
class RadioEventBridge final :
    public Radio::IRadioLifecycleObserver,
    public Radio::IRadioPacketObserver,
    public Radio::IRadioSendObserver,
    public Radio::IRadioTransportLifecycleObserver,
    public Radio::IRadioTransportTopologyObserver,
    public Radio::IRadioTransportMessageObserver {
private:
    static constexpr auto ExternalPreferred = System::Memory::MemoryPolicy::ExternalPreferred;
    using HandleStorage = System::Memory::Vector<Observable::ObserverHandlePtr, ExternalPreferred>;

    HandleStorage _handles;

public:
    RadioEventBridge() = default;
    ~RadioEventBridge() override { Shutdown(); }

    RadioEventBridge(const RadioEventBridge&) = delete;
    RadioEventBridge& operator=(const RadioEventBridge&) = delete;
    RadioEventBridge(RadioEventBridge&&) = delete;
    RadioEventBridge& operator=(RadioEventBridge&&) = delete;

    /// <summary>Subscribes this bridge to one concrete radio's lifecycle, packet and send observer surfaces.</summary>
    bool Subscribe(Radio::IRadio& radio) {
        try {
            auto handle = radio.Observers().Subscribe<
                Radio::IRadioLifecycleObserver,
                Radio::IRadioPacketObserver,
                Radio::IRadioSendObserver
            >(this);
            if (!handle) return false;
            _handles.emplace_back(std::move(handle));
            return true;
        } catch (...) {
            return false;
        }
    }

    /// <summary>Subscribes this bridge to RadioTransport lifecycle, topology and logical-message observer surfaces.</summary>
    bool Subscribe(Radio::RadioTransport& transport) {
        try {
            auto handle = transport.Observers().Subscribe<
                Radio::IRadioTransportLifecycleObserver,
                Radio::IRadioTransportTopologyObserver,
                Radio::IRadioTransportMessageObserver
            >(this);
            if (!handle) return false;
            _handles.emplace_back(std::move(handle));
            return true;
        } catch (...) {
            return false;
        }
    }

    /// <summary>Releases every observer registration owned by this bridge.</summary>
    void Shutdown() noexcept {
        _handles.clear();
    }

    /// <summary>Returns the number of active Radio/RadioTransport observer registrations owned by this bridge.</summary>
    std::size_t SubscriptionCount() const noexcept { return _handles.size(); }

    void OnRadioStarted(Radio::IRadio& radio) override {
        (new RadioStartedEvent(radio))->Queue();
    }

    void OnRadioStopped(Radio::IRadio& radio) override {
        (new RadioStoppedEvent(radio))->Queue();
    }

    void OnRadioPacketReceived(Radio::IRadio& radio, const Radio::RadioPacketView& packet) override {
        (new RadioPacketReceivedEvent(radio, packet))->Queue();
    }

    void OnRadioSendCompleted(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioSendResult& result
    ) override {
        (new RadioSendCompletedEvent(radio, destination, payloadSize, result))->Queue();
    }

    void OnRadioTransportStarted(Radio::RadioTransport& transport) override {
        (new RadioTransportStartedEvent(transport))->Queue();
    }

    void OnRadioTransportStopped(Radio::RadioTransport& transport) override {
        (new RadioTransportStoppedEvent(transport))->Queue();
    }

    void OnRadioInterfaceConfigured(
        Radio::RadioTransport& transport,
        Radio::IRadio& radio,
        bool defaultRoute
    ) override {
        (new RadioInterfaceConfiguredEvent(transport, radio, defaultRoute))->Queue();
    }

    void OnRadioRouteConfigured(
        Radio::RadioTransport& transport,
        Radio::RadioNodeId destination,
        Radio::IRadio& radio,
        const Radio::RadioAddress& nextHop
    ) override {
        (new RadioRouteConfiguredEvent(transport, destination, radio, nextHop))->Queue();
    }

    void OnRadioRouteRemoved(
        Radio::RadioTransport& transport,
        Radio::RadioNodeId destination
    ) override {
        (new RadioRouteRemovedEvent(transport, destination))->Queue();
    }

    void OnRadioTransportSendCompleted(
        Radio::RadioTransport& transport,
        Radio::RadioNodeId destination,
        Radio::RadioChannel channel,
        std::size_t payloadSize,
        const Radio::RadioTransportSendResult& result
    ) override {
        (new RadioTransportSendCompletedEvent(
            transport,
            destination,
            channel,
            payloadSize,
            result
        ))->Queue();
    }

    void OnRadioTransportMessageReceived(
        Radio::RadioTransport& transport,
        const Radio::RadioTransportMessageView& message
    ) override {
        (new RadioTransportMessageReceivedEvent(transport, message))->Queue();
    }

    void OnRadioTransportMessageForwarded(
        Radio::RadioTransport& transport,
        const Radio::RadioTransportMessageView& message,
        const Radio::RadioTransportSendResult& result
    ) override {
        (new RadioTransportMessageForwardedEvent(transport, message, result))->Queue();
    }
};

} // namespace ESPressio::Event
