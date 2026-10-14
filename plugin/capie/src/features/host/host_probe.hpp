/**
 * @file host_probe.hpp
 * @brief SKETCH (not built, not included anywhere): read-only host detection for the `capie_host` record.
 *
 * Status: design sketch for docs/evidence/session-20261014/host-detect/report.txt. No CMake target,
 * no include from public_editor.hpp. Wiring is described in the report (section 7) and must stay
 * behind `CAPIE_HOST_RECORD` (default 0) until the two-client test passes.
 *
 * Decision (docs/API-DECISIONS.md, 2026-10-14): CAPIE exposes `get_my_username` and
 * `get_host_username`; is_host = both equal; only the bool travels, as `capie_host = 1` in the
 * Apply command built on the sender's machine; any unreadable value = no record.
 *
 * Engine chain (hoi4.exe 1.19.3.0, static evidence in the report):
 *   app      = [image + 0x3452450]                 GetApplication() 222BDB0; vtable CGameApplication 27182E0
 *   session  = [app + 0x380]                       CGameApplication::GetSession 1776E0 (vslot 0x88)
 *              vtable CSession 2B3E9D8
 *   mode     = dword [session + 0x4C]              CSession::Init 2250890: 0 client, 1 MP host, 2 local/SP
 *   server   = byte  [session + 0x54]              Init: (mode-1) <= 1; rehost 22502F0 sets 1
 *   backend  = vtable of [session + 0x58]          CProxyServer 2B53598 / CNetworkServer 2B53020 / CDummyServer 2B52C28
 *   my_id    = dword [session + 0xA4]              GetMachineId B54190; Init sets 1, clients get SetMachineId 2252480
 *   humans   = [gamestate + 0xF8], count [gamestate + 0x104], stride 0xA0, element vtable CHuman 2720DA8
 *              (GetLocalHuman 1DB8E0 matches element+0x98 against my_id)
 *   name     = MSVC std::string at human + 0x20   (USER_NAME tooltip 19A94BB, "Added player" log 1997E65)
 *   host id  = 1                                   (host-only command check 2269C50: cmd+0xC == 1)
 *
 * Privacy: names are copied into a fixed stack buffer, compared, and dropped. They are never
 * logged by the release flavor, never written to game state or variables; the script only sees
 * the bool record. The debug flavor may log lengths and a 32-bit hash, never the text.
 *
 * Thread: called only from the Apply path of capie_editor (UI thread, the thread that also swaps
 * the session through CGameApplication::SetSession 1A8470 and reads humans in UI tooltips).
 */
#pragma once
#include "../../core/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::host {

using core::uptr;

/// @brief RVAs and offsets of the host chain (would move to core/game_layout.hpp when wired).
namespace layout {
inline constexpr uptr app_global = 0x3452450;         ///< CApplication* (RIP target in 222BDB0).
inline constexpr uptr vt_game_application = 0x27182E0;  ///< CGameApplication primary vtable.
inline constexpr uptr vt_session = 0x2B3E9D8;         ///< CSession vtable.
inline constexpr uptr vt_proxy_server = 0x2B53598;    ///< Client backend.
inline constexpr uptr vt_network_server = 0x2B53020;  ///< MP host backend.
inline constexpr uptr vt_dummy_server = 0x2B52C28;    ///< Local (single player) backend.
inline constexpr uptr vt_human = 0x2720DA8;           ///< CHuman vtable.
inline constexpr uptr gamestate_global = 0x332F260;   ///< CGameState* (already pinned by the name layer).

inline constexpr uptr app_session = 0x380;       ///< 1776E0: mov rax,[rcx+380].
inline constexpr uptr session_mode = 0x4C;       ///< 22508E2: mov [rcx+4C],r8d.
inline constexpr uptr session_is_server = 0x54;  ///< 22508D5: mov [rcx+54],al; 1CD569A: cmp byte [rax+54],0.
inline constexpr uptr session_backend = 0x58;    ///< 22508FE: mov [rcx+58],rdx.
inline constexpr uptr session_machine_id = 0xA4; ///< B54190: mov eax,[rcx+A4].
inline constexpr uptr gamestate_humans = 0xF8;       ///< 1DB90F: mov r10,[rbx+F8].
inline constexpr uptr gamestate_human_count = 0x104; ///< 1DB901: movsxd rcx,[rbx+104].
inline constexpr uptr human_stride = 0xA0;           ///< 1DB92D: add rcx,0A0.
inline constexpr uptr human_name = 0x20;             ///< 19A94BB: lea rdx,[rbx+20]; ctor BBC66A.
inline constexpr uptr human_machine_id = 0x98;       ///< 1DB91C / BBC6E3.
inline constexpr std::int32_t host_machine_id = 1;   ///< 2269C50: cmp dword [rcx+0C],1 = "from host".

inline constexpr std::int32_t mode_client = 0;
inline constexpr std::int32_t mode_mp_host = 1;
inline constexpr std::int32_t mode_local = 2;
inline constexpr std::int32_t max_humans = 64;       ///< Sanity bound (HOI4 MP lobby is far smaller).
inline constexpr std::size_t max_name_bytes = 128;   ///< Longer names = unreadable (fail-closed).
}  // namespace layout

/// @brief One player name copied out of the game (UTF-8 bytes, not NUL-terminated).
struct Name {
    char bytes[layout::max_name_bytes]{};
    std::size_t size{};
    bool ok{};
};

