#include <ESPressio_RadioDomainRuntime.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

using namespace ESPressio;
using namespace ESPressio::Radio;

class HostSignal final : public System::Synchronization::ISignal {
    std::mutex _mutex;
    std::condition_variable _condition;
    bool _set{false};
public:
    System::PlatformResult Give() noexcept override {
        { std::lock_guard<std::mutex> lock(_mutex); _set=true; }
        _condition.notify_all();
        return System::PlatformResult::Succeeded();
    }
    System::PlatformResult GiveFromInterrupt() noexcept override { return Give(); }
    System::PlatformResult Wait(std::uint32_t timeout=System::Synchronization::WaitForever) noexcept override {
        std::unique_lock<std::mutex> lock(_mutex);
        if(timeout==System::Synchronization::WaitForever){
            _condition.wait(lock,[this]{return _set;});
            return System::PlatformResult::Succeeded();
        }
        if(_condition.wait_for(lock,std::chrono::milliseconds(timeout),[this]{return _set;}))
            return System::PlatformResult::Succeeded();
        return System::PlatformResult::Failed(System::PlatformStatus::Timeout);
    }
    System::PlatformResult Reset() noexcept override {
        std::lock_guard<std::mutex> lock(_mutex);_set=false;return System::PlatformResult::Succeeded();
    }
};

class HostSynchronization final : public System::Synchronization::ISynchronizationProvider {
public:
    std::unique_ptr<System::Synchronization::ISignal> CreateBinarySignal(bool initiallySet=false) override {
        auto signal=std::make_unique<HostSignal>();
        if(initiallySet)(void)signal->Give();
        return signal;
    }
};

class HostExecution final : public System::Execution::IExecutionProvider {
    struct Control final { std::thread Thread; };
public:
    System::Execution::ExecutionCreationResult Create(
        System::Execution::ExecutionEntry,void*,const System::Execution::ExecutionConfiguration&) override {
        return {System::PlatformResult::Failed(System::PlatformStatus::Unsupported),System::Execution::InvalidExecutionHandle};
    }
    System::Execution::ExecutionCreationResult CreateJoinable(
        System::Execution::ExecutionEntry entry,void* context,const System::Execution::ExecutionConfiguration&) override {
        auto* control=new Control{std::thread([entry,context]{entry(context);})};
        return {System::PlatformResult::Succeeded(),reinterpret_cast<System::Execution::ExecutionHandle>(control)};
    }
    System::PlatformResult Join(System::Execution::ExecutionHandle handle) override {
        if(handle==System::Execution::InvalidExecutionHandle)return System::PlatformResult::Failed(System::PlatformStatus::InvalidArgument);
        auto* control=reinterpret_cast<Control*>(handle);
        if(control->Thread.joinable())control->Thread.join();
        delete control;
        return System::PlatformResult::Succeeded();
    }
    System::PlatformResult Destroy(System::Execution::ExecutionHandle) override {return System::PlatformResult::Failed(System::PlatformStatus::Unsupported);}
    System::PlatformResult Suspend(System::Execution::ExecutionHandle) override {return System::PlatformResult::Failed(System::PlatformStatus::Unsupported);}
    System::PlatformResult Resume(System::Execution::ExecutionHandle) override {return System::PlatformResult::Failed(System::PlatformStatus::Unsupported);}
    System::Execution::ExecutionHandle Current() const noexcept override{return 1;}
    std::uint32_t MinimumFreeStackBytes(System::Execution::ExecutionHandle) const noexcept override{return 0;}
    std::uint32_t ProcessorCount() const noexcept override{return 2;}
    void SleepMilliseconds(std::uint32_t ms) override{std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
    void Yield() override{std::this_thread::yield();}
    bool SupportsProcessorAffinity() const noexcept override{return true;}
};

class FakeScheduler final {
    RadioSchedulerWakeTarget _wake{};
public:
    std::atomic<unsigned> ServiceCount{0};
    std::atomic<unsigned> InboundCount{0};
    std::atomic<unsigned> ShutdownCount{0};
    RadioSchedulerStatus Initialize(IRadioTransferResultSink*,RadioSchedulerWakeTarget wake={}) noexcept{_wake=wake;return RadioSchedulerStatus::Success;}
    ManagedRadioIngressServiceResult ServiceOneInboundProvider(std::size_t=0) noexcept{++InboundCount;return {};}
    RadioSchedulerServiceResult Service(std::uint64_t) noexcept{++ServiceCount;return {RadioSchedulerStatus::Success,false,0};}
    RadioSchedulerStatus Shutdown() noexcept{++ShutdownCount;return RadioSchedulerStatus::Success;}
    void TriggerWake() noexcept{assert(_wake.Wake);_wake.Wake(_wake.Context);}
};

static void WaitFor(const std::atomic<unsigned>& value,unsigned expected){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(value.load(std::memory_order_acquire)<expected){
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}

int main(){
    HostSynchronization synchronization;
    System::Synchronization::SetProvider(&synchronization);
    HostExecution execution;
    FakeScheduler scheduler;
    RadioDomainRuntime<FakeScheduler> runtime(scheduler,execution);
    assert(runtime.Initialize(nullptr)==RadioDomainRuntimeStatus::Success);
    assert(runtime.Start()==RadioDomainRuntimeStatus::Success);
    WaitFor(scheduler.ServiceCount,1);
    const auto first=scheduler.ServiceCount.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(scheduler.ServiceCount.load()==first); // genuinely blocked: no fixed polling cadence
    scheduler.TriggerWake();
    WaitFor(scheduler.ServiceCount,first+1);
    assert(runtime.IsRunning());
    assert(runtime.Shutdown()==RadioDomainRuntimeStatus::Success);
    assert(!runtime.IsRunning());
    assert(scheduler.ShutdownCount==1);
    System::Synchronization::ResetProvider();
    return 0;
}
