#include <cassert>
#include <cstddef>
#include <cstdint>

#include <ESPressio_RadioPeerRegistry.hpp>

using namespace ESPressio::Radio;

class FakeRadio final : public IRadio {
public:
    explicit FakeRadio(std::uint8_t local) : _local(RadioAddress::FromBytes(&local, 1)) {}
    bool Start() override { _started = true; return true; }
    void Stop() noexcept override { _started = false; }
    bool IsStarted() const noexcept override { return _started; }
    RadioCapabilities Capabilities() const noexcept override { return {}; }
    RadioAddress LocalAddress() const noexcept override { return _local; }
    RadioSendResult Send(const RadioAddress&, const std::uint8_t*, std::size_t) override {
        return RadioSendResult::Accepted();
    }
    void SetReceiver(IRadioReceiver*) noexcept override {}
    void SetWorkSignal(IRadioWorkSignal*) noexcept override {}
    void DrainInbound() override {}
    RadioObserverSubscriptions& Observers() noexcept override { return _observers; }
private:
    RadioAddress _local{};
    bool _started{false};
    RadioObserverSubscriptions _observers{};
};

static RadioAddress Address(std::uint8_t value) {
    return RadioAddress::FromBytes(&value, 1);
}

int main() {
    RadioPeerRegistry<2> peers;
    FakeRadio radioA{0xA0};
    FakeRadio radioB{0xB0};

    RadioPeerHandle first{};
    assert(peers.Observe(radioA, Address(1), first) == RadioPeerObserveResult::Observed);
    assert(first);
    assert(peers.Size() == 1);
    assert(peers.Resolve(first) != nullptr);
    assert(peers.Resolve(first)->Interface == &radioA);
    assert(peers.Resolve(first)->Address == Address(1));

    RadioPeerHandle refreshed{};
    assert(peers.Observe(radioA, Address(1), refreshed) == RadioPeerObserveResult::Refreshed);
    assert(refreshed == first);
    assert(peers.Size() == 1);

    RadioPeerHandle second{};
    assert(peers.Observe(radioB, Address(2), second) == RadioPeerObserveResult::Observed);
    assert(peers.Size() == 2);

    // Enumeration is bounded, non-allocating and exposes the exact generation-safe handle + binding pairs.
    std::size_t enumerated = 0;
    bool sawFirst = false;
    bool sawSecond = false;
    peers.ForEach([&](RadioPeerHandle handle, const RadioPeerBinding& binding) {
        ++enumerated;
        if (handle == first) {
            sawFirst = true;
            assert(binding.Interface == &radioA);
            assert(binding.Address == Address(1));
        }
        if (handle == second) {
            sawSecond = true;
            assert(binding.Interface == &radioB);
            assert(binding.Address == Address(2));
        }
    });
    assert(enumerated == 2);
    assert(sawFirst && sawSecond);

    RadioPeerHandle full{};
    assert(peers.Observe(radioA, Address(3), full) == RadioPeerObserveResult::ResourceUnavailable);
    assert(!full);

    assert(peers.Invalidate(first));
    assert(peers.Resolve(first) == nullptr);

    RadioPeerHandle replacement{};
    assert(peers.Observe(radioA, Address(3), replacement) == RadioPeerObserveResult::Observed);
    assert(replacement.Slot == first.Slot);
    assert(replacement.Generation != first.Generation);
    assert(peers.Resolve(first) == nullptr);
    assert(peers.Resolve(replacement) != nullptr);

    assert(peers.InvalidateInterface(radioB) == 1);
    assert(peers.Resolve(second) == nullptr);
    assert(peers.Size() == 1);

    enumerated = 0;
    peers.ForEach([&](RadioPeerHandle handle, const RadioPeerBinding& binding) {
        ++enumerated;
        assert(handle == replacement);
        assert(binding.Interface == &radioA);
        assert(binding.Address == Address(3));
    });
    assert(enumerated == 1);

    RadioPeerHandle broadcast{};
    assert(peers.Observe(radioA, RadioAddress::Broadcast(1), broadcast) == RadioPeerObserveResult::Invalid);
    assert(!broadcast);

    peers.Clear();
    assert(peers.Empty());
    assert(peers.Resolve(replacement) == nullptr);
    return 0;
}
