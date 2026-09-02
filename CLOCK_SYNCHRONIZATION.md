# Radio clock synchronization

`ESPressio-Radio` provides an optional `RadioClockSynchronizer` adapter between a concrete `IRadio` and ESPressio Timing. It does not change the responsibility of `IRadio` or `RadioTransport`: radios still transport opaque bytes, while ESPressio Timing owns offset/delay estimation, filtering, drift learning, synchronization state, and clock discipline.

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

For receive timestamps, a concrete radio advertising `RadioCapability::ReceiveTimestamp` must place a timestamp from the active `System::Clock::Monotonic()` nanosecond domain into `RadioPacketView::ReceiveTimestampNanoseconds`, captured as close to physical reception as the driver permits. The synchronizer reconstructs T2/T4 in the Timing target's System Clock domain by subtracting the elapsed monotonic time between the recorded receive instant and processing time.

If a radio does not provide receive timestamps, synchronization may still operate with a lower-quality fallback captured when the packet reaches the Radio observer on `RadioWorker`. Set `RadioClockSynchronizationConfig::RequireReceiveTimestamp=true` when an application prefers initialization to fail rather than accept this reduced precision.

## Why synchronization is carried directly by IRadio

Clock synchronization is deliberately link-local rather than a `RadioTransport` logical message. The synchronization response is exactly 32 bytes: an 8-byte control header followed by T1, T2, and T3. This fits an nRF24 packet exactly and avoids fragmentation, forwarding, reassembly, and route-selection latency contaminating the synchronization exchange.

The request is 25 bytes. In addition to T1 it carries the client's opaque `RadioAddress`. This is required for radios such as nRF24 whose receive hardware does not expose the transmitter address. If `RadioPacketView::Source` is available, it must agree with the embedded requester address and takes precedence. If it is unavailable, the reference replies to the embedded address.

The link-control wire magic is distinct from `RadioTransport` framing. Production `RadioWorker` therefore advances the packet to `RadioTransport` first; it is ignored as non-transport framing, after which the synchronizer observes the unchanged borrowed packet through `IRadioPacketObserver`.

## Roles and configuration

`RadioClockSynchronizationMode` supports:

- `Disabled`
- `Client`
- `Reference`
- `ClientAndReference`

A client supplies the reference's link-layer `RadioAddress`. `SynchronizationIntervalMilliseconds` controls `Update()`-driven synchronization attempts; zero disables automatic requests. `AdjustmentMode` is passed unchanged to ESPressio Timing. Failed sends consume the current synchronization interval so temporary `Busy`/`NoMemory` pressure cannot turn into a tight retry loop.

## Security boundary

Clock synchronization establishes a timing sample, not trust. `RadioClockSynchronizer` does not authenticate the reference or payload and must not be interpreted as establishing authenticated time. Systems requiring a trusted time source must establish that trust through the chosen radio/link/security architecture independently of Timing's offset/delay calculations.

## Radio capability and accuracy

Any `IRadio` with a payload capacity of at least 32 bytes and usable link addressing can carry the protocol. Accuracy depends on the concrete provider:

- providers with driver-proximate receive timestamps in the common monotonic domain avoid RadioWorker/RadioTransport scheduling latency in T2/T4;
- providers without receive timestamps remain functionally usable with the fallback, but their samples include software scheduling latency and jitter;
- `RequireReceiveTimestamp` can be used to reject the latter configuration.

Current ESP32 raw IEEE 802.11 already advertises `ReceiveTimestamp` and captures the timestamp in its receive callback. The current nRF24 provider has a 32-byte MTU and does not expose a receive timestamp or transmitter source address; the generic wire protocol explicitly accommodates both limitations.

## Example

```cpp
#include <ESPressio_Radio.hpp>

using namespace ESPressio;

Radio::RadioClockSynchronizer synchronizer(radio);

Radio::RadioClockSynchronizationConfig config;
config.Mode = Radio::RadioClockSynchronizationMode::Client;
config.ReferencePeer = referenceRadioAddress;
config.SynchronizationIntervalMilliseconds = 1000;
config.AdjustmentMode = Timing::ClockSynchronizationAdjustmentMode::SlewOnly;
config.RequireReceiveTimestamp = true;

if (synchronizer.Initialize(config)) {
    // Call periodically from the application's existing worker/control loop.
    synchronizer.Update();
}
```

Reference nodes use the same class with `Mode=Reference`. A reference does not require a preconfigured client address; the request carries enough link-layer information to return the response even on source-less receive hardware such as nRF24.

`GetSynchronizationStatus()` forwards the Timing target's current status. `GetStatistics()` provides lightweight request/response/send/sample/fallback counters for diagnostics without altering Timing discipline state.
