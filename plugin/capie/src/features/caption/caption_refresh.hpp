/**
 * @file caption_refresh.hpp
 * @brief Refreshes the politics leader caption and party row right after a successful rename.
 *
 * Problem it fixes: after a leader or party rename the politics panel kept the old
 * caption until it was closed and reopened. After each successful Apply the script raises
 * a plain success counter (`rn_ip_lui_rev` leader, `rn_ip_pui_rev` party) that is shown in
 * a hidden carrier text of the editor and launcher windows. When the counter goes up,
 * this module asks the game once to redraw the politics view with its own native request
 * `B6E210(gui, 12)`. That call only marks the view dirty; the normal frame loop then runs
 * the view's observer, which reads current data and rewrites the caption and rows with
 * native formatting. CAPIE never writes a caption, runs a command or rebuilds a widget.
 *
 * Contract (rev7, reviews R1..R6):
 *   - Seed only at prefill. `on_prefill(window)` is the only source of a baseline and is
 *     called by the editor bridge after its one-shot opening prefill succeeded. Ticks and
 *     launchers never seed; an existing seed is never overwritten.
 *   - Apply gate. `on_dispatch_verdict(window)` is the hard gate before an own leader or
 *     party Apply: unseeded, unreadable carrier, value jump, rollback suspicion or counter
 *     cap means the Apply is not sent with a payload; the verdict says which sentinel.
 *   - Stale text vs rollback. A lower value is treated as stale carrier text for 1-2
 *     reads; 3 consecutive lower reads confirm a rollback and drop the baseline.
 *   - Bind resets everything. Equal or higher counters after a load without Bind are
 *     accepted (worst case one redundant or missed redraw; never a write or command).
 *   - Minimal refresh guard: a request needs a seed in this bind generation, a counter
 *     increase without rollback suspicion, a visible own window and a fresh pointer chain
 *     whose parent is the current politics view root.
 *   - One native request per plan, no lock across it, no retry after a failure; acks and
 *     failures count only results verified after the call (fresh relookup, same gui/view,
 *     dirty == 1, counter re-read).
 *
 * Pointers live only on the stack of one call; the tracker holds scalars. The guard chain
 * and window inspection are templates over a memory policy, so the same code runs in the
 * game (`ProcessMemory`) and in the offline fake-memory tests.
 *
 * Risks / OPEN: carrier freshness in the first Tick after Apply is not proven (a short
 * rollback looks like stale text); world identity across a bindless load is not checked;
 * counters stop at 999 per field and save (fail-safe).
 */
#pragma once
#include "../../core/memory.hpp"
#include "../../core/platform_game_layout.hpp"
#if !defined(__APPLE__)
#include "../../core/signature_table.hpp"
#endif
#include "../../engine/platform_string.hpp"
#if defined(__APPLE__)
#include "../../engine/macos_records.hpp"
#endif
#include "../editor/schema.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

namespace capie::caption {

using core::rd;
using core::u64;
using core::uptr;
using engine::string_equals;
namespace rva = core::rva;
namespace off = core::off;

/// @brief Token value of `politics_tab`.
inline constexpr std::uint32_t token_politics_tab_value = core::value::token_politics_tab;
/// @brief Token value of `player_context`.
inline constexpr std::uint32_t token_player_context_value = core::value::token_player_context;
/// @brief Politics view index passed to `B6E210`.
inline constexpr int politics_view_index = core::value::politics_view_index;
/// @brief View index ignored by `B6E210`.
inline constexpr int gui_view_limit = core::value::gui_view_limit;
/// @brief Offset of the diagnostic allocator copy inside the GUI container.
inline constexpr uptr gui_structural_offset = off::gui_structural;
/// @brief RVA expected at that offset (diagnostic only, never a gate).
inline constexpr uptr gui_structural_rva = rva::gui_structural;
#if defined(__APPLE__)
/// @brief Native slot triplet, independently mapped in editor-map.md and core-pins.json.
struct NativeSlotPin {
    uptr vtable;
    uptr slot;
    uptr target;
};
/// @brief Four native vtable checks retained by resolve_view; no Windows fingerprint reuse.
inline constexpr NativeSlotPin slot_pins[] = {
    {rva::vt_politics_view, off::slot_view_visible, rva::view_is_visible},
    {rva::vt_politics_observer, off::slot_observer_frame, rva::observer_frame},
    {rva::vt_politics_observer, off::slot_observer_refresh, rva::observer_refresh},
    {rva::vt_ingame_idler, off::slot_idler_gui, rva::idler_gui_getter},
};
#else
/// @brief The 25 caption fingerprints (alias of the central table).
inline constexpr const auto& fingerprints = core::caption_signatures;
/// @brief Number of caption fingerprints.
inline constexpr std::size_t fingerprint_count = core::caption_signature_count;
/// @brief Caption vtable slot pins (alias of the central table).
inline constexpr const auto& slot_pins = core::caption_slot_pins;
using core::decode_hex;
using core::hex_nibble;

#endif

// ---------------------------------------------------------------------------------------------
// Revision counters. HOI4 variables are fixed point; `[?var]` formatting of values >= 1000 is
// unverified, so the accepted range is 0..999 as plain digits and Apply is gated below 999.

/// @brief Largest accepted counter value.
inline constexpr u64 max_revision = 999;
/// @brief Longest accepted counter text.
inline constexpr std::size_t max_revision_digits = 3;
/// @brief Consecutive lower reads that confirm a rollback.
inline constexpr unsigned rollback_confirm_reads = 3;
/// @brief Size of the carrier text buffer.
inline constexpr std::size_t revision_text_capacity = 32;

/**
 * @brief Parses a counter: 1..3 decimal digits, no leading zero, value <= 999.
 * @param text Bytes.
 * @param size Length.
 * @param out Receives the value.
 * @return `false` for anything else (never read as 0).
 */
inline bool parse_revision(const char* text, std::size_t size, u64& out) noexcept {
    if (text == nullptr || size == 0 || size > max_revision_digits) {
        return false;
    }
    if (size > 1 && text[0] == '0') {
        return false;
    }
    u64 value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10) + (c - '0');  // NOLINT(readability-magic-numbers): decimal base
    }
    if (value > max_revision) {
        return false;
    }
    out = value;
    return true;
}

