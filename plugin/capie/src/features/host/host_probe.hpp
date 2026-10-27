/**
 * @file host_probe.hpp
 * @brief Read-only host detection probe (debug flavor of `capie_editor.dll`: log only, no record).
 *
 * Design and static evidence: monorepo archive: docs/evidence/session-20261014/host-detect/report.txt. Stage 1 of
 * the rollout (report section 5, step 0): the debug flavor logs one `CAPIE_HOST` line per grammar
 * Apply; no `capie_host` record is sent and the release flavor does not contain the probe. The
 * record waits for the two-client test (report sections 5 and 7).
 *
 * Decision (docs/API-DECISIONS.md, 2026-10-14): CAPIE reads `get_my_username` and
 * `get_host_username`; is_host = both equal. Names are compared as the SAME element of the human
 * list (machine id), so two players with the same display name never make a client the host.
 *
 * Engine chain (hoi4.exe 1.19.4.0; 1.19.3.0 values in docs/research/port-1.19.4.0.md):
 *   app      = [image + 0x33DDB08]                 getter 19A5F70; GetLocalHuman 1F1A49 loads it and calls vslot 0x88;
 *              vtable CGameApplication 2796308
 *   session  = [app + 0x380]                       CGameApplication::GetSession 17F880 (vslot 0x88)
 *              vtable CSession 2BB4548
 *   mode     = dword [session + 0x34]              CSession::Init 22C3760: 0 client, 1 MP host, 2 local/SP
 *   server   = byte  [session + 0x3C]              Init: (mode-1) <= 1
 *   backend  = vtable of [session + 0x40]          CProxyServer 2BCBA08 / CNetworkServer 2BCB310 / CDummyServer 2BCB020
 *   my_id    = dword [session + 0x84]              GetMachineId 148D5F0; Init sets 1
 *   humans   = [gamestate + 0xF8], count [gamestate + 0x104], stride 0xA0, element vtable CHuman 279EDB8
 *              (GetLocalHuman 1F1A40 matches element+0x98 against my_id)
 *   name     = MSVC std::string at human + 0x20   (USER_NAME tooltip 1A413EB)
 *   host id  = 1                                   (host-only command check 22D8B40: cmd+0xC == 1)
 *
 * Privacy: names are copied into fixed buffers inside the snapshot, compared and dropped. The
 * log line carries only lengths and "equal/different", never the text.
 *
 * Thread: called only from the Apply path of capie_editor (UI thread, the thread that also swaps
 * the session through CGameApplication::SetSession 1A8470). Reads only; no game calls, no writes.
 */
#pragma once
#include "../../core/memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace capie::host {

using core::uptr;

