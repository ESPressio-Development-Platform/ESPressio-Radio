#pragma once

#include <cstddef>

#include <ESPressio_Observable.hpp>
#include <ESPressio_Memory.hpp>

#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Observes synchronous lifecycle changes emitted by a concrete packet radio.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioLifecycleObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioLifecycleObserver() = default;
    virtual void OnRadioStarted(IRadio& radio) = 0;
    virtual void OnRadioStopped(IRadio& radio) = 0;
};

/// <summary>Observes complete link-layer packets delivered upward by a concrete packet radio.</summary>
/// <remarks>The packet payload is borrowed and remains valid only for the duration of the callback.</remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioPacketObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioPacketObserver() = default;
    virtual void OnRadioPacketReceived(IRadio& radio, const RadioPacketView& packet) = 0;
};

/// <summary>Observes the synchronous return of a concrete packet-radio Send attempt.</summary>
/// <remarks>
/// This callback reports submission/admission and any stronger direct-link evidence already established before Send
/// returned. An Accepted result with unknown transmission completion is not a transmission-completion notification and
/// must never be interpreted as peer delivery.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioSendAttemptObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioSendAttemptObserver() = default;
    virtual void OnRadioSendAttempted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) = 0;
};

/// <summary>Observes one terminal outcome for a previously accepted deferred packet transmission.</summary>
/// <remarks>
/// The handle must be the valid provider-local DeferredTransmission returned by the corresponding Send call. Providers
/// emit exactly one terminal observation for a promised handle. Evidence must be terminal; this callback is never an
/// admission/submission notification. It does not imply Mesh end-to-end delivery.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class IRadioTransmissionObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioTransmissionObserver() = default;
    virtual void OnRadioTransmissionResolved(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioDirectLinkEvidence& evidence
    ) = 0;
};

/// <summary>
/// RAII observer-subscription surface shared by all ESPressio Radio concretes.
/// Subscriptions are supplemental observations and do not replace the radio's transport receiver.
/// </summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - _dispatcher (System::Memory::SharedPtr<Dispatcher>): 8 bytes [shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; pointee: Observable: _registrations: Capacity * (12 bytes) element storage; pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Total Memory: 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class RadioObserverSubscriptions final {
private:
        /**
     * ESPressio Memory Audit
     * Inherited Memory Total: 52 bytes [Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; Observable: _registrations: Capacity * (12 bytes) element storage; Observable: _bindings: Capacity * (12 bytes) element storage]
     * Members: none (standalone empty object occupies 1 byte; an eligible empty base may be optimized to 0 bytes).
     * Total Memory: 52 bytes [Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; Observable: _registrations: Capacity * (12 bytes) element storage; Observable: _bindings: Capacity * (12 bytes) element storage]
     * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
     * End ESPressio Memory Audit
     */
class Dispatcher final : public Observable::Observable {
    public:
        void NotifyStarted(IRadio& radio) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioLifecycleObserver>(
                    [&](IRadioLifecycleObserver* observer) { observer->OnRadioStarted(radio); }
                );
            });
        }

        void NotifyStopped(IRadio& radio) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioLifecycleObserver>(
                    [&](IRadioLifecycleObserver* observer) { observer->OnRadioStopped(radio); }
                );
            });
        }

        void NotifyPacketReceived(IRadio& radio, const RadioPacketView& packet) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioPacketObserver>(
                    [&](IRadioPacketObserver* observer) { observer->OnRadioPacketReceived(radio, packet); }
                );
            });
        }

        void NotifySendAttempted(
            IRadio& radio,
            const RadioAddress& destination,
            std::size_t payloadSize,
            const RadioSendResult& result
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioSendAttemptObserver>(
                    [&](IRadioSendAttemptObserver* observer) {
                        observer->OnRadioSendAttempted(radio, destination, payloadSize, result);
                    }
                );
            });
        }

        void NotifyTransmissionResolved(
            IRadio& radio,
            RadioTransmissionHandle transmission,
            const RadioAddress& destination,
            std::size_t payloadSize,
            const RadioDirectLinkEvidence& evidence
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioTransmissionObserver>(
                    [&](IRadioTransmissionObserver* observer) {
                        observer->OnRadioTransmissionResolved(radio, transmission, destination, payloadSize, evidence);
                    }
                );
            });
        }
    };

    System::Memory::SharedPtr<Dispatcher> _dispatcher;

public:
    RadioObserverSubscriptions()
        : _dispatcher(System::Memory::MakeShared<
            Dispatcher,
            System::Memory::MemoryPolicy::ExternalPreferred
        >()) {}

    RadioObserverSubscriptions(const RadioObserverSubscriptions&) = delete;
    RadioObserverSubscriptions& operator=(const RadioObserverSubscriptions&) = delete;
    RadioObserverSubscriptions(RadioObserverSubscriptions&&) = delete;
    RadioObserverSubscriptions& operator=(RadioObserverSubscriptions&&) = delete;

    /// <summary>Subscribes one observer for the explicitly declared radio observer interfaces.</summary>
    template<typename... ObserverInterfaces, typename TObserver>
    Observable::ObserverHandlePtr Subscribe(TObserver* observer) {
        return _dispatcher->template RegisterObserverAs<ObserverInterfaces...>(observer);
    }

    /// <summary>Explicitly removes an observer subscription when present.</summary>
    void Unsubscribe(Observable::IObserver* observer) {
        _dispatcher->UnregisterObserver(observer);
    }

    /// <summary>Determines whether an observer currently has a radio subscription.</summary>
    bool IsSubscribed(Observable::IObserver* observer) {
        return _dispatcher->IsObserverRegistered(observer);
    }

    void NotifyStarted(IRadio& radio) noexcept {
        try { _dispatcher->NotifyStarted(radio); } catch (...) {}
    }

    void NotifyStopped(IRadio& radio) noexcept {
        try { _dispatcher->NotifyStopped(radio); } catch (...) {}
    }

    void NotifyPacketReceived(IRadio& radio, const RadioPacketView& packet) noexcept {
        try { _dispatcher->NotifyPacketReceived(radio, packet); } catch (...) {}
    }

    /// <summary>Emits the synchronous Send-attempt result. Accepted without completion evidence remains admission only.</summary>
    void NotifySendAttempted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) noexcept {
        try { _dispatcher->NotifySendAttempted(radio, destination, payloadSize, result); } catch (...) {}
    }

    /// <summary>Publishes exactly one terminal outcome for a valid deferred transmission handle.</summary>
    void NotifyTransmissionResolved(
        IRadio& radio,
        RadioTransmissionHandle transmission,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioDirectLinkEvidence& evidence
    ) noexcept {
        if (!transmission || !evidence.IsTerminal()) return;
        try { _dispatcher->NotifyTransmissionResolved(radio, transmission, destination, payloadSize, evidence); } catch (...) {}
    }
};

} // namespace ESPressio::Radio
