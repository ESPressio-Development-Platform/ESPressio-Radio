# ESPressio-Radio

`ESPressio-Radio` provides the hardware-agnostic physical/link transport boundary used by ESPressio.

Its responsibility is deliberately narrow: concrete `IRadio` implementations move bounded opaque physical packets to and from `RadioAddress` endpoints, while `RadioTransport` provides bounded **hop-local logical transfer** by fragmenting/reassembling complete opaque byte sequences over one explicitly selected radio interface and next-hop address. Radio owns no Mesh membership, topology, route selection, forwarding policy, device identity, authentication, Command/Event/State semantics, or application protocol meaning.

## Responsibility boundary

Outbound:

```text
higher layer (for example ESPressio-Mesh)
        -> selects one IRadio + next-hop RadioAddress
        -> RadioTransport bounded logical transfer
        -> IRadio physical/link send
        -> RF medium
```

Inbound:

```text
RF medium
        -> IRadio concrete bounded RX storage / hardware FIFO
        -> RadioWorker (PrecisionThread)
        -> RadioTransport bounded reassembly
        -> higher-layer receiver
```

A concrete `IRadio` knows only how to start/stop its technology, report its capabilities, expose its local RadioAddress, send bounded opaque physical/link bytes to another RadioAddress, and drain inbound physical packets into the worker-owned receiver. It must not parse ESPressio Mesh or conceptual primitives.

## RadioTransport: direct-link logical transfer only

`RadioTransport` does **not** contain a logical-node routing table and does **not** forward traffic. Every outbound call names the exact radio interface and next-hop `RadioAddress` chosen by the caller:

```cpp
transport.Send(radio, peerRadioAddress, bytes, size);
```

The service owns only:

- bounded hop-local fragmentation and reassembly;
- one finite `MaximumLogicalTransferSize(radio)` per interface;
- a bounded set of registered radio interfaces;
- bounded incomplete-reassembly state;
- bounded recently-completed transfer suppression; and
- delivery of one complete opaque logical byte sequence to `IRadioTransportReceiver`.

This is the architectural boundary required by ESPressio-Mesh: Mesh owns end-to-end routing, retries, forwarding, identities, hop limits and delivery semantics; Radio executes only the selected direct link.

The default generic logical-transfer ceiling is 4096 bytes and is compile-time bounded by `ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES`. A concrete provider can advertise a smaller `RadioCapabilities::MaximumLogicalTransferBytes`. The effective capability is also constrained by its physical MTU and the maximum 255-fragment Radio transfer framing.

Each RadioTransport fragment carries the sending `RadioAddress` inside the Radio-owned framing. This is intentional: technologies such as nRF24 do not expose the transmitter address when receiving a packet. When a concrete driver *does* provide `RadioPacketView::Source`, RadioTransport verifies that it agrees with the framed Radio source. This link endpoint remains strictly separate from `System::DeviceIdentifier` and every Mesh identity.

## RadioWorker

Inbound processing is owned by `RadioWorker`, which derives from ESPressio `PrecisionThread`.

`RadioWorker` does only three things:

1. service attached `IRadio` providers for available physical/link packets;
2. advance each packet into `RadioTransport`; and
3. notify supplemental physical-packet observers after RadioTransport has consumed the borrowed packet view.

It does not authenticate/decrypt messages, resolve routes, forward Mesh traffic, or inspect Command, Event, State or another conceptual primitive family.

Callback-driven providers such as ESP32 raw 802.11 copy accepted inbound packet data into bounded provider-owned queues and invoke only `IRadioWorkSignal::OnRadioWorkAvailable()`. That signal wakes the PrecisionThread; parsing/reassembly and observer notification therefore occur outside the hardware/driver callback. Providers without an asynchronous wake path may be serviced by the worker's bounded iteration cadence.

`RadioWorker::AddInterface()` registers the interface with RadioTransport and installs the worker as its inbound receiver/work signal. `RadioTransport::AddInterface()` itself records only the bounded Radio-layer registration; it does not install a competing receive path.

## Physical and logical capabilities

`RadioCapabilities` distinguishes the physical packet ceiling from the complete logical-transfer ceiling:

- `MaximumPayloadBytes` — maximum opaque bytes accepted by one concrete `IRadio::Send()` operation;
- `AddressBytes` — meaningful RadioAddress width for that technology;
- `MaximumLogicalTransferBytes` — optional concrete lower cap on complete RadioTransport transfers; zero means the generic bounded RadioTransport cap applies.

`RadioAddress` is opaque technology-specific link addressing. It is never a permanent device identifier, authentication claim, Mesh node identity or route authority.