/// @brief RVAs and offsets of the host chain (each one pinned in `host_pins.hpp`).
namespace layout {
inline constexpr uptr app_global = 0x33DDB08;           ///< CApplication* (RIP target in 222BDB0).
inline constexpr uptr vt_game_application = 0x2796308;  ///< CGameApplication primary vtable.
inline constexpr uptr vt_session = 0x2BB4548;           ///< CSession vtable.
inline constexpr uptr vt_proxy_server = 0x2BCBA08;      ///< Client backend.
inline constexpr uptr vt_network_server = 0x2BCB310;    ///< MP host backend.
inline constexpr uptr vt_dummy_server = 0x2BCB020;      ///< Local (single player) backend.
inline constexpr uptr vt_human = 0x279EDB8;             ///< CHuman vtable.
inline constexpr uptr gamestate_global = 0x33DD6B0;     ///< CGameState* (also pinned by the name layer).

inline constexpr uptr app_session = 0x380;            ///< 1776E0: mov rax,[rcx+380].
inline constexpr uptr session_mode = 0x34;            ///< 22508E2: mov [rcx+4C],r8d.
inline constexpr uptr session_is_server = 0x3C;       ///< 22508D5: mov [rcx+54],al.
inline constexpr uptr session_backend = 0x40;         ///< 22508FE: mov [rcx+58],rdx.
inline constexpr uptr session_machine_id = 0x84;      ///< B54190: mov eax,[rcx+A4].
inline constexpr uptr gamestate_humans = 0xF8;        ///< 1DB90F: mov r10,[rbx+F8].
inline constexpr uptr gamestate_human_count = 0x104;  ///< 1DB901: movsxd rcx,[rbx+104].
inline constexpr uptr human_stride = 0xA0;            ///< 1DB92D: add rcx,0A0.
inline constexpr uptr human_name = 0x20;              ///< 19A94BB: lea rdx,[rbx+20].
inline constexpr uptr human_machine_id = 0x98;        ///< 1DB91C / BBC6E3.
inline constexpr std::int32_t host_machine_id = 1;    ///< 2269C50: cmp dword [rcx+0C],1 = "from host".

inline constexpr std::int32_t mode_client = 0;      ///< Session mode of an MP client.
inline constexpr std::int32_t mode_mp_host = 1;     ///< Session mode of an MP host.
inline constexpr std::int32_t mode_local = 2;       ///< Session mode of a local (SP) session.
inline constexpr std::int32_t max_humans = 64;      ///< Sanity bound (HOI4 MP lobby is far smaller).
inline constexpr std::size_t max_name_bytes = 128;  ///< Longer names = unreadable (fail-closed).
inline constexpr std::uint8_t unread_flag = 0xFF;   ///< Server flag value before a read.
inline constexpr std::uint64_t sso_capacity = 15;   ///< MSVC std::string inline capacity.
inline constexpr uptr string_size = 0x10;           ///< MSVC std::string size field.
inline constexpr uptr string_capacity = 0x18;       ///< MSVC std::string capacity field.
}  // namespace layout

/// @brief One player name copied out of the game (bytes, not NUL-terminated).
struct Name {
    std::array<char, layout::max_name_bytes> bytes{};  ///< Copied bytes.
    std::size_t size{};                                ///< Valid bytes.
    bool ok{};                                         ///< Read succeeded.
};

/// @brief Everything the decision needs, read in one pass; no pointers kept.
struct Snapshot {
    bool chain_ok{};                              ///< app, session, backend vtables matched.
    std::int32_t mode{-1};                        ///< session+0x34.
    std::uint8_t is_server{layout::unread_flag};  ///< session+0x3C.
    uptr backend_vtable_rva{};                    ///< vtable of session+0x40, as RVA.
    std::int32_t my_id{-1};                       ///< session+0x84.
    std::int32_t humans{-1};                      ///< Human count read (-1 = unreadable).
    std::int32_t my_index{-1};                    ///< Index of the human whose +0x98 == my_id (unique), else -1.
    std::int32_t host_index{-1};                  ///< Index of the human whose +0x98 == 1 (unique), else -1.
    Name my_name{};                               ///< get_my_username.
    Name host_name{};                             ///< get_host_username.
};

/// @brief Result of the decision.
enum class Verdict : std::uint8_t {
    host,      ///< Would send `capie_host = 1`.
    not_host,  ///< Consistent client.
    unknown,   ///< Fail-closed: no record.
};

/**
 * @brief Reads one MSVC std::string (SSO 16); fails on size 0, > max or a bad capacity.
 * @tparam M Memory policy (`core::ProcessMemory` in game, a fake offline).
 * @param mem Memory policy.
 * @param string_address Address of the string object.
 * @param out Receives the bytes.
 * @return `true` on success.
 */
template <class M>
bool read_name(const M& mem, uptr string_address, Name& out) noexcept {
    std::uint64_t size = 0;
    std::uint64_t capacity = 0;
    if (!core::rd(mem, string_address, layout::string_size, size) ||
        !core::rd(mem, string_address, layout::string_capacity, capacity) || size == 0 ||
        size > layout::max_name_bytes || capacity < size || capacity < layout::sso_capacity) {
        return false;
    }
    uptr data = string_address;
    if (capacity > layout::sso_capacity && !core::rd(mem, string_address, 0, data)) {
        return false;
    }
    if (!mem.read(data, out.bytes.data(), static_cast<std::size_t>(size))) {
        return false;
    }
    out.size = static_cast<std::size_t>(size);
    out.ok = true;
    return true;
}

