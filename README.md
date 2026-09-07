# ESPressio-Radio

`ESPressio-Radio` provides the hardware-agnostic physical/link transport boundary used by ESPressio.

Its responsibility is deliberately narrow: concrete `IRadio` implementations move bounded opaque physical packets to and from `RadioAddress` endpoints, while `RadioTransport` provides bounded **hop-local logical transfer** by fragmenting/reassembling complete opaque byte sequences over one explicitly selected radio interface and next-hop address. Radio owns no Mesh membership, topology, route selection, forwarding policy, device identity, authentication, Command/Event/State semantics, or application protocol meaning.

## Responsibility boundary

Outbound:

```text
higher layer (for example ESPressio-Mesh)
        -> selects one IRadio + next-hop RadioPeerHandle/RadioAddress
        -> RadioTransport bounded logical transfer
        -> IRadio physical/link send
        -> RF medium
```

Ordinary inbound traffic:

```text
RF medium
        -> IRadio concrete bounded RX storage / hardware FIFO
        -> RadioWorker (PrecisionThread)
        -> RadioTransport bounded reassembly
        -> higher-layer receiver
```

Latency-critical Radio-owned control traffic may instead use a provider's physically separate prioritized ingress:

```text
RF / driver callback
        -> generic opaque-payload classification
        -> CONTROL provider queue
        -> RadioControlWorker (independent PrecisionThread)
        -> registered IRadioControlProtocol
```

The current `RadioClockSynchronizer` uses this control lifecycle. It no longer waits behind ordinary `RadioTransport` traffic when the provider implements `IRadioPrioritizedIngress`.

A concrete `IRadio` knows only how to start/stop its technology, report its capabilities, expose its local `RadioAddress`, send bounded opaque physical/link bytes to another `RadioAddress`, and drain inbound physical packets into the worker-owned receiver. It must not parse ESPressio Mesh or conceptual primitives. A concrete implementing `IRadioPrioritizedIngress` still remains protocol-agnostic: it asks the injected classifier whether opaque bytes are `Standard` or `Control` rather than learning clock or Mesh semantics itself.

## Send admission and direct-link evidence

Radio deliberately distinguishes **submission/admission** from facts a technology can actually prove about the physical/link transmission.

`IRadio::Send()` returns `RadioSendResult`. `RadioSendStatus::Accepted` means the provider accepted the packet send operation. It does **not** by itself mean that RF transmission completed and does not mean that a peer acknowledged the packet.

Stronger facts, when genuinely available, are carried separately in `RadioSendResult::Evidence`:

- `RadioTransmissionCompletion::Unknown` — the provider cannot prove completion at `Send()` return time;
- `RadioTransmissionCompletion::Completed` — the provider can prove the physical/link transmission completed;
- `RadioPeerAcknowledgement::Unavailable` — that bearer/operation has no qualifying peer acknowledgement;
- `RadioPeerAcknowledgement::Unknown` — acknowledgement state is not established;
- `RadioPeerAcknowledgement::Acknowledged` — the provider can prove a qualifying link-layer peer acknowledgement.

For example, the current nRF24 concrete uses synchronous `RF24::write()` evidence: successful unicast can report transmission completed + peer acknowledged, while broadcast can report transmission completed without peer acknowledgement. ESP32 Raw80211 currently reports only submission acceptance because `esp_wifi_80211_tx()` returning success does not establish a qualifying peer acknowledgement. The BLE legacy-advertising concrete queues asynchronous advertising work and likewise reports only immediate submission acceptance.

These are **Radio/link facts only**. Even `Completed + Acknowledged` is not an ESPressio-Mesh delivery acknowledgement and does not prove that a peer Mesh stack authenticated, validated, accepted or forwarded a Mesh message.

`RadioTransportSendResult` applies the same distinction to a complete logical transfer. A fragmented transfer reports `TransmissionCompleted` only when every fragment synchronously established completion, and reports `PeerAcknowledged` only when every fragment established acknowledgement. Otherwise `Accepted` remains admission of the complete fragment set, not logical Mesh delivery.

## RadioTransport: direct-link logical transfer only