## Precision clock exchange

`RadioClockSynchronizer` remains a separate link-local precision mechanism operating directly over `IRadio`, intentionally bypassing ordinary RadioTransport fragmentation/reassembly. This preserves the T1/T2/T3/T4 timestamp boundary and its uncertainty characteristics. Timing owns clock mathematics/discipline; Radio owns only direct-link timestamp mechanics and transport.

## Observer callback subscriptions

Radio integrates the typed, RTTI-free ESPressio Observable subscription model without changing ownership.

Concrete radios expose:

- `IRadioLifecycleObserver` — successful start/stop transitions;
- `IRadioPacketObserver` — physical/link packets after the worker has advanced them into RadioTransport;
- `IRadioSendObserver` — synchronous concrete-radio send completion/result.

`RadioTransport` exposes:

- `IRadioTransportLifecycleObserver` — logical-transfer service start/stop;
- `IRadioTransportInterfaceObserver` — interface registration/removal;
- `IRadioTransportMessageObserver` — complete logical-transfer send and receive observations.

The `IRadioReceiver` → `RadioWorker` → `RadioTransport` path remains the single inbound ownership path. `RadioTransport::SetReceiver()` remains the single complete-transfer delivery path. Observers are supplemental telemetry/composition surfaces only.

Observer callbacks are synchronous. Borrowed packet/transfer payload views are valid only for the duration of the callback. Optional asynchronous Event conversion is provided by `RadioEventBridge`; because Event delivery outlives the callback, that bridge takes one required owned payload snapshot using ESPressio-System memory policy.

## Concrete providers

The hardware-neutral interfaces live here. Concrete implementations belong with the technology/platform that owns them:

- `ESPressio-ESP32` — ESP32 integrated raw IEEE 802.11 radio;
- `ESPressio-ESP-Now` — ESP-NOW concrete where used as a Radio implementation;
- `ESPressio-NRF24` — nRF24L01/nRF24L01+ concrete;
- future LoRa/sub-GHz/802.15.4 providers may implement the same contract.

Platform-global resource coordination stays with the platform concrete. In particular, ESP32 raw 802.11 and ordinary Wi-Fi share the same physical Wi-Fi PHY; shared channel/power-state ownership therefore belongs in `ESPressio-ESP32`, not in this portable Radio layer.

## Minimal usage

```cpp
#include <ESPressio_Radio.hpp>

ESPressio::Radio::RadioTransport transport;
ESPressio::Radio::RadioWorker worker(transport);

worker.AddInterface(radio);
transport.SetReceiver(&higherLayerIngress);

transport.Start();
worker.Initialize();
worker.Start();

const uint8_t bytes[] = {1, 2, 3, 4};
transport.Send(radio, peerRadioAddress, bytes, sizeof(bytes));
```

The registered receiver gets a complete `RadioTransportMessageView` containing the source/destination Radio endpoints, Radio-local transfer identifier, flags and borrowed complete payload. A higher layer such as ESPressio-Mesh then applies authentication, membership, routing/delivery and primitive-family semantics according to its own contracts.

## Memory behaviour

All retained RadioTransport cardinalities are bounded. The default compile-time controls are:

- `ESPRESSIO_RADIO_MAX_INTERFACES = 4`;
- `ESPRESSIO_RADIO_MAX_REASSEMBLIES = 4`;
- `ESPRESSIO_RADIO_MAX_RECENT_TRANSFERS = 32`;
- `ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES = 4096`.

Each active reassembly may own at most one logical-transfer buffer up to the effective interface maximum. Reassembly payload storage uses `System::Memory::ByteVector<ExternalPreferred>`, so the installed ESPressio-System provider controls its actual memory region. The 256-fragment receipt bitmap is fixed at 32 bytes per reassembly slot.

Observable dispatcher ownership is obtained through `System::Memory::MakeShared<..., ExternalPreferred>()`; Radio does not bypass ESPressio-System with local platform allocation policy.

Concrete callback-driven providers are separately responsible for finite bounded RX storage and for documenting their physical queue/pool costs.

## Validation

Native tests exercise direct-link fragmentation/reassembly at small MTUs, provider-specific logical-transfer bounds, typed one-to-many Observable callbacks, lifecycle/interface/message observation and RAII unsubscription. Clock synchronization protocol tests validate the separate precision link path. ESP32 PlatformIO smoke validation compiles RadioWorker, optional Radio→Event integration and clock synchronization against the coordinated structural-realignment branches.

During the Mesh implementation tranche, participating dependencies are pinned to their matching `structural_realignment_propagation_ESPressio-Mesh` branches until reintegration.