/**
 * @brief Reads the session part of the chain.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base of hoi4.exe.
 * @param s Receives mode, flag, machine id and backend.
 * @return `true` when app, session and backend were read with the expected vtables.
 */
template <class M>
bool read_session(const M& mem, uptr image, Snapshot& s) noexcept {
    uptr app = 0;
    uptr app_vt = 0;
    uptr session = 0;
    uptr session_vt = 0;
    uptr backend = 0;
    uptr backend_vt = 0;
    if (!core::rd(mem, image, layout::app_global, app) || !core::rd(mem, app, 0, app_vt) ||
        app_vt != image + layout::vt_game_application || !core::rd(mem, app, layout::app_session, session) ||
        !core::rd(mem, session, 0, session_vt) || session_vt != image + layout::vt_session) {
        return false;
    }
    if (!core::rd(mem, session, layout::session_mode, s.mode) ||
        !core::rd(mem, session, layout::session_is_server, s.is_server) ||
        !core::rd(mem, session, layout::session_machine_id, s.my_id) ||
        !core::rd(mem, session, layout::session_backend, backend) || !core::rd(mem, backend, 0, backend_vt) ||
        backend_vt < image) {
        return false;
    }
    s.backend_vtable_rva = backend_vt - image;
    return true;
}

/**
 * @brief Reads the whole chain. Never calls game code, never writes.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base of hoi4.exe.
 * @return Snapshot; unreadable parts keep their "unknown" defaults.
 */
template <class M>
Snapshot read_snapshot(const M& mem, uptr image) noexcept {
    Snapshot s{};
    if (!read_session(mem, image, s)) {
        return s;
    }
    s.chain_ok = true;
    uptr gamestate = 0;
    uptr humans = 0;
    std::int32_t count = 0;
    if (!core::rd(mem, image, layout::gamestate_global, gamestate) ||
        !core::rd(mem, gamestate, layout::gamestate_humans, humans) ||
        !core::rd(mem, gamestate, layout::gamestate_human_count, count) || count <= 0 || count > layout::max_humans) {
        return s;
    }
    s.humans = count;
    int my_hits = 0;
    int host_hits = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const uptr human = humans + (static_cast<uptr>(i) * layout::human_stride);
        uptr vt = 0;
        std::int32_t id = 0;
        if (!core::rd(mem, human, 0, vt) || vt != image + layout::vt_human ||
            !core::rd(mem, human, layout::human_machine_id, id)) {
            s.my_index = -1;  // a torn or foreign element voids the list
            s.host_index = -1;
            return s;
        }
        if (id == s.my_id) {
            s.my_index = i;
            ++my_hits;
        }
        if (id == layout::host_machine_id) {
            s.host_index = i;
            ++host_hits;
        }
    }
    if (my_hits != 1) {
        s.my_index = -1;
    }
    if (host_hits != 1) {
        s.host_index = -1;
    }
    if (s.my_index >= 0) {
        static_cast<void>(read_name(
            mem, humans + (static_cast<uptr>(s.my_index) * layout::human_stride) + layout::human_name, s.my_name));
    }
    if (s.host_index >= 0) {
        static_cast<void>(read_name(
            mem, humans + (static_cast<uptr>(s.host_index) * layout::human_stride) + layout::human_name, s.host_name));
    }
    return s;
}

/**
 * @brief Byte-equal names (the decision's "both values equal").
 * @param a First name.
 * @param b Second name.
 * @return `true` when both were read and are equal.
 */
inline bool same_name(const Name& a, const Name& b) noexcept {
    return a.ok && b.ok && a.size == b.size && std::memcmp(a.bytes.data(), b.bytes.data(), a.size) == 0;
}

/**
 * @brief Pure decision. Every signal must agree; any disagreement = unknown.
 *
 * host requires: chain ok; backend NetworkServer (MP) or DummyServer (SP) with the matching mode;
 * is_server == 1; my_id == 1; my human and host human are the same element (ids unique) and the
 * names read and equal. not_host requires a consistent client: ProxyServer, mode 0, is_server 0,
 * my_id > 1, own and host entries present, different and with readable names.
 * @param s Snapshot.
 * @return Verdict.
 */