`RadioTransport` does **not** contain a logical-node routing table and does **not** forward traffic. Every outbound operation resolves to one exact direct Radio peer. The preferred higher-layer path uses a generation-safe `RadioPeerHandle`:

```cpp
transport.Send(peerHandle, bytes, size);
```

The lower Radio-facing overload remains available for Radio-layer composition and takes the exact interface/address pair:

```cpp
transport.Send(radio, peerRadioAddress, bytes, size);
```

The service owns only:

- bounded hop-local fragmentation and reassembly;
- one finite `MaximumLogicalTransferSize(radio)` per interface;
- a bounded set of registered radio interfaces;
- generation-safe bounded direct-peer bindings;
- bounded incomplete-reassembly state;
- bounded recently-completed transfer suppression; and
- delivery of one complete opaque logical byte sequence to `IRadioTransportReceiver`.

This is the architectural boundary required by ESPressio-Mesh: Mesh owns end-to-end routing, retries, forwarding, identities, hop limits and delivery semantics; Radio executes only the selected direct link.

The default generic logical-transfer ceiling is 4096 bytes and is compile-time bounded by `ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES`. A concrete provider can advertise a smaller `RadioCapabilities::MaximumLogicalTransferBytes`. The effective capability is also constrained by its physical MTU and the maximum 255-fragment Radio transfer framing.

Each RadioTransport fragment carries the sending `RadioAddress` inside the Radio-owned framing. This is intentional: technologies such as nRF24 do not expose the transmitter address when receiving a packet. When a concrete driver *does* provide `RadioPacketView::Source`, RadioTransport verifies that it agrees with the framed Radio source. This link endpoint remains strictly separate from `System::DeviceIdentifier` and every Mesh identity.

## RadioPeerHandle

`RadioPeerHandle` is a compact generation-safe process-local capability issued by `RadioPeerRegistry`. It resolves only inside the owning Radio service to one `IRadio* + RadioAddress` binding.

A peer handle is never a `DeviceIdentifier`, membership identity, route authority or distributed value. Slot reuse advances its generation so a stale handle cannot resolve to a replacement peer. Explicit invalidation, interface removal and transport shutdown invalidate matching peer handles and emit peer-lifecycle observations before the binding disappears.

Higher layers may associate an authenticated identity with a current peer handle in their own bounded state, but Radio never constructs or interprets that identity.

## RadioWorker and RadioControlWorker

Ordinary inbound processing is owned by `RadioWorker`, which derives from ESPressio `PrecisionThread`.

`RadioWorker` does three things for **standard** ingress:

1. service attached `IRadio` providers for available standard physical/link packets;
2. advance each packet into `RadioTransport`; and
3. notify supplemental physical-packet observers after RadioTransport has consumed the borrowed packet view.

It does not authenticate/decrypt messages, resolve routes, forward Mesh traffic, or inspect Command, Event, State or another conceptual primitive family.

`RadioControlWorker` is a separate `PrecisionThread` for bounded latency-critical Radio-owned protocols. It implements the generic `IRadioIngressClassifier`, receives/drains a provider's control queue through `IRadioPrioritizedIngress`, and invokes registered `IRadioControlProtocol` implementations. Protocol matching in the provider callback must remain bounded, allocation-free, non-blocking and `noexcept`; actual control work executes later on the control worker.

Callback-driven providers such as ESP32 Raw80211 copy accepted inbound packet data into bounded provider-owned queues and invoke only the relevant `IRadioWorkSignal::OnRadioWorkAvailable()`. Those signals wake their worker threads; parsing/reassembly, control-protocol processing and observer notification therefore occur outside the hardware/driver callback. Providers without an asynchronous wake path may be serviced by their worker's bounded iteration cadence.

`RadioWorker::AddInterface()` registers the interface with RadioTransport and installs the worker as its standard inbound receiver/work signal. `RadioControlWorker::AddInterface()` binds a provider's optional prioritized-control extension; `RegisterProtocol()` then associates one bounded control protocol with that Radio. These registrations are composition-time operations and should occur before the Radio starts.