/// @brief Caption field.
enum class Field : unsigned { leader = 0, party = 1 };
/// @brief Number of caption fields.
inline constexpr std::size_t field_count = 2;
/// @brief Window role: editors seed and gate Apply, launchers only observe.
enum class Role : unsigned { editor, launcher };
/// @brief One recognized window model.
struct ModelSpec {
    const char* name;     ///< Model name.
    Field field;          ///< Field it reports.
    Role role;            ///< Editor or launcher.
    const char* carrier;  ///< Revision carrier widget name.
};
/// @brief The four models with a revision carrier (names from the shared editor schema).
inline constexpr ModelSpec model_specs[] = {
    {editor::models[editor::model_index::leader].name, Field::leader, Role::editor, editor::leader_revision_carrier},
    {editor::leader_launcher_model, Field::leader, Role::launcher, editor::leader_revision_carrier},
    {editor::models[editor::model_index::party].name, Field::party, Role::editor, editor::party_revision_carrier},
    {editor::party_launcher_model, Field::party, Role::launcher, editor::party_revision_carrier},
};
/// @brief Number of caption models.
inline constexpr std::size_t model_count = sizeof(model_specs) / sizeof(model_specs[0]);

// ---------------------------------------------------------------------------------------------
// Scalar tracker. Never stores an engine pointer. Not internally locked: Service holds a mutex
// around every call and never across a native call.

/// @brief Per-field scalar state.
struct FieldState {
    bool seeded{};            ///< Baseline exists (from prefill).
    bool pending{};           ///< Observed counter is above the acknowledged one.
    bool suspended{};         ///< A failed refresh suspended this revision.
    bool deferred{};          ///< A precall reject deferred to the next visibility edge.
    bool poisoned{};          ///< Rollback/invalid: seeding refused until the next edge.
    u64 acked{};              ///< Last verified refreshed revision.
    u64 observed{};           ///< Last observed revision.
    u64 suspended_rev{};      ///< Revision that failed.
    u64 deferred_edge{};      ///< Edge at deferral.
    u64 poisoned_edge{};      ///< Edge at poisoning.
    unsigned lower_streak{};  ///< Consecutive lower reads.
};
/// @brief Counters for tests and diagnostics. `native_calls` counts attempts; acks/failures verified results.
struct Counters {
    u64 seeds{};                          ///< Baselines created.
    u64 seed_refusals{};                  ///< Seeds refused.
    u64 observations_ignored_unseeded{};  ///< Observations before a seed.
    u64 stale_lower_reads{};              ///< Lower reads not yet confirmed.
    u64 rollbacks{};                      ///< Confirmed rollbacks.
    u64 binds{};                          ///< Bind resets.
    u64 invalid_resets{};                 ///< Invalid-context resets.
    u64 dispatch_allowed{};               ///< Apply allowed.
    u64 dispatch_rejected{};              ///< Apply rejected.
    u64 plans{};                          ///< Refresh plans.
    u64 precall_rejects{};                ///< Plans rejected before the call.
    u64 native_calls{};                   ///< Call attempts.
    u64 acks{};                           ///< Verified refreshes.
    u64 failures{};                       ///< Failed refreshes.
    u64 stale_results{};                  ///< Results for a superseded plan.
};
/// @brief One refresh plan (at most one in flight).
struct Plan {
    bool valid{};                ///< Plan exists.
    u64 serial{};                ///< Plan serial.
    bool fields[field_count]{};  ///< Fields covered.
    u64 target[field_count]{};   ///< Revision each field must reach.
};

/// @brief Scalar state machine of the caption refresh (see file comment).
class Tracker {
public:
    /// @brief Outcome of `seed`.
    enum class Seed { seeded, observed, refused_poisoned, refused_bad };
    /// @brief Outcome of `observe`.
    enum class Observe { ignored_unseeded, unchanged, increased, lower_unconfirmed, rollback_confirmed, invalid_value };

    /**
     * @brief Prefill-only entry point: creates the baseline, never overwrites an existing one.
     * @param f Field.
     * @param rev Counter value read at prefill (> 999 = refusal).
     * @return Outcome.
     */
    Seed seed(Field f, u64 rev) noexcept {
        auto& s = state_[idx(f)];
        if (rev > max_revision) {
            ++counters_.seed_refusals;
            return Seed::refused_bad;
        }
        if (s.poisoned && s.poisoned_edge == edge_[idx(f)]) {
            ++counters_.seed_refusals;
            return Seed::refused_poisoned;
        }
        if (!s.seeded) {
            s = FieldState{};
            s.seeded = true;
            s.acked = rev;
            s.observed = rev;
            ++counters_.seeds;
            return Seed::seeded;
        }
        observe(f, rev);  // an existing seed is never overwritten by a later value
        return Seed::observed;
    }

    /**
     * @brief Tick observation; never creates a baseline. Lower values are stale text until confirmed.
     * @param f Field.
     * @param rev Counter value.
     * @return Outcome.
     */
    Observe observe(Field f, u64 rev) noexcept {
        auto& s = state_[idx(f)];
        if (rev > max_revision) {
            return Observe::invalid_value;
        }
        if (!s.seeded) {
            ++counters_.observations_ignored_unseeded;
            return Observe::ignored_unseeded;
        }
        if (rev < s.observed) {
            if (++s.lower_streak >= rollback_confirm_reads) {
                poison(f);
                ++counters_.rollbacks;
                return Observe::rollback_confirmed;
            }
            ++counters_.stale_lower_reads;
            return Observe::lower_unconfirmed;
        }
        s.lower_streak = 0;
        if (rev == s.observed) {
            return Observe::unchanged;
        }
        s.observed = rev;
        if (s.suspended && rev > s.suspended_rev) {
            s.suspended = false;  // new success = new attempt
        }
        s.deferred = false;  // new success = new attempt
        s.pending = s.observed > s.acked;
        return Observe::increased;
    }