inline Verdict decide(const Snapshot& s) noexcept {
    if (!s.chain_ok || (s.is_server != 0 && s.is_server != 1)) {
        return Verdict::unknown;
    }
    const bool backend_host = s.backend_vtable_rva == layout::vt_network_server && s.mode == layout::mode_mp_host;
    const bool backend_local = s.backend_vtable_rva == layout::vt_dummy_server && s.mode == layout::mode_local;
    const bool backend_client = s.backend_vtable_rva == layout::vt_proxy_server && s.mode == layout::mode_client;
    if (backend_host || backend_local) {
        const bool ok = s.is_server == 1 && s.my_id == layout::host_machine_id && s.my_index >= 0 &&
                        s.my_index == s.host_index && same_name(s.my_name, s.host_name);
        return ok ? Verdict::host : Verdict::unknown;
    }
    if (backend_client) {
        const bool ok = s.is_server == 0 && s.my_id > layout::host_machine_id && s.my_index >= 0 && s.host_index >= 0 &&
                        s.my_index != s.host_index && s.my_name.ok && s.host_name.ok;
        return ok ? Verdict::not_host : Verdict::unknown;
    }
    return Verdict::unknown;  // rehost (mode 0 with NetworkServer) lands here: fail-closed
}

/**
 * @brief Backend name for the log.
 * @param s Snapshot.
 * @return "Dummy", "Network", "Proxy", "other" or "-" (chain unreadable).
 */
inline const char* backend_name(const Snapshot& s) noexcept {
    if (!s.chain_ok) {
        return "-";
    }
    switch (s.backend_vtable_rva) {
        case layout::vt_dummy_server:
            return "Dummy";
        case layout::vt_network_server:
            return "Network";
        case layout::vt_proxy_server:
            return "Proxy";
        default:
            return "other";
    }
}

/// @brief Size of a probe log line buffer.
inline constexpr std::size_t line_capacity = 256;

/**
 * @brief Formats the debug log line of one Apply (lengths and equality only, never names).
 * @param s Snapshot.
 * @param verdict Verdict of `decide(s)`.
 * @param line Receives the NUL-terminated line with CRLF.
 * @return Bytes stored (0 on a formatting error).
 */
inline std::size_t format_line(const Snapshot& s, Verdict verdict, std::array<char, line_capacity>& line) noexcept {
    const char* would_be = "unreadable";
    if (verdict != Verdict::unknown) {
        would_be = verdict == Verdict::host ? "yes" : "no";
    }
    const bool own = s.my_index >= 0;
    const char* own_is_host = "unknown";
    if (own && s.host_index >= 0) {
        own_is_host = s.my_index == s.host_index ? "yes" : "no";
    }
    const char* names = "unreadable";
    if (s.my_name.ok && s.host_name.ok) {
        names = same_name(s.my_name, s.host_name) ? "equal" : "different";
    }
    const int count = std::snprintf(
        line.data(), line.size(),
        "CAPIE_HOST probe would_be_host=%s mode=%d server=%u backend=%s machine=%d humans=%d own_entry=%s "
        "own_is_host=%s my_len=%zu host_len=%zu names=%s record=none\r\n",
        would_be, s.mode, static_cast<unsigned>(s.is_server), backend_name(s), s.my_id, s.humans,
        own ? "found" : "missing", own_is_host, s.my_name.ok ? s.my_name.size : 0U,
        s.host_name.ok ? s.host_name.size : 0U, names);
    if (count <= 0) {
        return 0;
    }
    const auto wanted = static_cast<std::size_t>(count);
    return wanted < line.size() ? wanted : line.size() - 1;
}

/**
 * @brief Reads, decides and formats in one call (the Apply-path entry point).
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param line Receives the line.
 * @return Bytes stored.
 */
template <class M>
std::size_t probe_line(const M& mem, uptr image, std::array<char, line_capacity>& line) noexcept {
    const Snapshot s = read_snapshot(mem, image);
    return format_line(s, decide(s), line);
}

}  // namespace capie::host
