#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <ESPressio_PrecisionThread.hpp>
#include <ESPressio_PrecisionThreadTraits.hpp>
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
/// ESPressio PrecisionThread worker responsible only for advancing inbound radio traffic into RadioTransport.
/// </summary>
/// <remarks>
/// The worker does not authenticate/decrypt messages and does not understand Command, Event, State, or any other
/// Foundation Type. RadioTransport reconstructs/routes opaque messages and hands locally addressed messages onward to
/// its configured receiver, which is expected to be the Security/authentication boundary.
///
/// Callback-driven providers queue their packet bytes in provider-owned bounded storage and call
/// IRadioWorkSignal::OnRadioWorkAvailable(). That only bumps this PrecisionThread. Provider queues and hardware are
/// processed here, so RadioTransport processing never runs inside a Wi-Fi/ESP-NOW driver callback.
///
/// PrecisionThread is intentional rather than EventThread: inbound radio availability is scheduling/work state, not an
/// ESPressio Foundation Event. The worker therefore remains completely unaware of Event payload types while still
/// gaining the common Threads lifecycle, observation, cadence and rate-limiting behaviour.
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
    /// Adds a radio to RadioTransport and makes this worker the sole inbound-service path for that interface.
    /// </summary>
    bool AddInterface(IRadio& radio, bool defaultRoute = false) noexcept {
        for (IRadio* existing : _radios) {
            if (existing == &radio) {
                if (!_transport.AddInterface(radio, defaultRoute)) return false;
                radio.SetReceiver(this);
                radio.SetWorkSignal(this);
                return true;
            }
        }

        for (IRadio*& slot : _radios) {
            if (slot != nullptr) continue;
            if (!_transport.AddInterface(radio, defaultRoute)) return false;
            slot = &radio;
            radio.SetReceiver(this);
            radio.SetWorkSignal(this);
            return true;
        }
        return false;
    }

    /// <summary>Returns a copy of the worker scheduling configuration.</summary>
    RadioWorkerConfiguration Configuration() const {
        std::lock_guard<std::mutex> lock(_configurationMutex);
        return _configuration;
    }

    /// <summary>Updates the PrecisionThread cadence/execution budget used for inbound servicing.</summary>
    void Configure(RadioWorkerConfiguration configuration) {
        {
            std::lock_guard<std::mutex> lock(_configurationMutex);
            _configuration = configuration;
        }
        ApplyRuntimeConfiguration(configuration);
        Bump();
    }

    /// <summary>
    /// Wakes the worker after a concrete provider has queued asynchronous inbound work.
    /// No packet parsing, routing, authentication, or observer dispatch occurs on the provider callback thread.
    /// </summary>
    void OnRadioWorkAvailable(IRadio&) noexcept override {
        Bump();
    }

    /// <summary>
    /// Receives one provider-serviced link packet on the RadioWorker thread and advances it into RadioTransport.
    /// </summary>
    void OnRadioPacket(IRadio& radio, const RadioPacketView& packet) override {
        // The onward-transport path gets priority. Supplemental observers run only after transport has consumed the view.
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
                radio->ProcessInbound();
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
    mutable std::mutex _configurationMutex;
    RadioWorkerConfiguration _configuration{};
};

} // namespace ESPressio::Radio