    /**
     * @brief Apply gate: seed required, no unobserved jump, no rollback suspicion, headroom for +1.
     * @param f Field.
     * @param rev_now Counter value read right before dispatch.
     * @return `true` when allowed.
     */
    bool dispatch_allowed(Field f, u64 rev_now) noexcept {
        const auto& s = state_[idx(f)];
        const bool ok = s.seeded && s.lower_streak == 0 && rev_now == s.observed && rev_now < max_revision;
        if (ok) {
            ++counters_.dispatch_allowed;
        } else {
            ++counters_.dispatch_rejected;
        }
        return ok;
    }
    /// @brief Counts a rejection decided outside the tracker.
    void count_dispatch_reject() noexcept { ++counters_.dispatch_rejected; }
    /**
     * @brief Bind of a field's window: new bind generation, full reset.
     * @param f Field.
     */
    void bind(Field f) noexcept {
        ++bind_gen_[idx(f)];
        reset(f);
        ++counters_.binds;
    }
    /**
     * @brief Invalid context: reset and refuse seeding until the next visibility edge.
     * @param f Field.
     */
    void invalid(Field f) noexcept {
        poison(f);
        ++counters_.invalid_resets;
    }
    /**
     * @brief Records a model's visibility; a hidden->visible change is a new edge of its field.
     * @param model Model index.
     * @param visible Visible now.
     */
    void visibility(std::size_t model, bool visible) noexcept {
        if (model >= model_count) {
            return;
        }
        if (visible && !visible_[model]) {
            ++edge_[idx(model_specs[model].field)];
        }
        visible_[model] = visible;
    }

    /**
     * @brief Plans at most one refresh covering every pending field.
     * @return Plan (invalid when nothing is due or a plan is in flight).
     */
    Plan plan() noexcept {
        Plan p{};
        if (inflight_.valid) {
            return p;  // also the re-entrancy guard
        }
        for (std::size_t i = 0; i < field_count; ++i) {
            const auto& s = state_[i];
            if (!s.seeded || !s.pending || s.observed <= s.acked) {
                continue;
            }
            if (s.lower_streak) {
                continue;  // stale/rollback suspicion: no request on unconfirmed text
            }
            if (s.suspended && s.suspended_rev >= s.observed) {
                continue;
            }
            if (s.deferred && s.deferred_edge == edge_[i]) {
                continue;
            }
            p.fields[i] = true;
            p.target[i] = s.observed;
            p.valid = true;
        }
        if (!p.valid) {
            return p;
        }
        p.serial = ++serial_;
        inflight_ = p;
        for (std::size_t i = 0; i < field_count; ++i) {
            inflight_gen_[i] = bind_gen_[i];
        }
        ++counters_.plans;
        return p;
    }

    /**
     * @brief Guards failed before any native call: wait for a new visibility edge or a new success.
     * @param serial Plan serial.
     */
    void precall_rejected(u64 serial) noexcept {
        if (!take(serial)) {
            return;
        }
        for (std::size_t i = 0; i < field_count; ++i) {
            if (last_.fields[i] && current(i)) {
                state_[i].deferred = true;
                state_[i].deferred_edge = edge_[i];
            }
        }
        ++counters_.precall_rejects;
    }
    /**
     * @brief Records a call attempt (before the call).
     * @param serial Plan serial.
     */
    void called(u64 serial) noexcept {
        if (inflight_.valid && inflight_.serial == serial) {
            ++counters_.native_calls;
        }
    }
    /**
     * @brief Records a verified result after the call.
     * @param serial Plan serial.
     * @param ok Fresh relookup, same gui/view, dirty == 1 and counter re-read all passed.
     */
    void finished(u64 serial, bool ok) noexcept {
        if (!take(serial)) {
            ++counters_.stale_results;
            return;
        }
        bool any = false;
        for (std::size_t i = 0; i < field_count; ++i) {
            if (last_.fields[i] && current(i)) {
                any = true;
                auto& s = state_[i];
                if (ok) {
                    s.acked = (std::max)(s.acked, last_.target[i]);
                    s.pending = s.observed > s.acked;
                } else {
                    s.suspended = true;
                    s.suspended_rev = last_.target[i];
                }
            }
        }
        if (!any) {
            ++counters_.stale_results;
            return;
        }
        if (ok) {
            ++counters_.acks;
        } else {
            ++counters_.failures;
        }
    }
    /**
     * @brief Field state (tests, diagnostics).
     * @param f Field.
     * @return State reference.
     */
    [[nodiscard]] const FieldState& state(Field f) const noexcept { return state_[idx(f)]; }
    /// @brief Counters. @return Reference.
    [[nodiscard]] const Counters& counters() const noexcept { return counters_; }
    /// @brief Whether a plan is in flight. @return Flag.
    [[nodiscard]] bool inflight() const noexcept { return inflight_.valid; }
    /**
     * @brief Bind generation of a field.
     * @param f Field.
     * @return Generation.
     */
    [[nodiscard]] u64 bind_generation(Field f) const noexcept { return bind_gen_[idx(f)]; }

private:
    static std::size_t idx(Field f) noexcept { return static_cast<std::size_t>(f); }
    [[nodiscard]] bool current(std::size_t i) const noexcept {
        return inflight_gen_[i] == bind_gen_[i] && state_[i].seeded;
    }
    bool take(u64 serial) noexcept {
        if (!inflight_.valid || inflight_.serial != serial) {
            return false;
        }
        last_ = inflight_;
        inflight_ = Plan{};
        return true;
    }
    void reset(Field f) noexcept {
        state_[idx(f)] = FieldState{};
        if (inflight_.valid && inflight_.fields[idx(f)]) {
            inflight_.fields[idx(f)] = false;
        }
    }
    void poison(Field f) noexcept {
        reset(f);
        state_[idx(f)].poisoned = true;
        state_[idx(f)].poisoned_edge = edge_[idx(f)];
    }
    FieldState state_[field_count]{};
    u64 bind_gen_[field_count]{};
    u64 inflight_gen_[field_count]{};
    u64 edge_[field_count]{};
    bool visible_[model_count]{};
    Plan inflight_{};
    Plan last_{};
    u64 serial_{};
    Counters counters_{};
};

