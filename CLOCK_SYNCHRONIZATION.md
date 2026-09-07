# Radio clock synchronization

`ESPressio-Radio` provides an optional `RadioClockSynchronizer` protocol between a concrete `IRadio` and ESPressio Timing. It does not change the responsibility of either abstraction: concrete Radios still transport opaque link packets, while ESPressio Timing owns offset/delay estimation, filtering, drift learning, synchronization state, and clock discipline.

Clock synchronization is deliberately **not** a `RadioTransport` logical transfer. Time-critical clock frames are eligible for the independent Radio control lifecycle so ordinary fragmentation, Mesh traffic, Events, State, Commands, and application backlog do not sit in front of T2/T3/T4 processing.

## Four-timestamp exchange

The synchronization exchange preserves the established ESPressio four-timestamp semantics:

```text
Client                                      Reference

T1 request transmit  --------------------->
                              T2 request receive
                              T3 response transmit
                     <---------------------
T4 response receive
```

The completed `Timing::ClockSynchronizationSample` contains T1, T2, T3, and T4 and is submitted to the configured `Timing::IClockSynchronizationTarget`. By default the target is `Timing::SystemClock<>`.

T1 is captured immediately before the request is handed to `IRadio::Send()`. T3 is captured immediately before the reference response is handed to `IRadio::Send()`.

For receive timestamps, a concrete Radio advertising `RadioCapability::ReceiveTimestamp` must place a timestamp from the active `System::Clock::Monotonic()` nanosecond domain into `RadioPacketView::ReceiveTimestampNanoseconds`, captured as close to physical reception as the driver permits. The synchronizer reconstructs T2/T4 in the Timing target's System Clock domain by subtracting the elapsed monotonic time between the recorded receive instant and control-worker processing time.

If a provider cannot supply a receive timestamp, the synchronizer can use processing time as a lower-quality fallback when `RequireReceiveTimestamp=false`. Set `RadioClockSynchronizationConfig::RequireReceiveTimestamp=true` for precision-sensitive deployments so initialization fails rather than silently claiming timestamp quality the bearer cannot provide.

## Dedicated control lifecycle

The time-critical path is intentionally separate from ordinary `RadioWorker` processing:

```text
RF / driver receive callback
        |
        +--> provider-proximate receive timestamp
        +--> bounded opaque-payload classification
        |
        +--> CONTROL ingress queue
        |       |
        |       +--> RadioControlWorker
        |               |
        |               +--> IRadioControlProtocol
        |                        |
        |                        +--> RadioClockSynchronizer
        |
        +--> STANDARD ingress queue
                |
                +--> RadioWorker
                        |
                        +--> RadioTransport
                        +--> higher-layer traffic
```

`RadioClockSynchronizer` implements `IRadioControlProtocol`:

- `MatchesControlFrame(...)` performs only bounded wire recognition suitable for provider callback classification;
- `ProcessControlPacket(...)` performs request/response work on the control worker;
- `ServiceControl()` owns synchronization cadence and unanswered-exchange expiry.

It is **not** an `IRadioPacketObserver`, and its control frames do not need to enter `RadioTransport` first.

`RadioControlWorker` is an independent `PrecisionThread`. It is both the generic ingress classifier and the control receiver/work-signal target for providers implementing `IRadioPrioritizedIngress`. The worker drains control ingress first, then services registered protocol cadence/timeouts. A composition can configure a higher thread priority and shorter fallback cadence than the ordinary `RadioWorker`.

Concrete providers remain protocol-agnostic. A Raw80211 provider, for example, does not know clock wire magic; it asks the injected `IRadioIngressClassifier` whether an opaque packet is `Control` or `Standard` and places it in the corresponding bounded queue.

### Provider requirement for physically separated ingress

The protocol itself remains expressed in terms of `IRadio`, but **physical queue/lifecycle separation requires a provider or adapter that implements `IRadioPrioritizedIngress`**. The current ESP32 Raw80211 concrete does so.

A provider without prioritized ingress must not be described as having the same scheduling isolation merely because it can carry the 25/32-byte wire packets. It may support the protocol through another dedicated control-ingress composition, but ordinary `RadioWorker` processing is no longer the built-in precision path.

## Why synchronization is link-local

The synchronization response is exactly 32 bytes: an 8-byte control header followed by T1, T2, and T3. The request is 25 bytes and additionally carries the client's opaque `RadioAddress`.

Keeping this protocol link-local avoids `RadioTransport` fragmentation, forwarding, reassembly, and higher-layer route-selection latency contaminating the four-timestamp exchange.

