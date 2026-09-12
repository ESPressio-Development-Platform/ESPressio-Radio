#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>

#include <ESPressio_Synchronization.hpp>
#include <ESPressio_SystemPlatformClock.hpp>
#include <ESPressio_TaskRuntime.hpp>

#include "ESPressio_RadioScheduler.hpp"

namespace ESPressio::Radio {

enum class RadioDomainRuntimeStatus : std::uint8_t {
    Success = 0,
    AlreadyInitialized,
    NotInitialized,
    AlreadyRunning,
    SynchronizationUnavailable,
    SchedulerInitializationFailed,
    ExecutionUnavailable,
    JoinFailed
};

/// <summary>
/// Owns exactly one cooperative T1 execution context and one fixed wake signal for one R3 contention domain.
/// </summary>
/// <remarks>
/// The scheduler owns all queues and retained work; this runtime owns no work queue. Every loop services at most one
/// bounded inbound-provider quantum and one bounded scheduler quantum. It blocks on the fixed infrastructure signal when
/// idle. The exact monotonic earliest deadline remains scheduler-visible for R7-17 precision wake integration; the
/// millisecond System signal timeout is only a conservative fallback and is never presented as sub-millisecond timing.
/// Controlled shutdown sets Stop, wakes the task, lets the entry return cooperatively, then Join releases execution
/// resources. No executing quantum is force-killed.
/// </remarks>
template<class TScheduler>
class RadioDomainRuntime final {
    TScheduler* _scheduler{nullptr};
    System::Execution::IExecutionProvider* _executionProvider{nullptr};
    std::unique_ptr<System::Synchronization::ISignal> _wakeSignal{};
    Task::TaskHandle _task{System::Execution::InvalidExecutionHandle};
    std::atomic<bool> _initialized{false};
    std::atomic<bool> _running{false};
    std::atomic<bool> _stop{false};
    std::atomic<std::uint64_t> _publishedEarliestDeadline{0};

    static void WakeThunk(void* context) noexcept {
        static_cast<RadioDomainRuntime*>(context)->Wake();
    }

    static void EntryThunk(void* context) {
        static_cast<RadioDomainRuntime*>(context)->Run();
    }