// ---------------------------------------------------------------------------------------------
// Fresh guard chain over a memory policy M with `bool read(uptr, void*, size_t) const`.

/// @brief Why `resolve` refused.
enum class Reject : unsigned {
    none,
    window_read,
    model_vt,
    token_image,
    context_token,
    parent_token,
    parent_override,
    parent_sg_window,
    idler_null,
    idler_vt,
    idler_slot,
    manager_idler,
    gui_null,
    gui_idler,
    view_null,
    view_vt,
    observer_vt,
    view_slots,
    view_gui,
    view_idler,
    root_null,
    parent_root,
    dirty_read,
};
/**
 * @brief Log name of a `Reject`.
 * @param r Reject.
 * @return Stable name.
 */
inline const char* reject_name(Reject r) noexcept {
    static constexpr const char* names[] = {"none",          "window-read",  "model-vt",        "token-image",
                                            "context-token", "parent-token", "parent-override", "parent-sg-window",
                                            "idler-null",    "idler-vt",     "idler-slot",      "manager-idler",
                                            "gui-null",      "gui-idler",    "view-null",       "view-vt",
                                            "observer-vt",   "view-slots",   "view-gui",        "view-idler",
                                            "root-null",     "parent-root",  "dirty-read"};
    const auto i = static_cast<unsigned>(r);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}
/// @brief Placeholder for an unread dirty byte (any value > 1 is rejected).
inline constexpr unsigned char unread_dirty = 0xFF;
/// @brief Stack-only result of a successful resolve.
struct Target {
    uptr gui{};             ///< GUI container.
    uptr view{};            ///< Politics view.
    unsigned char dirty{};  ///< Dirty byte (0 or 1) before the call.
};
/// @brief Bounded diagnostics of a resolve: observed/expected RVAs (0 = unreadable) and raw gui words.
struct ResolveDiag {
    Reject reject{Reject::none};  ///< Reject reason.
    uptr observed_rva{};          ///< Observed vtable/slot RVA.
    uptr expected_rva{};          ///< Expected RVA.
    uptr slot_rva{};              ///< Slot address RVA (slot rejects).
    uptr gui_word0{};             ///< Raw word 0 of the GUI container (data, not a vtable; R4).
    uptr gui_b8_rva{};            ///< `[gui+B8]` - base (shared allocator global; R5-NOTE-1).
};

