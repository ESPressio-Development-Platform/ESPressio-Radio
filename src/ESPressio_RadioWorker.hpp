#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_Time.hpp>

#include "ESPressio_RadioTransport.hpp"

namespace ESPressio::Radio {

/// <summary>Runtime scheduling configuration for the radio inbound worker.</summary>
struct RadioWorkerConfiguration {
    /// <summary>
    /// Maximum idle interval between inbound-service passes. This allows providers without an asynchronous RX signal,
    /// such as an nRF24 implementation without an IRQ integration, to be serviced without application polling.
    /// </summary>
    uint32_t IterationPeriodMilliseconds = 10;

    /// <summary>Desired execution budget for one inbound-service pass.</summary>
    uint32_t DesiredExecutionBudgetMilliseconds = 2;
};

/// <summary>
/// ESPressio PrecisionThread worker responsible only for draining physical Radio ingress and advancing Radio-owned
/// direct-link logical reassembly.
/// </summary>
/// <remarks>
/// The worker never authenticates messages, resolves Mesh routes or understands Command, Event, State or another
/// conceptual primitive family. RadioTransport reconstructs opaque direct-link transfers and hands complete bytes to
/// its configured receiver. Higher layers own all onward-routing and distributed semantics.
///
/// Callback-driven providers queue packet bytes in provider-owned bounded storage and call
/// IRadioWorkSignal::OnRadioWorkAvailable(). The current PrecisionThread Working Branch exposes Bump() for this purpose:
/// it advances the next iteration to the current clock time and signals the scheduler. Provider queues and hardware are
/// drained by Iterate(), so RadioTransport processing never runs inside a Wi-Fi/ESP-NOW driver callback.
///
/// PrecisionThread is intentional rather than EventThread: inbound radio availability is scheduling/work state, not an
/// ESPressio conceptual Event.
/// </remarks>
class RadioWorker final
    : public Threads::PrecisionThread<
          Units::NanoSeconds<uint64_t>,
          Threads::PrecisionThreadTraits<Units::NanoSeconds<uint64_t>>
      >,
      public IRadioReceiver,
      public IRadioWorkSignal {
public:
    using Time = Units::NanoSeconds<uint64_t>;
    using Base = Threads::PrecisionThread<Time, Threads::PrecisionThreadTraits<Time>>;

    explicit RadioWorker(
        RadioTransport& transport,
        RadioWorkerConfiguration configuration = {}
    ) : _transport(transport), _configuration(configuration) {
        SetStartOnInitialize(false);
        ApplyRuntimeConfiguration(configuration);
    }

    ~RadioWorker() override {
        Shutdown();
        for (IRadio* radio : _radios) {
            if (radio == nullptr) continue;
            radio->SetWorkSignal(nullptr);
            radio->SetReceiver(nullptr);
        }
    }

    RadioWorker(const RadioWorker&) = delete;
    RadioWorker& operator=(const RadioWorker&) = delete;
    RadioWorker(RadioWorker&&) = delete;
    RadioWorker& operator=(RadioWorker&&) = delete;

    /// <summary>
    /// Registers a Radio with RadioTransport and makes this worker the sole inbound-service path for that interface.
    /// </summary>
    bool AddInterface(IRadio& radio) noexcept {
        for (IRadio* existing : _radios) {
            if (existing == &radio) {
                if (!_transport.AddInterface(radio)) return false;
                radio.SetReceiver(this);
                radio.SetWorkSignal(this);
                return true;
            }
        }

        for (IRadio*& slot : _radios) {
            if (slot != nullptr) continue;
            if (!_transport.AddInterface(radio)) return false;
            slot = &radio;
            radio.SetReceiver(this);
            radio.SetWorkSignal(this);
            return true;
        }
        return false;
    }

    /// <summary>Returns a copy of the worker scheduling configuration.</summary>
    RadioWorkerConfiguration Configuration() const {
        std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
        return _configuration;
    }

    /// <summary>Updates the PrecisionThread cadence/execution budget used for inbound servicing.</summary>
    void Configure(RadioWorkerConfiguration configuration) {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_configurationMutex);
            _configuration = configuration;
        }
        ApplyRuntimeConfiguration(configuration);
        Bump();
    }

    /// <summary>
    /// Requests an immediate worker iteration after a concrete provider has queued asynchronous inbound work.
    /// No packet parsing, routing, authentication or observer dispatch occurs on the provider callback thread.
    /// </summary>
    void OnRadioWorkAvailable(IRadio&) noexcept override {
        try {
            Bump();
        } catch (...) {
            // A provider work signal must never let scheduler failures escape into a driver callback context.
        }
    }

    /// <summary>
    /// Receives one provider-drained physical packet on the RadioWorker thread and advances Radio-level reassembly.
    /// </summary>
    void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) override {
        _transport.ProcessInboundPacket(radio, packet);
        radio.Observers().NotifyPacketReceived(radio, packet);
    }

protected:
    /// <summary>Services all attached radio interfaces for currently available inbound packets.</summary>
    void Iterate(
        Time,
        Time,
        Threads::SkippedIterationCount
    ) override {
        for (IRadio* radio : _radios) {
            if (radio != nullptr && radio->IsStarted()) {
                radio->DrainInbound();
            }
        }
    }

private:
    void ApplyRuntimeConfiguration(const RadioWorkerConfiguration& configuration) {
        SetIterationPeriod(
            Units::MilliSeconds<uint32_t>(configuration.IterationPeriodMilliseconds)
        );
        SetDesiredIterationPeriod(
            Units::MilliSeconds<uint32_t>(configuration.DesiredExecutionBudgetMilliseconds)
        );
    }

    RadioTransport& _transport;
    std::array<IRadio*, ESPRESSIO_RADIO_MAX_INTERFACES> _radios{};
    mutable System::Synchronization::Mutex _configurationMutex;
    RadioWorkerConfiguration _configuration{};
};

} // namespace ESPressio::Radio