Registration does not require a local link address yet: concrete providers such as ESP32 Raw80211 may resolve their hardware address during `Start()`. `RadioTransport::Start()` validates that every started interface then exposes a valid address and rolls the started set back on failure. `RadioTransport::AddInterface()` itself records only the bounded Radio-layer registration; it does not install a competing receive path.

Both workers expose aggregate service-latency statistics. The standard worker additionally measures the duration of `RadioTransport + observer` processing; the control worker measures registered protocol processing. These metrics allow a physical composition to distinguish RF/provider timestamp -> worker scheduling delay from work performed after the worker begins servicing the packet.

## Physical and logical capabilities

`RadioCapabilities` distinguishes the physical packet ceiling from the complete logical-transfer ceiling:

- `MaximumPayloadBytes` — maximum opaque bytes accepted by one concrete `IRadio::Send()` operation;
- `AddressBytes` — meaningful `RadioAddress` width for that technology;
- `MaximumLogicalTransferBytes` — optional concrete lower cap on complete RadioTransport transfers; zero means the generic bounded RadioTransport cap applies.

`RadioAddress` is opaque technology-specific link addressing. It is never a permanent device identifier, authentication claim, Mesh node identity or route authority.

## Precision clock exchange

`RadioClockSynchronizer` is a link-local `IRadioControlProtocol`, intentionally separate from ordinary RadioTransport fragmentation/reassembly and packet observers. It preserves the T1/T2/T3/T4 timestamp boundary while `RadioControlWorker` owns synchronization cadence and timeout service independently of Mesh/application traffic.

A provider implementing `IRadioPrioritizedIngress` can physically split clock/control packets into a dedicated bounded queue before ordinary `RadioWorker`. The current ESP32 Raw80211 concrete does this and can require provider-proximate receive timestamps for the sub-millisecond Mesh Lab.

The protocol itself still uses `IRadio` addressing/send semantics, but MTU compatibility alone does not imply scheduling isolation. A provider without prioritized ingress needs another dedicated control-ingress composition if it is to obtain the same latency separation.

Current clock response size is 32 bytes. The ESP32 legacy-advertising BLE concrete exposes only a 20-byte physical payload and therefore cannot carry this exact exchange without a different framing strategy.

See [`CLOCK_SYNCHRONIZATION.md`](CLOCK_SYNCHRONIZATION.md) for the wire protocol, provider requirements and control-worker composition.

## Observer callback subscriptions

Radio integrates the typed, RTTI-free ESPressio Observable subscription model without changing ownership.

Concrete radios expose:

- `IRadioLifecycleObserver` — successful start/stop transitions;
- `IRadioPacketObserver` — **standard** physical/link packets after the standard worker has advanced them into RadioTransport;
- `IRadioSendAttemptObserver` — synchronous return of one concrete-radio `Send()` attempt. The callback itself is **not** a transmission-completion signal; inspect `RadioSendResult::Evidence` for any stronger fact.

Latency-critical packets removed into a provider's dedicated Control queue are processed by `IRadioControlProtocol`; they are not required to traverse the ordinary packet-observer chain.

`RadioTransport` exposes:

- `IRadioTransportLifecycleObserver` — logical-transfer service start/stop;
- `IRadioTransportInterfaceObserver` — interface registration/removal;
- `IRadioTransportPeerObserver` — generation-safe peer observation/invalidation;
- `IRadioTransportMessageObserver` — complete logical-transfer send-attempt return and complete inbound logical-transfer observations. `OnRadioTransportSendAttempted` reports synchronous attempt return only; inspect `RadioTransportSendResult::LinkResult.Evidence` for stronger link evidence.

The `IRadioReceiver` -> `RadioWorker` -> `RadioTransport` path remains the single ordinary complete-transfer ownership path. `RadioTransport::SetReceiver()` remains the single complete-transfer delivery path. Observers are supplemental telemetry/composition surfaces only.

Observer callbacks are synchronous. Borrowed packet/transfer payload views are valid only for the duration of the callback. Optional asynchronous Event conversion is provided by `RadioEventBridge`; because Event delivery outlives the callback, that bridge takes one required owned payload snapshot using ESPressio-System memory policy.