/// @brief Stages of `resolve`; split only to keep each stage readable. Same reads, same order.
namespace detail {

/**
 * @brief Records a vtable reject in the diagnostics and returns it.
 * @param diag Optional diagnostics.
 * @param image Image base.
 * @param r Reject.
 * @param observed Observed vtable (0 = unreadable).
 * @param expected Expected vtable.
 * @return `r`.
 */
inline Reject vt_reject(ResolveDiag* diag, uptr image, Reject r, uptr observed, uptr expected) noexcept {
    if (diag) {
        diag->reject = r;
        diag->observed_rva = observed ? observed - image : 0;
        diag->expected_rva = expected - image;
    }
    return r;
}

/**
 * @brief Stage 1: own model, tokens, no parent overrides; reads the window's parent root.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param window Window.
 * @param diag Optional diagnostics.
 * @param parent Receives `[window+100]`.
 * @return `Reject::none` or the first failed check.
 */
template <class M>
Reject resolve_model(const M& mem, uptr image, uptr window, ResolveDiag* diag, uptr& parent) noexcept {
    uptr model = 0;
    uptr model_vt = 0;
    if (!rd(mem, window, off::window_model, model) || !rd(mem, model, 0, model_vt)) {
        return Reject::window_read;
    }
    if (model_vt != image + rva::vt_sg_model) {
        return vt_reject(diag, image, Reject::model_vt, model_vt, image + rva::vt_sg_model);
    }
    std::uint32_t politics_token = 0;
    std::uint32_t context_token = 0;
    std::uint32_t model_context = 0;
    std::uint32_t model_parent = 0;
    if (!rd(mem, image, rva::token_politics_tab, politics_token) ||
        !rd(mem, image, rva::token_player_context, context_token) || politics_token != token_politics_tab_value ||
        context_token != token_player_context_value) {
        return Reject::token_image;
    }
    if (!rd(mem, model, off::model_context_token, model_context) || model_context != context_token) {
        return Reject::context_token;
    }
    if (!rd(mem, model, off::model_parent_token, model_parent) || model_parent != politics_token) {
        return Reject::parent_token;
    }
#if defined(__APPLE__)
    if (!engine::macos::records::parent_names_empty(mem, model)) {
        return Reject::parent_override;
    }
#else
    u64 parent_window_len = 1;
    u64 parent_sg_len = 1;
    if (!rd(mem, model, off::model_parent_window_len, parent_window_len) ||
        !rd(mem, model, off::model_parent_sg_len, parent_sg_len) || parent_window_len != 0 || parent_sg_len != 0) {
        return Reject::parent_override;
    }
#endif
    uptr parent_sg = 1;
    if (!rd(mem, window, off::window_parent_sg, parent_sg) || parent_sg != 0) {
        return Reject::parent_sg_window;
    }
    if (!rd(mem, window, off::window_parent, parent)) {
        return Reject::window_read;
    }
    return Reject::none;
}

/**
 * @brief Stage 2: idler (vtable, getter slot) and the token-map owner's idler.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param window Window.
 * @param diag Optional diagnostics.
 * @param idler Receives the idler.
 * @return `Reject::none` or the first failed check.
 */
template <class M>
Reject resolve_idler(const M& mem, uptr image, uptr window, ResolveDiag* diag, uptr& idler) noexcept {
    uptr idler_vt = 0;
    uptr getter = 0;
    if (!rd(mem, window, off::window_idler, idler) || !idler) {
        return Reject::idler_null;
    }
    if (!rd(mem, idler, 0, idler_vt) || idler_vt != image + rva::vt_ingame_idler) {
        return vt_reject(diag, image, Reject::idler_vt, idler_vt, image + rva::vt_ingame_idler);
    }
    if (!rd(mem, idler_vt, off::slot_idler_gui, getter) || getter != image + rva::idler_gui_getter) {
        if (diag) {
            diag->reject = Reject::idler_slot;
            diag->observed_rva = getter ? getter - image : 0;
            diag->expected_rva = rva::idler_gui_getter;
            diag->slot_rva = rva::vt_ingame_idler + off::slot_idler_gui;
        }
        return Reject::idler_slot;
    }
    uptr manager = 0;
    uptr manager_idler = 0;
    if (!rd(mem, window, off::window_manager, manager) || !rd(mem, manager, off::manager_idler, manager_idler) ||
        manager_idler != idler) {
        return Reject::manager_idler;
    }
    return Reject::none;
}

/**
 * @brief Stage 3: GUI container from the idler (getter semantics read directly) and its back-pointer.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param idler Idler.
 * @param diag Optional diagnostics (word 0 and +B8 are copied for the log only).
 * @param gui Receives the GUI container.
 * @return `Reject::none` or the first failed check.
 */
template <class M>
Reject resolve_gui(const M& mem, uptr image, uptr idler, ResolveDiag* diag, uptr& gui) noexcept {
    // Fresh getter: DCCF30 is exactly `mov rax,[rcx+6B8]; ret` (fingerprinted), so the bounded
    // read below is its exact semantics without executing native code.
    uptr gui_word0 = 0;
    uptr gui_idler = 0;
    if (!rd(mem, idler, off::idler_gui, gui) || !gui) {
        return Reject::gui_null;
    }
    // No gui vtable gate: [idler+6B8] is a non-polymorphic container (R4, ctor B614C0); identity is
    // closed by gui+180 == idler and the politics-view checks below. Word 0 and +B8 are logged only.
    uptr gui_b8 = 0;
    if (diag) {
        if (rd(mem, gui, 0, gui_word0)) {
            diag->gui_word0 = gui_word0;
        }
        if (rd(mem, gui, gui_structural_offset, gui_b8)) {
            diag->gui_b8_rva = gui_b8 ? gui_b8 - image : 0;
        }
    }
    if (!rd(mem, gui, off::gui_idler, gui_idler) || gui_idler != idler) {
        return Reject::gui_idler;
    }
    return Reject::none;
}

/**
 * @brief Stage 4: politics view (vtables, slot pins, back-pointers, root == parent, dirty byte).
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param gui GUI container.
 * @param idler Idler.
 * @param parent Window parent root from stage 1.
 * @param diag Optional diagnostics.
 * @param out Receives gui, view and dirty on success.
 * @return `Reject::none` or the first failed check.
 */
template <class M>
Reject resolve_view(const M& mem, uptr image, uptr gui, uptr idler, uptr parent, ResolveDiag* diag,
                    Target& out) noexcept {
    uptr view = 0;
    uptr view_vt = 0;
    uptr observer_vt = 0;
    if (!rd(mem, gui, off::gui_views + (sizeof(uptr) * static_cast<uptr>(politics_view_index)), view) || !view) {
        return Reject::view_null;
    }
    if (!rd(mem, view, 0, view_vt) || view_vt != image + rva::vt_politics_view) {
        return vt_reject(diag, image, Reject::view_vt, view_vt, image + rva::vt_politics_view);
    }
    if (!rd(mem, view, off::view_observer, observer_vt) || observer_vt != image + rva::vt_politics_observer) {
        return vt_reject(diag, image, Reject::observer_vt, observer_vt, image + rva::vt_politics_observer);
    }
    for (const auto& pin : slot_pins) {
        if (pin.vtable == rva::vt_ingame_idler) {
            continue;  // checked above
        }
        uptr slot = 0;
        if (!rd(mem, image + pin.vtable, pin.slot, slot) || slot != image + pin.target) {
            if (diag) {
                diag->reject = Reject::view_slots;
                diag->observed_rva = slot ? slot - image : 0;
                diag->expected_rva = pin.target;
                diag->slot_rva = pin.vtable + pin.slot;
            }
            return Reject::view_slots;
        }
    }
    uptr view_gui = 0;
    uptr view_idler = 0;
    uptr root = 0;
    if (!rd(mem, view, off::view_gui, view_gui) || view_gui != gui) {
        return Reject::view_gui;
    }
    if (!rd(mem, view, off::view_idler, view_idler) || view_idler != idler) {
        return Reject::view_idler;
    }
    if (!rd(mem, view, off::view_root, root) || !root) {
        return Reject::root_null;
    }
    if (parent != root) {
        return Reject::parent_root;
    }
    unsigned char dirty = unread_dirty;
    if (!rd(mem, view, off::view_dirty, dirty) || dirty > 1) {
        return Reject::dirty_read;
    }
    out.gui = gui;
    out.view = view;
    out.dirty = dirty;
    return Reject::none;
}

}  // namespace detail

