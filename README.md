# ESPressio-Radio

`ESPressio-Radio` is the hardware-neutral physical/link transport layer for ESPressio. It owns bounded physical-provider integration, hop-local v3 fragmentation/reassembly, contention-domain scheduling, Clock transport, generation-safe direct-peer handles, and the family-opaque logical-transfer runtime consumed by higher layers.

Radio does **not** own Mesh membership, routing, authentication, Primitive family semantics, Command/Event/State policy, or application protocol meaning.

## Final architecture

The canonical outbound path is:

```text
family / RadioAdapter / Mesh
        -> RadioRuntime logical transfer submission
        -> RadioDomainScheduler (R3)
        -> one managed IRadio provider
        -> physical bearer
```

The canonical inbound path is:

```text
physical bearer
        -> managed IRadio finite ingress storage
        -> RadioIngressRouter (single IRadioReceiver owner)
        -> Clock direct-frame classification OR RadioTransport-v3 reassembly
        -> RadioRuntime ready handle / TakeInbound()
        -> higher layer
```

There is no predecessor `RadioWorker`, `RadioControlWorker`, `PrecisionThread`, Event bridge, Observable callback architecture, or monolithic v2 `RadioTransport` in the canonical branch.

## Managed provider contract

Concrete bearers implement `IRadio`. A managed provider exposes:

- finite start/stop lifecycle;
- local opaque `RadioAddress`;
- one `RadioContentionDomainId`;
- physical payload and logical-transfer capability facts;
- deterministic provider resource profile;
- readiness through `IsTransmitReady()` plus runtime wake notification;
- conservative transmission cost when promotable deadlines are supported;
- synchronous or deferred terminal transmission evidence;
- bounded `ServiceInbound()` quanta;
- optional capture-time receive timestamp evidence.

`Send()` admission is not automatically transmission completion. Deferred providers return a generation/correlation-safe `RadioTransmissionHandle` and later resolve it through the installed `IRadioRuntimeSink`. Providers must never claim peer acknowledgement or timing quality they cannot prove.

## Service classes and R3 arbitration

Radio uses the frozen six-class service taxonomy:

1. Infrastructure
2. Clock
3. Critical
4. Responsive
5. Convergent
6. BestEffort

Every physical transmission in one contention domain passes through the same `RadioDomainScheduler`. R3 combines finite per-class FIFO queues, weighted deficit round-robin accounting, bounded promotable-deadline debt, provider-reported cost and one scheduler-owned outstanding physical operation.

Clock does **not** own a privileged worker or bypass queue. A Clock record uses the normal Clock Q1 capacity and normal R3 arbitration. Its direct physical frame may skip v3 fragmentation, but still uses the same scheduler, provider-cost model, completion correlation and contention-domain execution context.

## Radio-local Q1 capacity

Inbound and outbound capacity are fixed compile-time planes built from complete record + byte ownership domains. Protected per-class capacity is isolated from `SharedOverflow`; untrusted ingress has a physically separate inbound quarantine domain.

A logical transfer becomes visible only after its record and byte lease are complete and committed. No hidden heap fallback or unbounded queue exists in the Radio hot path.

`ESPressio_RadioResources.hpp` exposes deterministic target-specific accounting for configured capacity planes, scheduler/runtime objects, queue topology, worker stack configuration, provider slots and provider-hidden resource declarations. Component values that overlap aggregate `sizeof` values are reported separately rather than summed misleadingly.

## RadioTransport v3 wire

Ordinary hop-local logical transfers use v3 framing. The fixed portion is 15 bytes, followed by the Radio source address and fragment payload.

The header carries:

- wire magic and version;
- Radio-local transfer id;
- fragment index/count;
- complete logical payload length;
- opaque source Radio address;
- service class;
- remaining residence in milliseconds.

Remaining residence is finite and non-increasing. Reassembly is bounded, duplicate-aware and generation-safe. Untrusted completed traffic remains quarantined until a higher trust layer validates it and calls promotion with the **same** service class carried by the authenticated transfer. A service-class mismatch is malformed and cannot self-promote into protected capacity.