## Concrete providers

The hardware-neutral interfaces live here. Concrete implementations belong with the technology/platform that owns them:

- `ESPressio-ESP32` — ESP32 integrated Raw80211 and BLE Radio concretes;
- `ESPressio-ESP-Now` — ESP-NOW concrete where used as a Radio implementation;
- `ESPressio-NRF24` — nRF24L01/nRF24L01+ concrete;
- future LoRa/sub-GHz/802.15.4 providers may implement the same contract.

Platform-global resource coordination stays with the platform concrete. In particular, ESP32 raw 802.11 and ordinary Wi-Fi share the same physical Wi-Fi PHY; shared channel/power-state ownership therefore belongs in `ESPressio-ESP32`, not in this portable Radio layer.

## Minimal standard-traffic usage

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
auto result = transport.Send(peerHandle, bytes, sizeof(bytes));

if (result && result.LinkResult.Evidence.TransmissionCompleted()) {
    // Radio proved transmission completion. This still is not a Mesh delivery ACK.
}
```

The registered receiver gets a complete `RadioTransportMessageView` containing the Radio-owned source peer handle, source/destination Radio endpoints, Radio-local transfer identifier, flags and borrowed complete payload. A higher layer such as ESPressio-Mesh then applies authentication, membership, routing/delivery and primitive-family semantics according to its own contracts.

## Memory behaviour

All retained RadioTransport cardinalities are bounded. The default compile-time controls are:

- `ESPRESSIO_RADIO_MAX_INTERFACES = 4`;
- `ESPRESSIO_RADIO_MAX_REASSEMBLIES = 4`;
- `ESPRESSIO_RADIO_MAX_RECENT_TRANSFERS = 32`;
- `ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES = 4096`.

`RadioPeerRegistry` is also finite; its default capacity is controlled independently by `ESPRESSIO_RADIO_MAX_PEERS` (currently 32) so technologies/integrations may choose a smaller bound when appropriate.

`RadioControlWorker` also uses compile-time bounded interface/protocol registration arrays (`ESPRESSIO_RADIO_CONTROL_MAX_INTERFACES` and `ESPRESSIO_RADIO_CONTROL_MAX_PROTOCOLS`). Concrete prioritized providers own the finite physical Control/Standard ingress queues and expose depth/high-water/drop statistics where implemented.

Each active reassembly owns one compile-time fixed payload array of `ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES`; no receive-time heap allocation or fallback exists. `RadioTransport::ReassemblyPayloadCapacityBytes` exposes the exact aggregate payload-array capacity (`ESPRESSIO_RADIO_MAX_REASSEMBLIES x ESPRESSIO_RADIO_MAX_LOGICAL_TRANSFER_BYTES`) for whole-device accounting. When every reassembly slot is occupied, a new transfer is dropped without evicting or corrupting an in-progress transfer. The 256-fragment receipt bitmap is fixed at 32 bytes per reassembly slot.

Observable dispatcher ownership is obtained through `System::Memory::MakeShared<..., ExternalPreferred>()`; Radio does not bypass ESPressio-System with local platform allocation policy.

Concrete callback-driven providers are separately responsible for finite bounded RX/TX storage and for documenting their physical queue/pool costs.

## Validation

Native tests exercise direct-link fragmentation/reassembly at small MTUs, provider-specific logical-transfer bounds, qualified direct-link evidence aggregation, typed one-to-many Observable callbacks, send-attempt semantics, peer lifecycle, and RAII unsubscription. Clock synchronization tests validate the separate control-protocol exchange, strict receive-timestamp behavior, source-less addressing and one-outstanding-exchange semantics.

The normal ESP32 PlatformIO smoke surface includes the Radio umbrella, RadioWorker, peer registry, optional Radio->Event integration and clock synchronization against the coordinated structural-realignment branches. During the current Mesh tranche, Actions execution may be unavailable due external budget limits; source presence in the workflow must not be described as a green run.

During the Mesh implementation tranche, participating dependencies are pinned to their matching `structural_realignment_propagation_ESPressio-Mesh` branches until reintegration.
