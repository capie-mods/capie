/**
 * @file host_probe_test.cpp
 * @brief Offline tests of `host_probe.hpp`: chain reading, decision and the debug log line.
 *
 * A sparse fake address space stands in for hoi4.exe; every case builds the chain from scratch.
 * The log line must never contain a player name.
 */
#include "../src/features/host/host_probe.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using capie::core::uptr;
namespace host = capie::host;
namespace layout = capie::host::layout;
using host::Verdict;

int checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
int failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

// Fake addresses and engine values are literal test data.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// @brief Sparse fake memory: byte map; unmapped reads fail like an access violation.
struct FakeMemory {
    std::map<uptr, unsigned char> bytes;  ///< Mapped bytes.

    /**
     * @brief Reads bytes.
     * @param address Address.
     * @param out Destination.
     * @param size Byte count.
     * @return `false` when any byte is unmapped.
     */
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        auto* target = static_cast<unsigned char*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto found = bytes.find(address + i);
            if (found == bytes.end()) {
                return false;
            }
            target[i] = found->second;
        }
        return true;
    }

    /**
     * @brief Writes a value.
     * @tparam T Value type.
     * @param address Address.
     * @param value Value.
     */
    template <class T>
    void put(uptr address, T value) {
        std::array<unsigned char, sizeof(T)> raw{};
        std::memcpy(raw.data(), &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes[address + i] = raw[i];
        }
    }

    /**
     * @brief Writes an MSVC std::string (inline up to 15 bytes, else on the heap).
     * @param address String object.
     * @param text Text.
     * @param heap Heap buffer for long text.
     */
    void put_string(uptr address, const std::string& text, uptr heap) {
        put<std::uint64_t>(address + 0x10, text.size());
        if (text.size() <= 15) {
            put<std::uint64_t>(address + 0x18, 15);
            for (std::size_t i = 0; i < 16; ++i) {
                bytes[address + i] = i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
            }
        } else {
            put<std::uint64_t>(address + 0x18, text.size());
            put<uptr>(address, heap);
            for (std::size_t i = 0; i < text.size(); ++i) {
                bytes[heap + i] = static_cast<unsigned char>(text[i]);
            }
        }
    }
};

constexpr uptr image = 0x140000000;
constexpr uptr fake_app = 0x10000;
constexpr uptr fake_session = 0x20000;
constexpr uptr fake_backend = 0x30000;
constexpr uptr fake_gamestate = 0x40000;
constexpr uptr fake_humans = 0x50000;

/// @brief One player of the fake list.
struct Human {
    std::int32_t id;   ///< Machine id.
    std::string name;  ///< Display name.
};

/**
 * @brief Builds a whole fake chain.
 * @param mode Session mode.
 * @param is_server Server flag byte.
 * @param backend_vt Backend vtable RVA.
 * @param my_id Own machine id.
 * @param list Players.
 * @return Memory.
 */
FakeMemory world(std::int32_t mode, std::uint8_t is_server, uptr backend_vt, std::int32_t my_id,
                 const std::vector<Human>& list) {
    FakeMemory m;
    m.put<uptr>(image + layout::app_global, fake_app);
    m.put<uptr>(fake_app, image + layout::vt_game_application);
    m.put<uptr>(fake_app + layout::app_session, fake_session);
    m.put<uptr>(fake_session, image + layout::vt_session);
    m.put<std::int32_t>(fake_session + layout::session_mode, mode);
    m.put<std::uint8_t>(fake_session + layout::session_is_server, is_server);
    m.put<uptr>(fake_session + layout::session_backend, fake_backend);
    m.put<uptr>(fake_backend, image + backend_vt);
    m.put<std::int32_t>(fake_session + layout::session_machine_id, my_id);
    m.put<uptr>(image + layout::gamestate_global, fake_gamestate);
    m.put<uptr>(fake_gamestate + layout::gamestate_humans, fake_humans);
    m.put<std::int32_t>(fake_gamestate + layout::gamestate_human_count, static_cast<std::int32_t>(list.size()));
    for (std::size_t i = 0; i < list.size(); ++i) {
        const uptr h = fake_humans + (i * layout::human_stride);
        m.put<uptr>(h, image + layout::vt_human);
        m.put<std::int32_t>(h + layout::human_machine_id, list[i].id);
        m.put_string(h + layout::human_name, list[i].name, 0x90000 + (i * 0x1000));
    }
    return m;
}

