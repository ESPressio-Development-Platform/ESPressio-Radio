#pragma once

#include <cstddef>

#include <ESPressio_Observable.hpp>
#include <ESPressio_Memory.hpp>

#include "ESPressio_RadioTypes.hpp"

namespace ESPressio::Radio {

class IRadio;

/// <summary>Observes synchronous lifecycle changes emitted by a concrete packet radio.</summary>
class IRadioLifecycleObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioLifecycleObserver() = default;
    virtual void OnRadioStarted(IRadio& radio) = 0;
    virtual void OnRadioStopped(IRadio& radio) = 0;
};

/// <summary>Observes complete link-layer packets delivered upward by a concrete packet radio.</summary>
/// <remarks>The packet payload is borrowed and remains valid only for the duration of the callback.</remarks>
class IRadioPacketObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioPacketObserver() = default;
    virtual void OnRadioPacketReceived(IRadio& radio, const RadioPacketView& packet) = 0;
};

/// <summary>Observes synchronous send attempts made through a concrete packet radio.</summary>
class IRadioSendObserver : public virtual Observable::IObserver {
public:
    virtual ~IRadioSendObserver() = default;
    virtual void OnRadioSendCompleted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) = 0;
};

/// <summary>
/// RAII observer-subscription surface shared by all ESPressio Radio concretes.
/// Subscriptions are supplemental observations and do not replace the radio's transport receiver.
/// </summary>
class RadioObserverSubscriptions final {
private:
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

        void NotifySendCompleted(
            IRadio& radio,
            const RadioAddress& destination,
            std::size_t payloadSize,
            const RadioSendResult& result
        ) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<IRadioSendObserver>(
                    [&](IRadioSendObserver* observer) {
                        observer->OnRadioSendCompleted(radio, destination, payloadSize, result);
                    }
                );
            });
        }
    };

    // Observable deliberately requires shared ownership for safe subscription lifetime handling.
    // Infer that ownership type exclusively from ESPressio-System's policy-aware factory so this
    // layer neither names nor constructs a standard-library smart pointer directly.
    using DispatcherOwner = decltype(System::Memory::MakeShared<
        Dispatcher,
        System::Memory::MemoryPolicy::ExternalPreferred
    >());

    DispatcherOwner _dispatcher;

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

    /// <summary>Emits a supplemental observer callback without allowing subscriber failures to alter transport mechanics.</summary>
    void NotifyStarted(IRadio& radio) noexcept {
        try { _dispatcher->NotifyStarted(radio); } catch (...) {}
    }

    /// <summary>Emits a supplemental observer callback without allowing subscriber failures to alter transport mechanics.</summary>
    void NotifyStopped(IRadio& radio) noexcept {
        try { _dispatcher->NotifyStopped(radio); } catch (...) {}
    }

    /// <summary>Emits a supplemental observer callback without allowing subscriber failures to alter transport mechanics.</summary>
    void NotifyPacketReceived(IRadio& radio, const RadioPacketView& packet) noexcept {
        try { _dispatcher->NotifyPacketReceived(radio, packet); } catch (...) {}
    }

    /// <summary>Emits a supplemental observer callback without allowing subscriber failures to alter transport mechanics.</summary>
    void NotifySendCompleted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) noexcept {
        try { _dispatcher->NotifySendCompleted(radio, destination, payloadSize, result); } catch (...) {}
    }
};

} // namespace ESPressio::Radio
