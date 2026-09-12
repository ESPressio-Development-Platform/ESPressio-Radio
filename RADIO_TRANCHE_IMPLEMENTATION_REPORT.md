# ESPressio-Radio — Tranche 7 Implementation Report

## Status

**Tranche 7 structural implementation: COMPLETE.**

This report closes the Radio/provider redesign work package against the locked Primitive Platform redesign architecture. It does not authorize versioning, main-branch reintegration, tags, releases or Wiki publication.

## Authoritative implementation heads

- ESPressio-Radio: implementation evidence head `2a33d86c782ef25a25750ccd2b50b28191abd161`; complete Radio redesign workflow run `34719879383` — SUCCESS.
- ESPressio-ESP32: managed Raw80211/BLE provider head `b29b53bc438a0d5013075ce58a501424c689c0a0`; provider smoke run `34716648904` — SUCCESS.
- ESPressio-NRF24: managed provider head `a641199da7d4e5101e4d2c871715a9c870bdc531`; provider validation run `34716303385` — SUCCESS.

Documentation-only child commits may follow the executable evidence head; they do not weaken the executable gate.

## Completion mapping

### R7-01–R7-05 — managed provider and wire foundation

Complete. `IRadio` is a finite managed provider contract with explicit readiness, cost, completion evidence, bounded ingress and provider resources. RadioTransport v3 uses the frozen six-class service taxonomy and finite non-increasing remaining residence.

### R7-06–R7-10 — Radio-local Q1 capacity and reassembly

Complete. Radio owns fixed record+byte capacity planes with protected per-service domains, SharedOverflow and physically separate inbound UntrustedIngress quarantine. Reassembly is bounded, duplicate-aware, finite-lifetime and requires validated promotion from quarantine.

### R7-11–R7-15 — contention-domain scheduler/runtime

Complete. `RadioDomainScheduler` provides one R3 arbiter per contention domain using finite class queues, weighted DRR, bounded promotable-deadline debt, provider cost/readiness and exact deferred-completion correlation. `RadioDomainRuntime` owns one cooperative T1 execution context and one fixed wake/deadline path; no polling worker is required.

### R7-16–R7-18 — Clock K1/K2 transport and orchestration

Complete at the architecture layer. Clock direct frames use normal Clock Q1 plus the same R3 contention scheduler, never a privileged control queue. T1/T3 are late-prepared at the normal physical submission boundary. T2/T4 require capture-time provider evidence. The compact request is <=17 bytes and response is exactly 32 bytes, including distinct remote System and monotonic processing chronology. `RadioClockCoordinator` owns at most one client exchange and one pending reference response and derives scheduling from Timing's adaptive deadline.

### R7-19–R7-21 — predecessor eradication

Complete. Canonical source no longer contains the predecessor v2 `RadioTransport`, `RadioWorker`, `RadioControlWorker`, PrecisionThread control path, Radio Event bridge, Observable callback architecture, predecessor Clock synchronizer or related direct dependencies. The CI dependency guard enforces the final direct Radio dependency set:

```text
ESPressio-System
ESPressio-Task
ESPressio-Timing
ESPressio-Units
```

### R7-22–R7-24 — ESP32 providers

Complete for structural provider migration.

Raw80211 now uses fixed bounded ingress, managed runtime readiness/completion, ESP-IDF raw 802.11 TX completion and the shared Wi-Fi contention-domain/PHY ownership model. TX-done callback work is latched and drained through normal domain service so provider completion cannot race R3 correlation installation.

BLE is a managed broadcast-only legacy-advertising provider. Removing the redundant carried destination expands the opaque bearer payload to 26 bytes, which supports ordinary v3 framing. Deferred completion is bounded by the configured advertising dwell.

### R7-25 — NRF24 provider

Complete. NRF24 exposes finite ingress service, exact v3 logical maximum, conservative retry-aware cost, synchronous terminal completion and genuine peer ACK evidence for unicast. Broadcast correctly does not invent peer acknowledgement.

### R7-26 — deterministic resource accounting

Complete. `ESPressio_RadioResources.hpp` reports target-specific aggregate object footprints and explicit configured capacity/queue/worker/provider-resource facts without double-counting overlapping components.

### R7-27+ — validation/documentation closure

Complete for structural implementation. Host validation covers exact wire contracts, capacity isolation, resource accounting, duplicate/reassembly behavior, quarantine promotion, malformed/truncated/spoofed ingress, Clock correlation, saturated BestEffort versus promotable Clock arbitration, lifecycle/restart boundaries and family-opaque runtime handoff. ESP32 and NRF24 provider branches have target-specific executable evidence.

`README.md` and `CLOCK_SYNCHRONIZATION.md` now document only the managed architecture.

## Explicit non-claims

Tranche 7 completion does **not** claim that every concrete bearer is physically certified for sub-millisecond synchronization.

- Raw80211 presently exposes Estimated receive timing until a conservative on-target capture bound is established.
- BLE legacy advertising is not compatible with the exact 32-byte Clock response and advertises no Clock-qualified timestamp capability.
- NRF24 has a compatible 32-byte MTU but presently has no bounded/certified receive timestamp.

The architecture required for certified Clock synchronization is implemented. Provider physical characterization remains a deployment/provider qualification and cannot be manufactured by software documentation or host tests.

## Handoff to Tranche 8

The next structural tranche is Mesh. Mesh must consume the managed Radio Q1/R3/runtime boundary; it must not recreate physical fragment arbitration or Clock discipline. Mesh owns reference topology/trust selection while Radio owns the timestamp exchange and Timing owns estimator/clock discipline.

Standing all-tranche authorization permits immediate continuation into Tranche 8 after the cross-repository handoff is updated and live Mesh/MeshAdapters/Adapters/Radio tips are revalidated.