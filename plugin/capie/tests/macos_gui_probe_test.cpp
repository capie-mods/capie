/** @file macos_gui_probe_test.cpp @brief Forward-only telemetry against owned fake memory. */
#include "../src/core/macos_gui_probe.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
namespace p = capie::core::macos::gui_probe;
struct Memory {
    std::array<unsigned char, 0x400> bytes{};
    bool read(std::uintptr_t address, void* out, std::size_t n) const noexcept {
        if (address < 0x1000 || address - 0x1000 > bytes.size() || n > bytes.size() - (address - 0x1000))
            return false;
        std::memcpy(out, bytes.data() + address - 0x1000, n);
        return true;
    }
    void put(std::size_t offset, std::uintptr_t value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
};
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool v) {
        ++checks;
        if (!v)
            ++failures;
    };
    Memory memory;
    memory.put(0xe0, 0x1300);
    memory.put(0xf0, 0x1380);
    memory.put(0x120, 0x1000);
    memory.put(0x300, 0x5555);
    memory.put(0x380, 0x6666);
    p::Recorder<8> recorder;
    unsigned calls = 0;
    recorder.tick(memory, 0x1000, char{7}, {41, true}, [&](void* window, char force) {
        ++calls;
        check(window == reinterpret_cast<void*>(0x1000) && force == 7);
        memory.put(0xf0, 0x1390);
    });
    p::Event before{}, after{};
    check(recorder.read(0, before) && recorder.read(1, after));
    check(before.phase == p::Phase::enter && after.phase == p::Phase::leave && before.thread.id == 41);
    check(before.snapshot.complete && before.snapshot.context_matches && before.snapshot.root == 0x1380);
    check(after.snapshot.complete && after.snapshot.root == 0x1390 && calls == 1);
    recorder.dispatch(memory, 0x1110, reinterpret_cast<void*>(0x7777), {41, true}, [&](void* context, void* selector) {
        ++calls;
        check(context == reinterpret_cast<void*>(0x1110) && selector == reinterpret_cast<void*>(0x7777));
    });
    check(recorder.read(2, before) && before.kind == p::Kind::dispatch && before.snapshot.context_matches);
    recorder.tick(memory, 0, 0, {42, false}, [&](void*, char) { ++calls; });
    check(recorder.read(4, before) && !before.snapshot.complete && !before.thread.main);
    recorder.tick(memory, 0x1000, 0, {41, true}, [&](void*, char) { ++calls; });
    recorder.tick(memory, 0x1000, 0, {41, true}, [&](void*, char) { ++calls; });
    check(calls == 5 && recorder.dropped() == 2 && !recorder.read(8, after));
    p::Recorder<128> concurrent;
    auto writer = [&](unsigned long id) {
        for (unsigned i = 0; i < 16; ++i)
            concurrent.tick(memory, 0x1000, 0, {id, false}, [](void*, char) {});
    };
    std::thread a(writer, 1), b(writer, 2);
    a.join();
    b.join();
    unsigned ready = 0;
    for (unsigned i = 0; i < 128; ++i)
        if (concurrent.read(i, before))
            ++ready;
    check(ready == 64 && concurrent.dropped() == 0);
    p::Recorder<8> nested;
    nested.tick(memory, 0x1000, 0, {41, true},
                [&](void*, char) { nested.dispatch(memory, 0x1110, nullptr, {41, true}, [](void*, void*) {}); });
    p::Event nested_enter{}, nested_leave{};
    check(nested.read(0, before) && nested.read(1, nested_enter) && nested.read(2, nested_leave) &&
          nested.read(3, after) && before.call == after.call && nested_enter.call == nested_leave.call &&
          before.call != nested_enter.call);
    p::Recorder<4> throwing;
    bool propagated = false;
    try {
        throwing.tick(memory, 0x1000, 0, {41, true}, [](void*, char) { throw 17; });
    } catch (int value) {
        propagated = value == 17;
    }
    check(propagated && throwing.read(0, before) && !throwing.read(1, after));
    recorder.dispatch(memory, std::numeric_limits<std::uintptr_t>::max(), nullptr, {41, true},
                      [&](void*, void*) { ++calls; });
    check(calls == 6);
    std::printf("GUI probe: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
