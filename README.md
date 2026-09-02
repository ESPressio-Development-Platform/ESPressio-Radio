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
        -> IRadio concrete bounded RX storage / hardware FIFO
        -> RadioWorker (PrecisionThread)
        -> ESPressio-Radio onward transport
        -> authentication / security owner
        -> primitive/message owner
```

A concrete `IRadio` therefore knows only how to start/stop its radio technology, report its packet capabilities, send bounded opaque bytes to a link-layer address, and expose available inbound opaque packets to the Radio worker. It must not parse ESPressio primitives.

## RadioWorker

Inbound processing is owned by `RadioWorker`, which derives from ESPressio `PrecisionThread` using the same scheduling architecture as other ESPressio workers.

`RadioWorker` deliberately does only this:

1. service attached `IRadio` providers for available inbound link packets;
2. advance each packet into `RadioTransport`;
3. allow `RadioTransport` to reassemble/forward or hand a locally addressed opaque message to its configured receiver.

It does **not** authenticate/decrypt messages and it does **not** inspect Command, Event, State, or any future Foundation Type.

Callback-driven providers such as ESP32 raw 802.11 and ESP-NOW copy accepted inbound packet data into bounded provider-owned queues and invoke only `IRadioWorkSignal::OnRadioWorkAvailable()`. That signal calls `PrecisionThread::Bump()`; parsing, routing, observer notification and onward delivery are deferred to the Radio worker thread. Providers such as nRF24 that may not have an IRQ integration are serviced by the worker's normal bounded iteration cadence.

There is intentionally no public `RadioTransport::Poll()` path. `RadioTransport::AddInterface()` records transport topology only and does not install itself as a radio receiver. `RadioWorker::AddInterface()` owns the production inbound binding and installs itself as the provider receiver/work signal.

`PrecisionThread` is used rather than `EventThread` because radio work availability is scheduling state, not a Foundation Event. This keeps the Radio worker unaware of ESPressio Event payload types while retaining the common Threads cadence/rate-control and lifecycle observation model.

`RadioWorkerConfiguration` exposes:

- `IterationPeriodMilliseconds` — maximum idle interval between inbound service passes;
- `DesiredExecutionBudgetMilliseconds` — desired `PrecisionThread` execution budget/rate-control target.

`RadioWorker` inherits ESPressio Threads lifecycle and iteration observation, so it intentionally does not duplicate those callbacks with a second worker-specific observer system.

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

- the `RadioWorker` / `IRadioReceiver` path remains the single inbound ownership path used to move link packets into `RadioTransport`;
- `RadioTransport::SetReceiver()` remains the single delivery hook for complete locally addressed logical messages moving onward to Security/authentication;
- `Observers().Subscribe<...>()` provides supplemental one-to-many observation for telemetry, diagnostics, application awareness, and composition with other ESPressio components.

Concrete radios expose these observer interfaces:

- `IRadioLifecycleObserver` — successful radio start/stop transitions;
- `IRadioPacketObserver` — complete link-layer packets after the worker has advanced them into RadioTransport;
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
- future LoRa/sub-GHz/802.15.4 providers can implement the same contract without changing `RadioTransport` or `RadioWorker`.

## Minimal usage

```cpp
#include <ESPressio_Radio.hpp>

ESPressio::Radio::RadioTransport transport(1); // logical local node ID
ESPressio::Radio::RadioWorker worker(transport);

// A concrete provider is supplied by another ESPressio library.
worker.AddInterface(radio);
transport.SetRoute(2, radio, peerRadioAddress);

// Complete locally addressed messages leave Radio here and enter the
// Security/authentication stage. Radio does not interpret the payload.
transport.SetReceiver(&authenticatedMessageIngress);

transport.Start();
worker.Initialize();
worker.Start();

const uint8_t bytes[] = {1, 2, 3, 4};
transport.Send(2, 7, bytes, sizeof(bytes));
```

The receiver registered with `RadioTransport::SetReceiver()` receives a complete `RadioTransportMessageView`. That receiver is the boundary back into Security/authentication and only afterwards into primitive-specific transport handling; Observer subscriptions remain supplemental and do not take over this responsibility.

## Memory behaviour

The fixed interface, route, recent-message, and reassembly registries avoid unbounded registry allocation. Reassembly payload storage uses `System::Memory::ByteVector<ExternalPreferred>` so its dynamic storage is routed through the active ESPressio-System memory provider.

The Observable implementation requires shared ownership for safe subscription lifetime handling. Radio therefore obtains dispatcher ownership **only** through `System::Memory::MakeShared<..., ExternalPreferred>()`; Radio does not directly name `std::shared_ptr`, call `std::make_shared`, or perform raw owning allocation. This keeps the ownership requirement behind ESPressio-System while allowing the installed platform provider, such as ESPressio-ESP32, to decide the actual memory region.

Callback-driven concrete providers use bounded inline RX queues and do not allocate while running inside radio-driver callbacks. The queue depth remains concrete-specific and compile-time configurable.

The default RadioTransport bounds are configurable at compile time with `ESPRESSIO_RADIO_MAX_INTERFACES`, `ESPRESSIO_RADIO_MAX_ROUTES`, `ESPRESSIO_RADIO_MAX_REASSEMBLIES`, `ESPRESSIO_RADIO_MAX_RECENT_MESSAGES`, and `ESPRESSIO_RADIO_MAX_MESSAGE_BYTES`.

## Validation

Native tests exercise fragmentation/reassembly, heterogeneous cross-radio forwarding with different link MTUs, typed one-to-many Observer callbacks, lifecycle/topology/message observations, and RAII subscription removal. The transport tests use a synchronous test-only ingress shim so they exercise the same `ProcessInboundPacket()` boundary that production `RadioWorker` owns. ESP32 PlatformIO smoke validation additionally compiles the `RadioWorker`/raw-802.11 provider integration against the current ESPressio System, Observable, and Threads working branches.