/**
 * @brief Resolves window -> idler -> GUI container -> politics view freshly and checks every link.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param window Ticking window.
 * @param out Receives gui, view and dirty on success.
 * @param diag Optional diagnostics.
 * @return `Reject::none` on success, otherwise the first failed check (stages run in order).
 */
template <class M>
Reject resolve(const M& mem, uptr image, uptr window, Target& out, ResolveDiag* diag = nullptr) noexcept {
    out = Target{};
    if (diag) {
        *diag = ResolveDiag{};
    }
    uptr parent = 0;
    uptr idler = 0;
    uptr gui = 0;
    if (const auto r = detail::resolve_model(mem, image, window, diag, parent); r != Reject::none) {
        return r;
    }
    if (const auto r = detail::resolve_idler(mem, image, window, diag, idler); r != Reject::none) {
        return r;
    }
    if (const auto r = detail::resolve_gui(mem, image, idler, diag, gui); r != Reject::none) {
        return r;
    }
    return detail::resolve_view(mem, image, gui, idler, parent, diag, out);
}

/// @brief Result of one refresh attempt.
enum class Outcome : unsigned {
    ok,
    precall_reject,
    call_fault,
    postcall_reject,
    postcall_identity,
    readback_dirty,
    readback_revision
};
/**
 * @brief Log name of an `Outcome`.
 * @param o Outcome.
 * @return Stable name.
 */
inline const char* outcome_name(Outcome o) noexcept {
    static constexpr const char* names[] = {"ok",
                                            "precall-reject",
                                            "call-fault",
                                            "postcall-reject",
                                            "postcall-identity",
                                            "readback-dirty",
                                            "readback-revision"};
    return names[static_cast<unsigned>(o)];
}

// ---------------------------------------------------------------------------------------------
// Own scripted window inspection (caption variant: no child-list checks).

/// @brief Classification of a window for the caption core.
enum class WindowKind { unrelated, invalid, hidden, visible };
/// @brief Copied inspection result.
struct OwnWindow {
    std::size_t model{model_count};  ///< Caption model index.
    uptr root{};                     ///< Root widget (current call only).
};

/**
 * @brief Inspects a window against the four caption models.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param window Window.
 * @param out Receives model and root.
 * @return Classification.
 */
template <class M>
WindowKind inspect(const M& mem, uptr image, uptr window, OwnWindow& out) noexcept {
    out = OwnWindow{};
    uptr model = 0;
    uptr model_vt = 0;
    if (!rd(mem, window, off::window_model, model) || !rd(mem, model, 0, model_vt) ||
        model_vt != image + rva::vt_sg_model) {
        return WindowKind::unrelated;
    }
    for (std::size_t i = 0; i < model_count; ++i) {
        if (string_equals(mem, model + off::model_name, model_specs[i].name)) {
            out.model = i;
            break;
        }
    }
    if (out.model == model_count) {
        return WindowKind::unrelated;
    }
    const uptr context = window + off::window_context;
    uptr ctx_vt = 0;
    uptr ctx_window = 0;
    uptr ctx_model = 0;
    uptr ctx_root = 0;
    uptr root = 0;
    uptr root_vt = 0;
    unsigned char flags = 0;
    unsigned char desired = 0;
    if (!rd(mem, context, 0, ctx_vt) || ctx_vt != image + rva::vt_sg_context ||
        !rd(mem, context, off::context_window, ctx_window) || ctx_window != window ||
        !rd(mem, context, off::context_model, ctx_model) || ctx_model != model ||
        !rd(mem, context, off::context_root, ctx_root) || !rd(mem, window, off::window_root, root) || !root ||
        ctx_root != root || !rd(mem, root, 0, root_vt) || root_vt != image + rva::vt_sg_root ||
        !rd(mem, root, off::root_flags, flags) || !rd(mem, window, off::window_desired, desired)) {
        return WindowKind::invalid;
    }
    out.root = root;
    return desired && core::flag::shown_not_hidden(flags) ? WindowKind::visible : WindowKind::hidden;
}

// ---------------------------------------------------------------------------------------------
/**
 * @brief Caption refresh service; all entry points on the UI thread after the original function.
 *
 * Env must provide (all noexcept): `const M& mem()`, `uptr image()`,
 * `bool carrier(uptr root, const char* name, char* out, size_t cap, size_t& size)` (fresh lookup),
 * `bool call_refresh(uptr gui, int index)` (SEH-guarded B6E210; false = fault),
 * `bool refresh_armed()` (operational switch) and `void log(const char*)`.
 * @tparam Env Environment adapter.
 */
template <class Env>
class Service {
public:
    /**
     * @brief Binds the service to its environment.
     * @param environment Adapter (outlives the service).
     */
    explicit Service(Env& environment) noexcept : env_(environment) {}