The exact maximum logical payload is derived from physical MTU, source-address width and the 255-fragment limit. For example, a 32-byte nRF24 frame with a five-byte source supports 3060 logical bytes under v3.

## Family-opaque RadioRuntime

`RadioRuntime` is the stable handoff consumed by RadioAdapters/MeshAdapters. It owns no Event/Command/State knowledge.

It provides:

- fixed domain/provider registration;
- generation-safe direct peers;
- outbound submission by direct peer;
- terminal logical-transfer results;
- inbound ready handles;
- explicit `TakeInbound()` ownership transfer;
- quarantine promotion;
- controlled shutdown and volatile-generation invalidation.

Higher layers map this neutral boundary into A2/family semantics. Radio never performs that mapping itself.

## Clock synchronization

Clock synchronization is implemented by `RadioClockCoordinator` plus compact direct Clock frames. It uses Timing's adaptive `NextRequiredSynchronizationMonotonic()` deadline rather than a fixed periodic worker.

The four-capture model remains T1/T2/T3/T4. T1/T3 are late-captured immediately before the normal R3 provider send. T2/T4 are accepted only from provider receive evidence whose capture-time Timing model can map the historical monotonic coordinate into System time without reconstructing it from a later mutable clock.

The compact request is at most 17 bytes and the response is exactly 32 bytes. The response preserves both remote System and remote monotonic T2→T3 chronology so Timing receives genuine four-capture ordering rather than fabricated coordinates.

See [`CLOCK_SYNCHRONIZATION.md`](CLOCK_SYNCHRONIZATION.md).

### Precision claims

Wire compatibility is not precision certification. A bearer may participate in ordinary Radio traffic while remaining ineligible for certified sub-millisecond Clock synchronization.

Current provider evidence boundaries are deliberately conservative:

- ESP32 Raw80211 uses managed finite ingress and real ESP-IDF raw-TX completion, but its present receive timing remains **Estimated** until a conservative physical capture bound is established.
- ESP32 BLE legacy advertising is broadcast-only, has a 26-byte opaque v3-capable payload, and does not claim Clock-qualified timing.
- nRF24 provides synchronous terminal TX and genuine unicast link ACK evidence, but does not currently provide a bounded Clock receive timestamp.
- ESP-NOW uses bounded callback capture and deferred native send completion. Its default receive timing is **Estimated**; finite-bounded K1/K2 evidence is available only when composition supplies `ESPNowRadioTimingCapture` with a conservative callback-boundary uncertainty. Successful unicast MAC completion is direct-link acknowledgement only, never Primitive admission.

Therefore Tranche 7/9 establishes the architecture required for sub-millisecond synchronization, but does **not** claim that every concrete provider has completed physical characterization/certification.

## Concrete providers

Concrete implementations live with their platform/technology owner:

- `ESPressio-ESP32` — Raw80211 and BLE;
- `ESPressio-NRF24` — nRF24L01/nRF24L01+;
- `ESPressio-ESP-Now` — ESP32 ESP-NOW physical provider;
- later physical providers implement the same managed `IRadio` contract.

ESP32 Raw80211, ESP-NOW and ordinary Wi-Fi share one physical Wi-Fi PHY. Their contention-domain/channel/readiness/power-state coordination must therefore reflect the shared hardware rather than modelling them as independent media.

## Direct dependencies

The final Radio dependency boundary is exactly:

```text
ESPressio-System
ESPressio-Task
ESPressio-Timing
ESPressio-Units
```

Radio has no direct Event, Observable, Threads, Command, State, Mesh, Adapters or platform dependency.

## Validation

The `primitives_redesign` workflow validates:

- canonical umbrella/dependency eradication;
- managed-provider contract;
- exact v3 wire vectors;
- Clock wire/capture and adaptive coordinator behavior;
- Q1 capacity isolation and deterministic accounting;
- bounded reassembly and quarantine promotion;
- malformed/truncated/spoofed ingress rejection;
- R3 DRR/deadline behavior including Clock under saturated BestEffort load;
- single-owner ingress routing;
- cooperative one-Task domain runtime;
- family-opaque `RadioRuntime` lifecycle and peer semantics.

Provider repositories have their own target compilation gates. No version number, tag or release is changed by this structural tranche.
