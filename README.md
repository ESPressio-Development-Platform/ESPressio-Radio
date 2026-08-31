# ESPressio-Radio

`ESPressio-Radio` provides hardware-agnostic packet-radio **onward transport** for ESPressio.

Its responsibility is intentionally narrow: it carries opaque messages between logical ESPressio nodes over one or more packet-radio interfaces. It does **not** implement Command, Event, State, or other primitive semantics, and it does **not** establish message authenticity. Authentication/encryption policy belongs to ESPressio Security and primitive meaning belongs to the primitive-owning libraries.

## Responsibility boundary

Outbound:

```text
ESPressio primitive / other message
        -> authentication / security owner
        -> ESPressio-Radio onward transport
        -> IRadio concrete
        -> RF medium
```

Inbound:

```text
RF medium
        -> IRadio concrete
        -> ESPressio-Radio onward transport
        -> authentication / security owner
        -> primitive/message owner
```

A concrete `IRadio` therefore knows only how to start/stop its radio technology, report its packet capabilities, send bounded opaque bytes to a link-layer address, and deliver bounded opaque received bytes upward. It must not parse ESPressio primitives.

## Portable transport

`RadioTransport` sits above one or more `IRadio` implementations and provides only mechanics required to move an already-addressed opaque message onward:

- logical node addressing;
- radio-interface and next-hop route selection;
- bounded fragmentation/reassembly across different radio MTUs;
- duplicate suppression;
- hop-limit enforcement; and
- heterogeneous cross-radio forwarding.

The payload is opaque. `RadioChannel` is an 8-bit transport discriminator for an upstream owner; Radio does not assign semantic meaning to a channel.

For example, a node can receive a logical message over an ESP32 raw 802.11 provider and forward it over an nRF24 provider without either concrete understanding the carried message.

## Observer callback subscriptions

Radio integrates the typed, RTTI-free ESPressio Observable callback-subscription model without changing transport ownership.

The distinction is deliberate:

- `IRadio::SetReceiver()` and `RadioTransport::SetReceiver()` remain the single delivery/ownership hooks used to move packets and complete logical messages through the transport pipeline;
- `Observers().Subscribe<...>()` provides supplemental one-to-many observation for telemetry, diagnostics, application awareness, and composition with other ESPressio components.

Concrete radios expose these observer interfaces:

- `IRadioLifecycleObserver` — successful radio start/stop transitions;
- `IRadioPacketObserver` — complete link-layer packets delivered upward;
- `IRadioSendObserver` — synchronous radio send completion/result.

`RadioTransport` exposes these observer interfaces:

- `IRadioTransportLifecycleObserver` — successful transport start/stop transitions;
- `IRadioTransportTopologyObserver` — interface configuration, route configuration, and route removal;
- `IRadioTransportMessageObserver` — public logical-message sends, completed local logical-message delivery, and heterogeneous onward forwarding.

Subscriptions return `Observable::ObserverHandlePtr`; retaining the handle retains the registration and destroying/resetting the handle unregisters it through the normal ESPressio Observable RAII mechanism. One observer may subscribe to several compatible observer interfaces in one registration:

```cpp
class Diagnostics final :
    public ESPressio::Radio::IRadioLifecycleObserver,
    public ESPressio::Radio::IRadioSendObserver {
public:
    void OnRadioStarted(ESPressio::Radio::IRadio&) override {}
    void OnRadioStopped(ESPressio::Radio::IRadio&) override {}

    void OnRadioSendCompleted(
        ESPressio::Radio::IRadio&,
        const ESPressio::Radio::RadioAddress&,
        std::size_t,
        const ESPressio::Radio::RadioSendResult&
    ) override {}
};

Diagnostics diagnostics;
auto subscription = radio.Observers().Subscribe<
    ESPressio::Radio::IRadioLifecycleObserver,
    ESPressio::Radio::IRadioSendObserver
>(&diagnostics);
```

Observer callbacks are synchronous. Packet/message payload views are borrowed and are valid only for the duration of the callback. Supplemental observer notification is exception-isolated at the Radio boundary: an observer failure cannot interrupt the underlying receiver/forwarding path or make a `noexcept` radio shutdown terminate the process.

## Concrete providers

The hardware-neutral interfaces live here. Concrete implementations belong with the hardware/protocol integration they represent:

- `ESPressio-ESP32`: ESP32 integrated Wi-Fi raw IEEE 802.11 provider;
- `ESPressio-ESP-Now`: ESP-NOW provider over the same `IRadio` contract;
- `ESPressio-NRF24`: nRF24L01/nRF24L01+ provider;
- future LoRa/sub-GHz/802.15.4 providers can implement the same contract without changing `RadioTransport`.

## Minimal usage

```cpp
#include <ESPressio_Radio.hpp>

ESPressio::Radio::RadioTransport transport(1); // logical local node ID

// A concrete provider is supplied by another ESPressio library.
transport.AddInterface(radio);
transport.SetRoute(2, radio, peerRadioAddress);
transport.Start();

const uint8_t bytes[] = {1, 2, 3, 4};
transport.Send(2, 7, bytes, sizeof(bytes));
```

The receiver registered with `RadioTransport::SetReceiver()` receives a complete `RadioTransportMessageView`. That receiver is the boundary back into Security/primitive-specific transport handling; Observer subscriptions remain supplemental and do not take over this responsibility.

## Memory behaviour

The fixed interface, route, recent-message, and reassembly registries avoid unbounded registry allocation. Reassembly payload storage and the Observable dispatcher ownership use the common ESPressio System `ExternalPreferred` memory abstraction, keeping eligible storage available to platform external memory such as ESP32 PSRAM.

The default bounds are configurable at compile time with `ESPRESSIO_RADIO_MAX_INTERFACES`, `ESPRESSIO_RADIO_MAX_ROUTES`, `ESPRESSIO_RADIO_MAX_REASSEMBLIES`, `ESPRESSIO_RADIO_MAX_RECENT_MESSAGES`, and `ESPRESSIO_RADIO_MAX_MESSAGE_BYTES`.

## Validation

Native tests exercise fragmentation/reassembly, heterogeneous cross-radio forwarding with different link MTUs, typed one-to-many Observer callbacks, lifecycle/topology/message observations, and RAII subscription removal. GitHub Actions validates against the current ESPressio System and Observable working branches with C++17, `-Wall`, `-Wextra`, and `-Werror`.