    /**
     * @brief Post-original Bind: full reset (or invalid reset) of the window's field.
     * @param window Window.
     */
    void on_bind(uptr window) noexcept {
        OwnWindow own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == WindowKind::unrelated) {
            return;
        }
        const auto f = model_specs[own.model].field;
        {
            const std::scoped_lock lock(mutex_);
            if (kind == WindowKind::invalid) {
                tracker_.invalid(f);
            } else {
                tracker_.bind(f);
            }
            invalid_[own.model] = kind == WindowKind::invalid;
            carrier_bad_[own.model] = false;
        }
        line("bind", own.model, kind == WindowKind::invalid ? "invalid-reset" : "baseline-reset", 0);
    }

    /**
     * @brief Post-original Tick: observation plus at most one refresh plan.
     * @param window Window.
     */
    void on_tick(uptr window) noexcept {
        OwnWindow own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == WindowKind::unrelated) {
            return;
        }
        const auto& spec = model_specs[own.model];
        if (kind == WindowKind::invalid) {
            bool transition = false;
            {
                const std::scoped_lock lock(mutex_);
                tracker_.visibility(own.model, false);
                transition = !invalid_[own.model];
                invalid_[own.model] = true;
                if (transition) {
                    tracker_.invalid(spec.field);  // edge-triggered, not per frame
                }
            }
            if (transition) {
                line("observe", own.model, "invalid-context-reset", 0);
            }
            return;
        }
        {
            const std::scoped_lock lock(mutex_);
            invalid_[own.model] = false;
            tracker_.visibility(own.model, kind == WindowKind::visible);  // hidden keeps seed/ack/pending
        }
        if (kind != WindowKind::visible) {
            return;
        }
        u64 rev = 0;
        if (!read_revision(own.root, spec.carrier, rev)) {
            carrier_rejected(own.model);
            return;
        }
        Tracker::Observe result{};
        {
            const std::scoped_lock lock(mutex_);
            carrier_bad_[own.model] = false;
            result = tracker_.observe(spec.field, rev);
            if (result == Tracker::Observe::lower_unconfirmed && tracker_.state(spec.field).lower_streak != 1) {
                result = Tracker::Observe::unchanged;  // log only the first lower read of a streak
            }
        }
        if (result == Tracker::Observe::lower_unconfirmed) {
            line("observe", own.model, "lower-revision-pending-kept", rev);
        }
        if (result == Tracker::Observe::rollback_confirmed) {
            line("observe", own.model, "rollback-confirmed-reset", rev);
        }
        service(window, own);
    }

    /**
     * @brief Bridge prefill succeeded: the only place a baseline is created.
     * @param window Editor window.
     * @return Seed outcome.
     */
    Tracker::Seed on_prefill(uptr window) noexcept {
        OwnWindow own{};
        u64 rev = 0;
        if (inspect(env_.mem(), env_.image(), window, own) != WindowKind::visible ||
            model_specs[own.model].role != Role::editor) {
            line("prefill", own.model, "not-visible-own-editor", 0);
            return Tracker::Seed::refused_bad;
        }
        const auto f = model_specs[own.model].field;
        // The bridge prefill runs before this Tick's on_tick: register the opening edge first.
        {
            const std::scoped_lock lock(mutex_);
            tracker_.visibility(own.model, true);
        }
        if (!read_revision(own.root, model_specs[own.model].carrier, rev)) {
            {
                const std::scoped_lock lock(mutex_);
                tracker_.seed(f, max_revision + 1);
            }  // counted refusal
            line("prefill", own.model, "seed-carrier-rejected-apply-blocked", 0);
            return Tracker::Seed::refused_bad;
        }
        Tracker::Seed result{};
        {
            const std::scoped_lock lock(mutex_);
            result = tracker_.seed(f, rev);
        }
        line("prefill", own.model, seed_name(result), rev);
        return result;
    }

    /**
     * @brief Log name of a prefill seed outcome.
     * @param result Seed outcome.
     * @return "seeded", "existing-seed-kept" or "seed-refused".
     */
    static const char* seed_name(Tracker::Seed result) noexcept {
        switch (result) {
            case Tracker::Seed::seeded:
                return "seeded";
            case Tracker::Seed::observed:
                return "existing-seed-kept";
            default:
                return "seed-refused";
        }
    }

    /// @brief Why an own Apply may (not) dispatch; the value is the negative sentinel epoch.
    enum class Verdict : unsigned { allowed = 0, stale = 1, no_counter = 2, cap_reached = 3 };
    /**
     * @brief Log name of a verdict.
     * @param v Verdict.
     * @return Stable name.
     */
    static const char* verdict_name(Verdict v) noexcept {
        switch (v) {
            case Verdict::allowed:
                return "apply-allowed";
            case Verdict::no_counter:
                return "apply-rejected-no-counter";
            case Verdict::cap_reached:
                return "apply-rejected-cap";
            default:
                return "apply-rejected-stale";
        }
    }
    /**
     * @brief Apply gate (R5-MINOR-1 distinguishes the cause).
     * @param window Editor window of the Apply.
     * @return Verdict; anything but `allowed` means no payload.
     */
    Verdict on_dispatch_verdict(uptr window) noexcept {
        OwnWindow own{};
        u64 rev = 0;
        const bool own_editor = inspect(env_.mem(), env_.image(), window, own) == WindowKind::visible &&
                                model_specs[own.model].role == Role::editor;
        const bool counter = own_editor && read_revision(own.root, model_specs[own.model].carrier, rev);
        Verdict verdict = Verdict::stale;
        {
            const std::scoped_lock lock(mutex_);
            if (!own_editor) {
                tracker_.count_dispatch_reject();
            } else if (!counter) {
                tracker_.count_dispatch_reject();
                verdict = Verdict::no_counter;
            } else if (rev >= max_revision) {
                tracker_.count_dispatch_reject();
                verdict = Verdict::cap_reached;
            } else if (tracker_.dispatch_allowed(model_specs[own.model].field, rev)) {
                verdict = Verdict::allowed;
            }
        }
        if (verdict != Verdict::allowed) {
            line("dispatch", own.model, verdict_name(verdict), rev);
        }
        return verdict;
    }
    /**
     * @brief Boolean form of the gate.
     * @param window Editor window.
     * @return `true` when allowed.
     */
    bool on_dispatch(uptr window) noexcept { return on_dispatch_verdict(window) == Verdict::allowed; }
    /**
     * @brief Direct gate query (tests).
     * @param f Field.
     * @param rev_now Counter.
     * @return Tracker verdict.
     */
    bool dispatch_allowed(Field f, u64 rev_now) noexcept {
        std::scoped_lock lock(mutex_);
        return tracker_.dispatch_allowed(f, rev_now);
    }
    /// @brief Copy of the tracker (tests, diagnostics). @return Snapshot.
    Tracker snapshot() noexcept {
        std::scoped_lock lock(mutex_);
        return tracker_;
    }

