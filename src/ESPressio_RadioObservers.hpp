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

/// <summary>Observes the synchronous return of a concrete packet-radio Send attempt.</summary>
/// <remarks>
/// This callback reports submission/admission and any stronger direct-link evidence already established before Send
/// returned. An Accepted result with unknown transmission completion is not a transmission-completion notification and
/// must never be interpreted as peer delivery.
/// </remarks>
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

    /// <summary>
    /// Emits the synchronous Send-attempt result. Accepted without completion evidence remains admission only.
    /// </summary>
    void NotifySendAttempted(
        IRadio& radio,
        const RadioAddress& destination,
        std::size_t payloadSize,
        const RadioSendResult& result
    ) noexcept {
        try { _dispatcher->NotifySendAttempted(radio, destination, payloadSize, result); } catch (...) {}
    }
};

} // namespace ESPressio::Radio
