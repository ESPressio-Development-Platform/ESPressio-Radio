# Radio clock synchronization

`ESPressio-Radio` transports one bounded four-capture Timing exchange over the same managed Radio capacity and contention scheduler as every other service class. Timing owns clock discipline, offset/delay estimation, filtering, drift learning, synchronization state and uncertainty. Radio owns only the physical exchange and the provider evidence needed to construct one valid Timing observation.

## Four-capture model

```text
Client                                      Reference

T1 request transmit  --------------------->
                              T2 request receive
                              T3 response transmit
                     <---------------------
T4 response receive
```

`RadioClockCoordinator` maintains at most one client exchange and at most one pending reference response. It does not create a worker, polling loop or fixed synchronization cadence.

The next client campaign comes from `Timing::IClockSynchronizationTarget::NextRequiredSynchronizationMonotonic()`. The coordinator is installed as the optional fixed service extension of the existing `RadioDomainRuntime`, so Clock shares that domain's single T1 execution context.

## Same Q1/R3 path

Clock has no privileged queue and never calls a provider around R3.

A Clock frame:

1. reserves ordinary protected Clock Q1 record+byte ownership;
2. enters the normal `RadioDomainScheduler` Clock queue;
3. participates in the same weighted DRR / deadline-promotion arbitration as all other classes;
4. uses the provider's normal cost/readiness contract;
5. resolves through the normal terminal-result path.

Direct Clock frames intentionally skip RadioTransport-v3 fragmentation because certified synchronization requires one physical frame. This is a wire mode inside the normal R3 scheduler, not a scheduler bypass.

## Late transmit capture

A sealed Clock-frame template remains owned while queued. When R3 selects it, the scheduler copies the template into its fixed physical scratch and invokes a bounded Clock prepare thunk immediately before provider cost/submission.

That late prepare point captures T1 or T3 close to the real physical submission boundary without making retained payload mutable and without introducing another Task.

## Receive evidence and historical System time

A valid precision candidate cannot derive T2/T4 by taking a later System-clock reading and subtracting elapsed monotonic time. The System clock may have changed between capture and service.

`RadioReceiveTimestampEvidence` therefore carries:

- the provider's capture monotonic coordinate;
- conservative capture uncertainty;
- timestamp continuity generation;
- capture source/quality;
- the Timing clock-model snapshot that was valid at capture time.

Only that capture-time model may map the historical monotonic coordinate into the System-clock domain. Finite uncertainty without a valid capture-time model is not certification-ready.

A provider that cannot establish a finite conservative capture bound may still carry ordinary Radio traffic; it simply cannot be presented as certified Clock evidence.

## Compact Clock wire

Clock magic is distinct from RadioTransport-v3 magic so `RadioIngressRouter` can classify the direct physical frame before ordinary v3 decode/reassembly.

The maximum request size is **17 bytes**. The response is exactly **32 bytes**.

The response preserves:

- exchange sequence/correlation;
- reference identity/lineage information required by the coordinator;
- T2 in the reference System-clock domain;
- exact T2→T3 System duration;
- exact T2→T3 monotonic duration;
- reference reliability/uncertainty metadata.

Carrying both durations is intentional. Timing receives genuine remote System and monotonic chronology; the coordinator does not invent a remote monotonic coordinate from the System delta.

## Ingress ownership

Each managed provider has one Radio-owned `IRadioReceiver`: `RadioIngressRouter`.

```text
provider finite RX storage
        -> ServiceInbound()
        -> RadioIngressRouter
             |-- compact Clock magic -> RadioClockCoordinator
             `-- ordinary v3         -> bounded RadioReassemblyTable
```

There is no `RadioControlWorker`, `IRadioPrioritizedIngress`, packet-observer precision path or competing Clock receiver.

## Correlation and lineage

The client accepts a response only when its sequence/reference lineage matches the one outstanding exchange. Delayed stale responses cannot become current observations.

Provider timestamp continuity is also part of the evidence lineage. A continuity-generation change clears source-specific estimator evidence before a new sample can qualify.

When a higher topology layer selects a different authenticated reference, that layer supplies the new reference relationship; Timing discipline remains in Timing and the physical exchange remains in Radio.

## Terminal send results

Clock request/response submissions use normal Radio transfer IDs and terminal results. The coordinator acts as a fixed forwarding sink: it consumes only the transfer IDs it owns and forwards every unrelated terminal result unchanged to the ordinary Radio owner.

A terminal Clock send failure/expiry clears only the matching exchange. It does not reset unrelated Radio work or create infinite retry.

## Precision qualification

Sub-millisecond synchronization is a deployment qualification, not a property inferred from MTU size or API names. Qualification must include:

- finite conservative provider RX capture uncertainty;
- valid capture-time Timing model evidence;
- provider TX completion semantics that are not confused with API admission;
- contention cost good enough for any enabled promotable-deadline claim;
- Clock deadlines meeting budget under saturated mixed-service traffic;
- continuity/reference failover behavior;
- on-target worst-case characterization under realistic coexistence/load.

The Radio host suite proves the architecture: Clock uses normal Q1/R3, is promoted under saturated BestEffort load, preserves one-exchange correlation, and feeds Timing complete chronology. It does **not** substitute host tests for physical-provider certification.

### Current providers

**ESP32 Raw80211**

The managed provider uses finite ingress storage and ESP-IDF's raw 802.11 TX-done callback for deferred terminal transmission completion. Its current receive timestamp quality remains `Estimated`; a conservative worst-case capture bound has not yet been certified, so it must not be described as a completed sub-millisecond precision bearer.

**ESP32 BLE legacy advertising**

The provider is broadcast-only and exposes 26 opaque physical bytes after removing the redundant carried destination. That is sufficient for ordinary v3 framing but not the exact 32-byte Clock response. It does not advertise Clock-qualified receive timing.

**nRF24**

The provider has a 32-byte physical MTU, synchronous terminal completion and real unicast link ACK evidence. Its current receive timestamp evidence is unbounded/not certified, so MTU compatibility alone does not make it a precision Clock bearer.

## Responsibility boundary

```text
reference selection / trust / topology    -> Mesh or other authenticated topology owner
physical Clock request/response transport -> ESPressio-Radio
T1/T2/T3/T4 validation and discipline     -> ESPressio-Timing
provider timestamp/completion facts       -> concrete IRadio implementation
```

No layer may strengthen the evidence supplied by the layer below it.