private:
    bool read_revision(uptr root, const char* name, u64& rev) noexcept {
        char text[revision_text_capacity]{};
        std::size_t size = 0;
        return env_.carrier(root, name, text, sizeof(text) - 1, size) && size < sizeof(text) &&
               parse_revision(text, size, rev);
    }
    void carrier_rejected(std::size_t model) noexcept {
        bool transition = false;
        {
            const std::scoped_lock lock(mutex_);
            transition = !carrier_bad_[model];
            carrier_bad_[model] = true;
        }
        if (transition) {
            line("observe", model, "revision-carrier-rejected", 0);  // once per ok->bad transition
        }
    }
    void service(uptr window, const OwnWindow& own) noexcept {
        Plan plan{};
        {
            const std::scoped_lock lock(mutex_);
            plan = tracker_.plan();
        }
        if (!plan.valid) {
            return;
        }
        if (!env_.refresh_armed()) {  // operational switch, not an authority proof
            {
                const std::scoped_lock lock(mutex_);
                tracker_.precall_rejected(plan.serial);
            }
            line("plan", own.model, "disarmed-no-call", plan.serial);
            return;
        }
        Target before{};
        ResolveDiag diag{};
        const auto pre = resolve(env_.mem(), env_.image(), window, before, &diag);
        if (pre != Reject::none) {
            {
                const std::scoped_lock lock(mutex_);
                tracker_.precall_rejected(plan.serial);
            }
            diag_line(own.model, reject_name(pre), plan.serial, diag);
            return;
        }
        bool first_success = false;
        {
            const std::scoped_lock lock(mutex_);
            tracker_.called(plan.serial);
            first_success = !resolved_once_;
            resolved_once_ = true;
        }
        if (first_success) {
            diag_line(own.model, "resolve-ok-first", plan.serial, diag);
        }
        // No lock is held across the native call. Exactly one call per plan.
        const bool returned = env_.call_refresh(before.gui, politics_view_index);
        Outcome outcome = Outcome::ok;
        Target after{};
        if (!returned) {
            outcome = Outcome::call_fault;
        } else if (resolve(env_.mem(), env_.image(), window, after) != Reject::none) {
            outcome = Outcome::postcall_reject;
        } else if (after.gui != before.gui || after.view != before.view) {
            outcome = Outcome::postcall_identity;
        } else if (after.dirty != 1) {
            outcome = Outcome::readback_dirty;
        } else {
            OwnWindow again{};
            u64 rev = 0;
            if (inspect(env_.mem(), env_.image(), window, again) != WindowKind::visible || again.model != own.model ||
                !read_revision(again.root, model_specs[own.model].carrier, rev) ||
                rev < plan.target[static_cast<std::size_t>(model_specs[own.model].field)]) {
                outcome = Outcome::readback_revision;
            }
        }
        {
            const std::scoped_lock lock(mutex_);
            tracker_.finished(plan.serial, outcome == Outcome::ok);
        }
        // dirty already 1 before the call: accepted (refresh is scheduled anyway) but labelled as
        // non-discriminating evidence of this call's effect.
        line("refresh", own.model,
             outcome == Outcome::ok && before.dirty == 1 ? "ok-already-dirty" : outcome_name(outcome), plan.serial);
    }
    void line(const char* event, std::size_t model, const char* detail, u64 value) noexcept {
        char text[log_line_capacity]{};
        static_cast<void>(std::snprintf(text, sizeof(text), "RN_CAPTION event=%s model=%s detail=%s value=%llu\r\n",
                                        event, model < model_count ? model_specs[model].name : "?", detail,
                                        static_cast<unsigned long long>(value)));
        env_.log(text);
    }
    void diag_line(std::size_t model, const char* detail, u64 serial, const ResolveDiag& diag) noexcept {
        char text[diag_line_capacity]{};
        static_cast<void>(std::snprintf(
            text, sizeof(text),
            "RN_CAPTION event=precall model=%s detail=%s serial=%llu observed_vt_rva=0x%llX expected_vt_rva=0x%llX "
            "slot_rva=0x%llX gui_word0=0x%llX gui_b8_rva=0x%llX gui_b8_expected=0x%llX\r\n",
            model < model_count ? model_specs[model].name : "?", detail, static_cast<unsigned long long>(serial),
            static_cast<unsigned long long>(diag.observed_rva), static_cast<unsigned long long>(diag.expected_rva),
            static_cast<unsigned long long>(diag.slot_rva), static_cast<unsigned long long>(diag.gui_word0),
            static_cast<unsigned long long>(diag.gui_b8_rva), static_cast<unsigned long long>(gui_structural_rva)));
        env_.log(text);
    }
    static constexpr std::size_t log_line_capacity = 256;
    static constexpr std::size_t diag_line_capacity = 400;
    Env& env_;
    std::mutex mutex_;
    Tracker tracker_;
    bool invalid_[model_count]{};
    bool carrier_bad_[model_count]{};
    bool resolved_once_{};
};

/**
 * @brief Hook body shape: forward to the original exactly once, first, with no lock; observe afterwards.
 * @tparam Original Original callable.
 * @tparam Observe Observer callable.
 * @tparam Args Argument types.
 * @param original Original function.
 * @param observe Observer.
 * @param active Whether observation is enabled.
 * @param args Arguments forwarded unchanged.
 */
template <class Original, class Observe, class... Args>
void forward_then_observe(Original original, Observe&& observe, bool active, Args... args) {
    original(args...);
    if (active) {
        std::forward<Observe>(observe)(args...);
    }
}

}  // namespace capie::caption