    static std::uint32_t FallbackTimeoutMilliseconds(
        std::uint64_t now,
        std::uint64_t deadline) noexcept {
        if (deadline == 0) return System::Synchronization::WaitForever;
        if (deadline <= now) return 0;
        const auto delta = deadline - now;
        const auto milliseconds = (delta + 999'999ULL) / 1'000'000ULL;
        if (milliseconds >= System::Synchronization::WaitForever)
            return System::Synchronization::WaitForever - 1U;
        return static_cast<std::uint32_t>(milliseconds == 0 ? 1 : milliseconds);
    }

    void Run() noexcept {
        while (!_stop.load(std::memory_order_acquire)) {
            // Reset before observing scheduler/provider state. A concurrent Give after this point is retained by the
            // binary signal; a Give before reset corresponds to state which is re-read immediately below.
            (void)_wakeSignal->Reset();
            if (_stop.load(std::memory_order_acquire)) break;

            const auto inbound = _scheduler->ServiceOneInboundProvider();
            const auto now = System::Clock::Monotonic().NowNanoseconds();
            const auto serviced = _scheduler->Service(now);
            _publishedEarliestDeadline.store(serviced.EarliestDeadlineNanoseconds, std::memory_order_release);

            if (_stop.load(std::memory_order_acquire)) break;
            if (inbound.WorkRemaining || serviced.ImmediateWorkRemaining) continue;

            const auto beforeWait = System::Clock::Monotonic().NowNanoseconds();
            const auto timeout = FallbackTimeoutMilliseconds(beforeWait, serviced.EarliestDeadlineNanoseconds);
            if (timeout == 0) continue;
            (void)_wakeSignal->Wait(timeout);
        }
        _running.store(false, std::memory_order_release);
    }

public:
    explicit RadioDomainRuntime(
        TScheduler& scheduler,
        System::Execution::IExecutionProvider& executionProvider = System::Execution::Provider()) noexcept
        : _scheduler(&scheduler), _executionProvider(&executionProvider) {}

    RadioDomainRuntime(const RadioDomainRuntime&) = delete;
    RadioDomainRuntime& operator=(const RadioDomainRuntime&) = delete;

    ~RadioDomainRuntime() {
        if (_initialized.load(std::memory_order_acquire)) (void)Shutdown();
    }

    RadioDomainRuntimeStatus Initialize(
        IRadioTransferResultSink* resultSink,
        const Task::TaskExecutionConfiguration& taskConfiguration = {}) {
        if (_initialized.load(std::memory_order_acquire)) return RadioDomainRuntimeStatus::AlreadyInitialized;
        auto* synchronization = System::Synchronization::Provider();
        if (synchronization == nullptr) return RadioDomainRuntimeStatus::SynchronizationUnavailable;
        _wakeSignal = System::Synchronization::CreateBinarySignal(false);
        if (!_wakeSignal) return RadioDomainRuntimeStatus::SynchronizationUnavailable;
        // Resolve all provider-backed synchronization while allocation is still permitted.
        if (!_wakeSignal->Reset()) {
            _wakeSignal.reset();
            return RadioDomainRuntimeStatus::SynchronizationUnavailable;
        }
        const auto schedulerStatus = _scheduler->Initialize(resultSink, {this, &WakeThunk});
        if (schedulerStatus != RadioSchedulerStatus::Success) {
            _wakeSignal.reset();
            return RadioDomainRuntimeStatus::SchedulerInitializationFailed;
        }
        _taskConfiguration = taskConfiguration;
        _initialized.store(true, std::memory_order_release);
        return RadioDomainRuntimeStatus::Success;
    }

    RadioDomainRuntimeStatus Start() {
        if (!_initialized.load(std::memory_order_acquire)) return RadioDomainRuntimeStatus::NotInitialized;
        if (_running.exchange(true, std::memory_order_acq_rel)) return RadioDomainRuntimeStatus::AlreadyRunning;
        _stop.store(false, std::memory_order_release);
        const auto created = Task::TaskRuntime::CreateJoinable(
            &EntryThunk, this, _taskConfiguration, *_executionProvider);
        if (!created) {
            _running.store(false, std::memory_order_release);
            return RadioDomainRuntimeStatus::ExecutionUnavailable;
        }
        _task = created.Handle;
        Wake();
        return RadioDomainRuntimeStatus::Success;
    }

    void Wake() noexcept {
        if (_wakeSignal) (void)_wakeSignal->Give();
    }

    /// <summary>Exact scheduler deadline which a precision Timing/platform wake source may arm independently.</summary>
    std::uint64_t EarliestDeadlineNanoseconds() const noexcept {
        return _publishedEarliestDeadline.load(std::memory_order_acquire);
    }

    bool IsRunning() const noexcept { return _running.load(std::memory_order_acquire); }

    RadioDomainRuntimeStatus Shutdown() noexcept {
        if (!_initialized.load(std::memory_order_acquire)) return RadioDomainRuntimeStatus::Success;
        _stop.store(true, std::memory_order_release);
        Wake();
        if (_task != System::Execution::InvalidExecutionHandle) {
            const auto joined = _executionProvider->Join(_task);
            if (!joined) return RadioDomainRuntimeStatus::JoinFailed;
            _task = System::Execution::InvalidExecutionHandle;
        }
        (void)_scheduler->Shutdown();
        _wakeSignal.reset();
        _initialized.store(false, std::memory_order_release);
        _running.store(false, std::memory_order_release);
        return RadioDomainRuntimeStatus::Success;
    }

private:
    Task::TaskExecutionConfiguration _taskConfiguration{};
};

} // namespace ESPressio::Radio