/**
 * @brief Reads and decides.
 * @param m Memory.
 * @return Verdict.
 */
Verdict run(const FakeMemory& m) {
    return host::decide(host::read_snapshot(m, image));
}

/**
 * @brief Log line of a world.
 * @param m Memory.
 * @return Line text.
 */
std::string line_of(const FakeMemory& m) {
    std::array<char, host::line_capacity> line{};
    const auto size = host::probe_line(m, image, line);
    return {line.data(), size};
}

/**
 * @brief Whether a line contains a fragment.
 * @param line Line.
 * @param part Fragment.
 * @return `true` when found.
 */
bool has(const std::string& line, std::string_view part) {
    return line.find(part) != std::string::npos;
}

/**
 * @brief Players of an MP session (host Alice, client Bob).
 * @return List.
 */
std::vector<Human> mp_list() {
    return {{1, "Alice"}, {2, "Bob z bardzo dluga nazwa"}};
}

void decision_tests() {
    const auto mp = mp_list();
    check(run(world(2, 1, layout::vt_dummy_server, 1, {{1, "Gracz"}})) == Verdict::host, "sp host");
    check(run(world(1, 1, layout::vt_network_server, 1, mp)) == Verdict::host, "mp host");
    check(run(world(0, 0, layout::vt_proxy_server, 2, mp)) == Verdict::not_host, "mp client");
    // Same display name on both machines must not make the client a host.
    check(run(world(0, 0, layout::vt_proxy_server, 2, {{1, "Ala"}, {2, "Ala"}})) == Verdict::not_host, "dup names");
    // Disagreements fail closed.
    check(run(world(1, 0, layout::vt_network_server, 1, mp)) == Verdict::unknown, "flag 0 on host backend");
    check(run(world(2, 0, layout::vt_dummy_server, 1, {{1, "Gracz"}})) == Verdict::unknown, "flag 0 on local");
    check(run(world(0, 1, layout::vt_proxy_server, 2, mp)) == Verdict::unknown, "flag 1 on client backend");
    check(run(world(0, 1, layout::vt_network_server, 2, mp)) == Verdict::unknown, "rehost (mode 0, network)");
    check(run(world(1, 1, layout::vt_network_server, 2, mp)) == Verdict::unknown, "host backend with machine 2");
    check(run(world(0, 0, layout::vt_proxy_server, 1, mp)) == Verdict::unknown, "client with machine 1");
    check(run(world(1, 1, layout::vt_network_server, 1, {{2, "Bob"}})) == Verdict::unknown, "host human missing");
    check(run(world(0, 0, layout::vt_proxy_server, 2, {{2, "Bob"}})) == Verdict::unknown, "client: host missing");
    check(run(world(1, 1, layout::vt_network_server, 1, {{1, "A"}, {1, "B"}})) == Verdict::unknown, "duplicate id");
    check(run(world(0, 0, layout::vt_proxy_server, 2, {{1, "A"}, {2, "B"}, {2, "C"}})) == Verdict::unknown,
          "client: own id twice");
    check(run(world(0, 0, layout::vt_proxy_server, 2, {{1, "A"}, {1, "B"}, {2, "C"}})) == Verdict::unknown,
          "client: host id twice");
    check(run(world(1, 1, layout::vt_network_server, 1, {{1, ""}})) == Verdict::unknown, "empty name");
    check(run(world(1, 1, layout::vt_network_server, 1, {{1, std::string(200, 'x')}})) == Verdict::unknown,
          "long name");
    check(run(world(1, 1, layout::vt_network_server, 1, {{1, std::string(40, 'x')}})) == Verdict::host,
          "heap name (40 bytes)");
    check(run(world(1, 1, layout::vt_session, 1, mp)) == Verdict::unknown, "foreign backend vtable");
    check(run(world(2, 1, layout::vt_network_server, 1, mp)) == Verdict::unknown, "network backend with mode 2");
    check(run(world(1, 1, layout::vt_dummy_server, 1, mp)) == Verdict::unknown, "dummy backend with mode 1");
    check(run(world(7, 1, layout::vt_network_server, 1, mp)) == Verdict::unknown, "bad mode");
    check(run(world(1, 2, layout::vt_network_server, 1, mp)) == Verdict::unknown, "bad flag byte");
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.put<uptr>(fake_app, image + 0x1234);  // not CGameApplication
        check(run(m) == Verdict::unknown, "app vtable");
    }
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.put<uptr>(fake_session, image + 0x1234);  // not CSession
        check(run(m) == Verdict::unknown, "session vtable");
    }
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.put<uptr>(fake_app + layout::app_session, 0);  // session being replaced
        check(run(m) == Verdict::unknown, "null session");
    }
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.put<uptr>(fake_humans + layout::human_stride, image + 0x1);  // torn element
        check(run(m) == Verdict::unknown, "torn human list");
    }
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.bytes.erase(fake_humans + layout::human_name + 0x10);  // unreadable name
        check(run(m) == Verdict::unknown, "unreadable name");
    }
    {
        auto m = world(1, 1, layout::vt_network_server, 1, mp);
        m.put<std::int32_t>(fake_gamestate + layout::gamestate_human_count, 65);
        check(run(m) == Verdict::unknown, "human count above the bound");
    }
}

