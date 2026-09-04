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
/// instantiated. Borrowed packet/transfer payloads are copied exactly once into externally preferred event-owned storage
/// because Event delivery outlives the synchronous observer callback.
/// </remarks>
class RadioEventBridge final :
    public Radio::IRadioLifecycleObserver,
    public Radio::IRadioPacketObserver,
    public Radio::IRadioSendAttemptObserver,
    public Radio::IRadioTransportLifecycleObserver,
    public Radio::IRadioTransportInterfaceObserver,
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

    /// <summary>Subscribes this bridge to one concrete radio's lifecycle, packet and send-attempt surfaces.</summary>
    bool Subscribe(Radio::IRadio& radio) {
        try {
            auto handle = radio.Observers().Subscribe<
                Radio::IRadioLifecycleObserver,
                Radio::IRadioPacketObserver,
                Radio::IRadioSendAttemptObserver
            >(this);
            if (!handle) return false;
            _handles.emplace_back(std::move(handle));
            return true;
        } catch (...) {
            return false;
        }
    }

    /// <summary>Subscribes this bridge to RadioTransport lifecycle, interface and logical-transfer observer surfaces.</summary>
    bool Subscribe(Radio::RadioTransport& transport) {
        try {
            auto handle = transport.Observers().Subscribe<
                Radio::IRadioTransportLifecycleObserver,
                Radio::IRadioTransportInterfaceObserver,
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
    void Shutdown() noexcept { _handles.clear(); }

    /// <summary>Returns the number of active Radio/RadioTransport observer registrations owned by this bridge.</summary>
    std::size_t SubscriptionCount() const noexcept { return _handles.size(); }

    void OnRadioStarted(Radio::IRadio& radio) override { (new RadioStartedEvent(radio))->Queue(); }
    void OnRadioStopped(Radio::IRadio& radio) override { (new RadioStoppedEvent(radio))->Queue(); }

    void OnRadioPacketReceived(Radio::IRadio& radio, const Radio::RadioPacketView& packet) override {
        (new RadioPacketReceivedEvent(radio, packet))->Queue();
    }

    void OnRadioSendAttempted(
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioSendResult& result
    ) override {
        (new RadioSendAttemptedEvent(radio, destination, payloadSize, result))->Queue();
    }

    void OnRadioTransportStarted(Radio::RadioTransport&) override { (new RadioTransportStartedEvent())->Queue(); }
    void OnRadioTransportStopped(Radio::RadioTransport&) override { (new RadioTransportStoppedEvent())->Queue(); }

    void OnRadioInterfaceAdded(Radio::RadioTransport&, Radio::IRadio& radio) override {
        (new RadioInterfaceAddedEvent(radio))->Queue();
    }

    void OnRadioInterfaceRemoved(Radio::RadioTransport&, Radio::IRadio& radio) override {
        (new RadioInterfaceRemovedEvent(radio))->Queue();
    }

    void OnRadioTransportSendAttempted(
        Radio::RadioTransport&,
        Radio::IRadio& radio,
        const Radio::RadioAddress& destination,
        std::size_t payloadSize,
        const Radio::RadioTransportSendResult& result
    ) override {
        (new RadioTransportSendAttemptedEvent(radio, destination, payloadSize, result))->Queue();
    }

    void OnRadioTransportMessageReceived(
        Radio::RadioTransport&,
        Radio::IRadio& radio,
        const Radio::RadioTransportMessageView& message
    ) override {
        (new RadioTransportMessageReceivedEvent(radio, message))->Queue();
    }
};

} // namespace ESPressio::Event
