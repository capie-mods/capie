/**
 * @file vp_view.hpp
 * @brief The state/VP view (API level 9, CONTRACT.md 21): a state's victory-point provinces, read at the click.
 *
 * Read-only walk of the game's own structures (runtime catalogs research section 4, spec 3.1):
 *
 *     scope (context + 0x3C0) + 0xA8   state id (int32; > 0 for a state scope)
 *     gamestate + 0x2C8 / + 0x2D4      CState* array by id / its slot count (slot 0 empty)
 *     state + 0x18 / + 0x24            CProvince* array / its count
 *     province + 0x38                  victory points (int32; > 0 = a VP)
 *     province + 0xA4                  province id
 *
 * Every state and province read must carry its RTTI-checked vtable. The VPs are sorted by value,
 * highest first, then by province id, and capped at 32. Names are not read here: the caller asks
 * the game's own `ProvinceName` for each province (custom name, else the tag's VP name, else the
 * VP name) on the UI thread, after `ThreadForbidCount` was checked (`thread_forbid_count`).
 *
 * Pure functions templated over the memory policy; no writes, no game calls.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace capie::catalog::vp {

using core::uptr;

/// @brief Most VP rows one view carries (`capie_vp_0` .. `capie_vp_31`).
inline constexpr std::size_t max_rows = 32;
/// @brief Sanity bound of the state array (slots).
inline constexpr std::int32_t max_states = 65536;
/// @brief Sanity bound of a state's province count.
inline constexpr std::int32_t max_provinces = 4096;

/// @brief One VP row.
struct Row {
    uptr province{};       ///< Province object (used within the click only).
    std::int32_t id{};     ///< Province id.
    std::int32_t value{};  ///< Victory points.
};

/// @brief Outcome of a read.
enum class Read : unsigned {
    ok,          ///< `rows` holds the state's VPs (possibly none).
    no_state,    ///< The scope holds no state (an empty view, not an error).
    unreadable,  ///< A pointer, count or vtable did not check out (fail closed: an empty view).
};

/// @brief The VPs of one state.
struct List {
    std::int32_t state{};             ///< State id (0 = none).
    std::array<Row, max_rows> rows{};  ///< Sorted rows.
    std::size_t count{};              ///< Rows used.
    std::size_t dropped{};            ///< VPs past the cap (logged by the caller).
};

/**
 * @brief Reads the state id of a scope.
 * @param mem Memory policy.
 * @param scope Scope object (the window context + 0x3C0).
 * @param state Receives the id (0 when none).
 * @return `false` when unreadable.
 */
template <class M>
bool scope_state(const M& mem, uptr scope, std::int32_t& state) noexcept {
    state = 0;
    std::int32_t id = 0;
    if (!core::rd(mem, scope, core::off::scope_state_id, id)) {
        return false;
    }
    state = id > 0 ? id : 0;
    return true;
}

/**
 * @brief Whether an object has the expected vtable.
 * @param mem Memory policy.
 * @param object Object.
 * @param vtable Expected vtable (absolute).
 * @return `true` on a match.
 */
template <class M>
bool has_vtable(const M& mem, uptr object, uptr vtable) noexcept {
    uptr seen = 0;
    return object != 0 && core::rd(mem, object, 0, seen) && seen == vtable;
}

/**
 * @brief Order of the view: VP value descending, then province id ascending.
 * @param a Row.
 * @param b Row.
 * @return `true` when `a` comes first.
 */
constexpr bool before(const Row& a, const Row& b) noexcept {
    return a.value != b.value ? a.value > b.value : a.id < b.id;
}

/**
 * @brief Reads the VP provinces of a state.
 * @param mem Memory policy.
 * @param image Image base (vtables and the game-state global).
 * @param state State id (> 0).
 * @param out Receives the rows.
 * @return `ok` or `unreadable`.
 */
template <class M>
Read read_state(const M& mem, uptr image, std::int32_t state, List& out) noexcept {
    out = List{};
    out.state = state;
    uptr gamestate = 0;
    uptr states = 0;
    std::int32_t slots = 0;
    if (state <= 0) {
        return Read::no_state;
    }
    if (!core::rd(mem, image, core::rva::gamestate_global, gamestate) || gamestate == 0 ||
        !core::rd(mem, gamestate, core::off::gamestate_states, states) ||
        !core::rd(mem, gamestate, core::off::gamestate_state_count, slots) || states == 0 || slots <= 0 ||
        slots > max_states) {
        return Read::unreadable;
    }
    if (state >= slots) {
        return Read::no_state;  // an id the game does not have
    }
    uptr object = 0;
    std::int32_t count = 0;
    uptr provinces = 0;
    if (!core::rd(mem, states, static_cast<uptr>(state) * sizeof(uptr), object) ||
        !has_vtable(mem, object, image + core::rva::vt_state) ||
        !core::rd(mem, object, core::off::state_province_count, count) || count < 0 || count > max_provinces ||
        (count != 0 && (!core::rd(mem, object, core::off::state_provinces, provinces) || provinces == 0))) {
        return Read::unreadable;
    }
    for (std::int32_t index = 0; index < count; ++index) {
        Row row{};
        if (!core::rd(mem, provinces, static_cast<uptr>(index) * sizeof(uptr), row.province) ||
            !has_vtable(mem, row.province, image + core::rva::vt_province) ||
            !core::rd(mem, row.province, core::off::province_vp, row.value) ||
            !core::rd(mem, row.province, core::off::province_id, row.id)) {
            out = List{};
            out.state = state;
            return Read::unreadable;
        }
        if (row.value <= 0) {
            continue;
        }
        if (out.count < max_rows) {
            out.rows[out.count++] = row;
            continue;
        }
        // Full: keep the best 32 (replace the last row in view order when this one comes first).
        ++out.dropped;
        std::sort(out.rows.begin(), out.rows.end(), before);
        if (before(row, out.rows[max_rows - 1])) {
            out.rows[max_rows - 1] = row;
        }
    }
    std::sort(out.rows.begin(), out.rows.begin() + static_cast<std::ptrdiff_t>(out.count), before);
    return Read::ok;
}

/**
 * @brief Reads `ThreadForbidCount` of the calling thread (the game's static TLS block).
 * @param mem Memory policy.
 * @param image Image base (`rva::tls_index` holds the game's `_tls_index`).
 * @param tls_array The thread's `ThreadLocalStoragePointer` (`gs:[0x58]`).
 * @param count Receives the counter.
 * @return `false` when unreadable (callers treat that as "forbidden").
 */
template <class M>
bool thread_forbid_count(const M& mem, uptr image, uptr tls_array, std::int32_t& count) noexcept {
    count = -1;
    std::uint32_t index = 0;
    uptr block = 0;
    std::int32_t value = 0;
    if (tls_array == 0 || !core::rd(mem, image, core::rva::tls_index, index) ||
        !core::rd(mem, tls_array, static_cast<uptr>(index) * sizeof(uptr), block) || block == 0 ||
        !core::rd(mem, block, core::off::tls_thread_forbid, value)) {
        return false;
    }
    count = value;
    return true;
}

}  // namespace capie::catalog::vp