void line_tests() {
    const auto mp = mp_list();
    const auto host_line = line_of(world(1, 1, layout::vt_network_server, 1, mp));
    check(host_line.starts_with("CAPIE_HOST probe would_be_host=yes mode=1 server=1 backend=Network machine=1 "
                                "humans=2 own_entry=found own_is_host=yes my_len=5 host_len=5 names=equal "
                                "record=none\r\n"),
          "host line: " + host_line);
    const auto client_line = line_of(world(0, 0, layout::vt_proxy_server, 2, mp));
    check(has(client_line, "would_be_host=no ") && has(client_line, "backend=Proxy ") &&
              has(client_line, "machine=2 ") && has(client_line, "own_is_host=no ") &&
              has(client_line, "my_len=24 host_len=5 names=different "),
          "client line: " + client_line);
    check(!has(host_line, "Alice") && !has(client_line, "Alice") && !has(client_line, "Bob"), "no names in the log");
    const auto sp_line = line_of(world(2, 1, layout::vt_dummy_server, 1, {{1, "Gracz"}}));
    check(has(sp_line, "would_be_host=yes mode=2 server=1 backend=Dummy "), "sp line: " + sp_line);
    const auto rehost = line_of(world(0, 1, layout::vt_network_server, 2, mp));
    check(has(rehost, "would_be_host=unreadable mode=0 server=1 backend=Network "), "rehost line: " + rehost);
    const auto missing = line_of(world(1, 1, layout::vt_network_server, 1, {{2, "Bob"}}));
    check(has(missing, "own_entry=missing own_is_host=unknown my_len=0 host_len=0 names=unreadable "),
          "missing own entry: " + missing);
    auto other = world(1, 1, layout::vt_session, 1, mp);
    check(has(line_of(other), "backend=other "), "foreign backend named other");
    other.put<uptr>(fake_app, 0);
    const auto broken = line_of(other);
    check(has(broken, "would_be_host=unreadable mode=-1 server=255 backend=- machine=-1 humans=-1 own_entry=missing"),
          "unreadable chain: " + broken);
    auto torn = world(1, 1, layout::vt_network_server, 1, mp);
    torn.put<uptr>(fake_humans + layout::human_stride, image + 0x1);
    const auto torn_line = line_of(torn);
    check(has(torn_line, "own_entry=missing own_is_host=unknown "), "torn list voids both entries: " + torn_line);
    const auto dup = line_of(world(0, 0, layout::vt_proxy_server, 2, {{1, "Ala"}, {2, "Ala"}}));
    check(has(dup, "would_be_host=no ") && has(dup, "own_is_host=no ") && has(dup, "names=equal "),
          "same name on another element: " + dup);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main() {
    try {
        decision_tests();
        line_tests();
    } catch (...) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception\n"));
    }
    static_cast<void>(std::printf("host probe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
