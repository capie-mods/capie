// SKETCH (not in build.ps1): offline tests for src/features/host/host_probe.hpp.
// A fake address space stands in for hoi4.exe; every case builds the chain from scratch.
// Run by hand: cl /std:c++20 /EHsc /permissive- /W4 /WX tests\host_probe_test.cpp
#include "../src/features/host/host_probe.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using capie::core::uptr;
namespace L = capie::host::layout;

/// @brief Sparse fake memory: byte map; unmapped reads fail like an access violation.
struct FakeMemory {
    std::map<uptr, unsigned char> bytes;
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        auto* o = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = bytes.find(address + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    template <class T> void put(uptr address, T value) {
        unsigned char raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[address + i] = raw[i];
    }
    void put_string(uptr address, const std::string& text, uptr heap) {
        put<std::uint64_t>(address + 0x10, text.size());
        if (text.size() <= 15) {
            put<std::uint64_t>(address + 0x18, 15);
            for (std::size_t i = 0; i < 16; ++i) bytes[address + i] = i < text.size() ? text[i] : 0;
        } else {
            put<std::uint64_t>(address + 0x18, text.size());
            put<uptr>(address, heap);
            for (std::size_t i = 0; i < text.size(); ++i) bytes[heap + i] = static_cast<unsigned char>(text[i]);
        }
    }
};

constexpr uptr image = 0x140000000;
constexpr uptr k_app = 0x10000, k_session = 0x20000, k_backend = 0x30000, k_gamestate = 0x40000, k_humans = 0x50000;

struct Human { std::int32_t id; std::string name; };

FakeMemory world(std::int32_t mode, std::uint8_t is_server, uptr backend_vt, std::int32_t my_id,
                 const std::vector<Human>& list) {
    FakeMemory m;
    m.put<uptr>(image + L::app_global, k_app);
    m.put<uptr>(k_app, image + L::vt_game_application);
    m.put<uptr>(k_app + L::app_session, k_session);
    m.put<uptr>(k_session, image + L::vt_session);
    m.put<std::int32_t>(k_session + L::session_mode, mode);
    m.put<std::uint8_t>(k_session + L::session_is_server, is_server);
    m.put<uptr>(k_session + L::session_backend, k_backend);
    m.put<uptr>(k_backend, image + backend_vt);
    m.put<std::int32_t>(k_session + L::session_machine_id, my_id);
    m.put<uptr>(image + L::gamestate_global, k_gamestate);
    m.put<uptr>(k_gamestate + L::gamestate_humans, k_humans);
    m.put<std::int32_t>(k_gamestate + L::gamestate_human_count, static_cast<std::int32_t>(list.size()));
    for (std::size_t i = 0; i < list.size(); ++i) {
        const uptr h = k_humans + i * L::human_stride;
        m.put<uptr>(h, image + L::vt_human);
        m.put<std::int32_t>(h + L::human_machine_id, list[i].id);
        m.put_string(h + L::human_name, list[i].name, 0x90000 + i * 0x1000);
    }
    return m;
}

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}
capie::host::Verdict run(const FakeMemory& m) {
    return capie::host::decide(capie::host::read_snapshot(m, image));
}
using capie::host::Verdict;

}  // namespace

int main() {
    // SP: local backend, mode 2, is_server 1, id 1, one human.
    expect(run(world(2, 1, L::vt_dummy_server, 1, {{1, "Gracz"}})) == Verdict::host, "sp host");
    // MP host and client views of the same session.
    const std::vector<Human> mp{{1, "Alice"}, {2, "Bob z bardzo dluga nazwa"}};
    expect(run(world(1, 1, L::vt_network_server, 1, mp)) == Verdict::host, "mp host");
    expect(run(world(0, 0, L::vt_proxy_server, 2, mp)) == Verdict::not_host, "mp client");
    // Same display name on both machines must not make the client a host.
    expect(run(world(0, 0, L::vt_proxy_server, 2, {{1, "Ala"}, {2, "Ala"}})) == Verdict::not_host, "dup names");
    // Disagreements fail closed.
    expect(run(world(1, 0, L::vt_network_server, 1, mp)) == Verdict::unknown, "flag 0 on host backend");
    expect(run(world(0, 1, L::vt_proxy_server, 2, mp)) == Verdict::unknown, "flag 1 on client backend");
    expect(run(world(0, 1, L::vt_network_server, 2, mp)) == Verdict::unknown, "rehost (mode 0, network)");
    expect(run(world(1, 1, L::vt_network_server, 1, {{2, "Bob"}})) == Verdict::unknown, "host human missing");
    expect(run(world(1, 1, L::vt_network_server, 1, {{1, "A"}, {1, "B"}})) == Verdict::unknown, "duplicate id");
    expect(run(world(1, 1, L::vt_network_server, 1, {{1, ""}})) == Verdict::unknown, "empty name");
    expect(run(world(1, 1, L::vt_network_server, 1, {{1, std::string(200, 'x')}})) == Verdict::unknown, "long name");
    expect(run(world(1, 1, L::vt_session, 1, mp)) == Verdict::unknown, "foreign backend vtable");
    expect(run(world(7, 1, L::vt_network_server, 1, mp)) == Verdict::unknown, "bad mode");
    expect(run(world(1, 2, L::vt_network_server, 1, mp)) == Verdict::unknown, "bad flag byte");
    {
        auto m = world(1, 1, L::vt_network_server, 1, mp);
        m.put<uptr>(k_app, image + 0x1234);  // not CGameApplication
        expect(run(m) == Verdict::unknown, "app vtable");
    }
    {
        auto m = world(1, 1, L::vt_network_server, 1, mp);
        m.put<uptr>(k_app + L::app_session, 0);  // session being replaced
        expect(run(m) == Verdict::unknown, "null session");
    }
    {
        auto m = world(1, 1, L::vt_network_server, 1, mp);
        m.put<uptr>(k_humans + L::human_stride, image + 0x1);  // torn element
        expect(run(m) == Verdict::unknown, "torn human list");
    }
    {
        auto m = world(1, 1, L::vt_network_server, 1, mp);
        m.bytes.erase(k_humans + L::human_name + 0x10);  // unreadable name
        expect(run(m) == Verdict::unknown, "unreadable name");
    }
    std::printf("host_probe_test: %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