/// @brief Everything the decision needs, read in one pass; no pointers kept.
struct Snapshot {
    bool chain_ok{};              ///< app, session, backend vtables matched.
    std::int32_t mode{-1};        ///< session+0x4C.
    std::uint8_t is_server{0xFF}; ///< session+0x54.
    uptr backend_vtable_rva{};    ///< vtable of session+0x58, as RVA.
    std::int32_t my_id{-1};       ///< session+0xA4.
    std::int32_t my_index{-1};    ///< index of the human whose +0x98 == my_id (unique), else -1.
    std::int32_t host_index{-1};  ///< index of the human whose +0x98 == 1 (unique), else -1.
    Name my_name{};               ///< get_my_username.
    Name host_name{};             ///< get_host_username.
};

/// @brief Result for the Apply path.
enum class Verdict : std::uint8_t {
    host,        ///< send `capie_host = 1`.
    not_host,    ///< no record (script: not host).
    unknown,     ///< no record (fail-closed); reason in the debug log only.
};

/**
 * @brief Reads one MSVC std::string (SSO 16) into `out`; fail on size 0, > max, or bad capacity.
 * @tparam M Memory policy (`core::ProcessMemory` in game, a fake offline).
 */
template <class M>
bool read_name(const M& mem, uptr string_address, Name& out) noexcept {
    std::uint64_t size = 0;
    std::uint64_t capacity = 0;
    if (!core::rd(mem, string_address, 0x10, size) || !core::rd(mem, string_address, 0x18, capacity) ||
        size == 0 || size > layout::max_name_bytes || capacity < size || capacity < 0xF) {
        return false;
    }
    uptr data = string_address;
    if (capacity > 0xF && !core::rd(mem, string_address, 0, data)) {
        return false;
    }
    if (!mem.read(data, out.bytes, static_cast<std::size_t>(size))) {
        return false;
    }
    out.size = static_cast<std::size_t>(size);
    out.ok = true;
    return true;
}

/**
 * @brief Reads the whole chain. Never calls game code, never writes.
 * @param image Loaded image base of hoi4.exe.
 */
template <class M>
Snapshot read_snapshot(const M& mem, uptr image) noexcept {
    Snapshot s{};
    uptr app = 0, app_vt = 0, session = 0, session_vt = 0, backend = 0, backend_vt = 0;
    if (!core::rd(mem, image, layout::app_global, app) || !core::rd(mem, app, 0, app_vt) ||
        app_vt != image + layout::vt_game_application || !core::rd(mem, app, layout::app_session, session) ||
        !core::rd(mem, session, 0, session_vt) || session_vt != image + layout::vt_session ||
        !core::rd(mem, session, layout::session_mode, s.mode) ||
        !core::rd(mem, session, layout::session_is_server, s.is_server) ||
        !core::rd(mem, session, layout::session_backend, backend) || !core::rd(mem, backend, 0, backend_vt) ||
        !core::rd(mem, session, layout::session_machine_id, s.my_id) || backend_vt < image) {
        return s;
    }
    s.backend_vtable_rva = backend_vt - image;
    s.chain_ok = true;

    uptr gamestate = 0, humans = 0;
    std::int32_t count = 0;
    if (!core::rd(mem, image, layout::gamestate_global, gamestate) ||
        !core::rd(mem, gamestate, layout::gamestate_humans, humans) ||
        !core::rd(mem, gamestate, layout::gamestate_human_count, count) || count <= 0 || count > layout::max_humans) {
        return s;
    }
    int my_hits = 0, host_hits = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        const uptr human = humans + static_cast<uptr>(i) * layout::human_stride;
        uptr vt = 0;
        std::int32_t id = 0;
        if (!core::rd(mem, human, 0, vt) || vt != image + layout::vt_human ||
            !core::rd(mem, human, layout::human_machine_id, id)) {
            s.my_index = s.host_index = -1;  // a torn or foreign element voids the list
            return s;
        }
        if (id == s.my_id) { s.my_index = i; ++my_hits; }
        if (id == layout::host_machine_id) { s.host_index = i; ++host_hits; }
    }
    if (my_hits != 1) s.my_index = -1;
    if (host_hits != 1) s.host_index = -1;
    if (s.my_index >= 0) {
        read_name(mem, humans + static_cast<uptr>(s.my_index) * layout::human_stride + layout::human_name, s.my_name);
    }
    if (s.host_index >= 0) {
        read_name(mem, humans + static_cast<uptr>(s.host_index) * layout::human_stride + layout::human_name,
                  s.host_name);
    }
    return s;
}

/// @brief Byte-equal names (the decision's "both values equal").
inline bool same_name(const Name& a, const Name& b) noexcept {
    return a.ok && b.ok && a.size == b.size && std::memcmp(a.bytes, b.bytes, a.size) == 0;
}

/**
 * @brief Pure decision (offline-testable). Every signal must agree; any disagreement = unknown.
 *
 * host requires: chain ok; backend NetworkServer (MP) or DummyServer (SP) with the matching mode;
 * is_server == 1; my_id == 1; my human and host human are the same element (ids unique) and the
 * names read and equal. not_host requires a consistent client: ProxyServer, mode 0, is_server 0,
 * my_id != 1, host human present with a readable name that differs from mine OR is another element.
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
        const bool ok = s.is_server == 0 && s.my_id != layout::host_machine_id && s.my_id > 0 && s.my_index >= 0 &&
                        s.host_index >= 0 && s.my_index != s.host_index && s.my_name.ok && s.host_name.ok;
        return ok ? Verdict::not_host : Verdict::unknown;
    }
    return Verdict::unknown;  // rehost (proxy -> network with my_id != 1) lands here: fail-closed
}

}  // namespace capie::host