The embedded requester address is required for Radios such as nRF24 whose receive hardware may not expose the transmitter address. If `RadioPacketView::Source` is available, it must agree with the embedded requester address and takes precedence. If it is unavailable, the reference replies to the embedded address.

The clock wire magic is intentionally distinct from ordinary RadioTransport framing and is recognized only by the registered control protocol classifier.

## Roles and configuration

`RadioClockSynchronizationMode` supports:

- `Disabled`
- `Client`
- `Reference`
- `ClientAndReference`

A client supplies the reference's link-layer `RadioAddress`. `SynchronizationIntervalMilliseconds` controls the cadence owned by `ServiceControl()`; zero disables automatic requests. `AdjustmentMode` is passed unchanged to ESPressio Timing.

At most one client exchange is outstanding. An explicit request while another is pending returns `RadioSendStatus::Busy`. When a full synchronization interval elapses with an unanswered exchange, periodic control service expires that sequence before issuing the next request. A delayed old response therefore cannot replace or be mis-correlated with a newer exchange.

Mesh integrations may select/authenticate the reference relationship and reconfigure the synchronizer accordingly. Mesh does **not** execute synchronization cadence; that remains a Radio control-lifecycle responsibility.

## Security boundary

Clock synchronization establishes a timing sample, not trust. `RadioClockSynchronizer` does not itself authenticate the reference or payload and must not be interpreted as establishing authenticated time. Systems requiring a trusted time source must establish that trust through the chosen link/Mesh/security architecture independently of Timing's offset/delay calculations.

## Capability and accuracy

Wire compatibility requires a physical payload capacity of at least 32 bytes and usable link addressing. Precision additionally depends on the provider and scheduling path:

- provider-proximate receive timestamps in the common monotonic domain remove worker scheduling delay from T2/T4 reconstruction;
- physically separate prioritized ingress prevents ordinary packet backlog from delaying clock processing/response generation;
- providers without receive timestamps can operate only with lower-quality processing-time fallback when allowed;
- `RequireReceiveTimestamp` rejects that lower-quality configuration.

Current ESP32 Raw80211 advertises `ReceiveTimestamp`, captures/maps the ESP-IDF receive timestamp near the driver boundary, and implements separate bounded Control and Standard queues. This is the current physical-Lab precision bearer.

The current ESP32 legacy-advertising BLE Radio has a 20-byte physical payload ceiling and therefore cannot carry the 32-byte response without a different clock framing strategy; it must not be presented as supporting this exact exchange merely because it is an `IRadio`.

The nRF24 physical MTU is 32 bytes, but scheduling isolation depends on the concrete/provider composition. MTU compatibility alone does not imply `IRadioPrioritizedIngress` support.

## Example: prioritized provider

```cpp
#include <ESPressio_Radio.hpp>

using namespace ESPressio;

Radio::RadioTransport transport;
Radio::RadioWorker standardWorker(transport);
Radio::RadioControlWorker controlWorker({1U, 1U, 4U, -1});
Radio::RadioClockSynchronizer synchronizer(radio);

// radio must implement IRadioPrioritizedIngress for this physically split path.
auto& prioritized = static_cast<Radio::IRadioPrioritizedIngress&>(radio);

standardWorker.AddInterface(radio);
controlWorker.AddInterface(radio, prioritized);
controlWorker.RegisterProtocol(radio, synchronizer);

Radio::RadioClockSynchronizationConfig config;
config.Mode = Radio::RadioClockSynchronizationMode::Client;
config.ReferencePeer = referenceRadioAddress;
config.SynchronizationIntervalMilliseconds = 1000;
config.AdjustmentMode = Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
config.RequireReceiveTimestamp = true;

// Configure the protocol relationship before it can generate client work.
if (!synchronizer.Initialize(config)) {
    // configuration/capability failure
}

controlWorker.Start();
standardWorker.Start();
transport.Start();
```

In a Mesh composition the relationship is normally configured by the Mesh clock coordinator after an authenticated direct parent is selected. `RadioControlWorker` remains responsible for calling `ServiceControl()` independently of Mesh/application traffic.

Reference nodes use the same synchronizer with `Mode=Reference` and no preconfigured client address.

## Diagnostics

`GetSynchronizationStatus()` forwards the Timing target's current status. `GetStatistics()` provides request/response/send/sample/timestamp-fallback counters.

`RadioControlWorker::GetStatistics()` additionally provides provider-RX-timestamp-to-control-service latency and control-protocol processing-duration statistics. `IRadioPrioritizedIngress` exposes independent Control/Standard accepted/drop/depth/high-water snapshots where the provider implements them.

These aggregate diagnostics are intentionally preferable to synchronous logging inside the time-critical callback/control path.
