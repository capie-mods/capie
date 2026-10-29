/**
 * @file focus_release.hpp
 * @brief Releases keyboard focus from our edit box when the politics panel is hidden.
 *
 * Problem it fixes (PID49352): Esc closed the politics panel but our edit box kept
 * keyboard focus, so later key presses went into a hidden field. When the politics root
 * (our editor window's parent) goes from visible to not visible and one of OUR exact-named
 * edit boxes holds focus, this module calls the game's own focus switch
 * `22BD430(manager, nullptr)` once. That runs the edit box's normal unfocus path
 * (`2371D90` -> `2379EC0` -> `22BD4C0`), the same code the game runs whenever focus moves
 * away from an edit box.
 *
 * What it does not do: no script flag, epoch or draft write, no retry, no focus *set*,
 * no Escape handling. The draft text is compared before and after the call and logged.
 * Every editor-root visible -> hidden transition is also logged (`RN_HIDE`) with its
 * source classified from copied bits, so runtime logs show whether hides other than a
 * panel close exist.
 *
 * Evidence (pinned EXE, `focus_signatures`): 22BD430 stores the new focus at
 * manager+138 and calls old->slot C8 / new->slot C0; the null branch is explicit code.
 * OPEN: no native caller passes nullptr (three callers, all non-null).
 *
 * Threading: UI thread; the mutex guards only scalar state and is never held during the
 * game call. The template parameter `Env` lets tests run the same code on fake memory.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../core/signature_table.hpp"
#include "../../engine/msvc_string.hpp"
#include "../editor/schema.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace capie::focus {

using core::rd;
using core::u64;
using core::uptr;
namespace rva = core::rva;
namespace off = core::off;
namespace flag = core::flag;

/// @brief Focus fingerprints (alias of the central table).
inline constexpr const auto& focus_fingerprints = core::focus_signatures;
/// @brief Number of focus fingerprints.
inline constexpr std::size_t focus_fingerprint_count = core::focus_signature_count;
/// @brief CEditBox slot pins (alias of the central table).
inline constexpr const auto& focus_slot_pins = core::focus_slot_pins;

/// @brief One editor model and its field names.
struct Editor {
    const char* model;      ///< Model name.
    const char* fields[2];  ///< Edit names (second may be null).
};
/// @brief The three editors.
/// @note Derived from the shared editor schema so the names exist in one place only.
inline constexpr Editor editors[] = {
    {editor::models[editor::model_index::leader].name, {editor::fields[editor::field_index::leader].edit, nullptr}},
    {editor::models[editor::model_index::party].name,
     {editor::fields[editor::field_index::party_short].edit, editor::fields[editor::field_index::party_long].edit}},
    {editor::models[editor::model_index::country].name,
     {editor::fields[editor::field_index::country_name].edit,
      editor::fields[editor::field_index::country_adjective].edit}},
};
/// @brief Number of editors.
inline constexpr std::size_t editor_count = sizeof(editors) / sizeof(editors[0]);
static_assert(editor_count == editor::model_count);
/// @brief Largest edit text compared before/after (code points).
inline constexpr u64 max_text_points = 128;
/// @brief Sanity limit on an edit capacity.
inline constexpr u64 max_text_capacity = 65536;
/// @brief Bytes per UTF-32 code point.
inline constexpr std::size_t utf32_bytes = 4;
/// @brief Size of one log line buffer.
inline constexpr std::size_t line_capacity = 256;

/**
 * @brief Identity of one of our edit boxes (three vtables + definition name).
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param edit Candidate.
 * @param name Exact name.
 * @return `true` on a match.
 */
template <class M>
bool edit_named(const M& mem, uptr image, uptr edit, const char* name) noexcept {
    uptr vt = 0;
    uptr tb_vt = 0;
    uptr definition = 0;
    uptr definition_vt = 0;
    return rd(mem, edit, 0, vt) && vt == image + rva::vt_edit && rd(mem, edit, off::edit_buffer, tb_vt) &&
           tb_vt == image + rva::vt_text_buffer && rd(mem, edit, off::edit_definition, definition) &&
           rd(mem, definition, 0, definition_vt) && definition_vt == image + rva::vt_edit_definition &&
           engine::string_equals(mem, definition + off::definition_name, name);
}
/// @brief Copied edit text.
struct Text {
    std::uint32_t points[max_text_points]{};  ///< Code points.
    u64 size{};                               ///< Length.
};
/**
 * @brief Copies an edit box's text.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param edit Edit box.
 * @param out Receives the text.
 * @return `false` when unreadable or oversized.
 */
template <class M>
bool edit_text(const M& mem, uptr edit, Text& out) noexcept {
    u64 size = 0;
    u64 capacity = 0;
    uptr data = edit + off::edit_text;
    if (!rd(mem, edit, off::edit_size, size) || !rd(mem, edit, off::edit_capacity, capacity) ||
        size > max_text_points || size > capacity || capacity > max_text_capacity) {
        return false;
    }
    if (capacity > core::value::inline_utf32_capacity && !rd(mem, edit, off::edit_text, data)) {
        return false;
    }
    out.size = size;
    return size == 0 || mem.read(data, out.points, static_cast<std::size_t>(size) * utf32_bytes);
}
/**
 * @brief Compares two texts.
 * @param a First.
 * @param b Second.
 * @return `true` when equal.
 */
inline bool same(const Text& a, const Text& b) noexcept {
    return a.size == b.size && std::memcmp(a.points, b.points, static_cast<std::size_t>(a.size) * utf32_bytes) == 0;
}

/// @brief Classification of a window for the focus release.
enum class Kind { unrelated, invalid, valid };
/// @brief Copied inspection result (diagnostic copies included).
struct Own {
    std::size_t editor{editor_count};  ///< Editor index.
    uptr root{};                       ///< Root (current call only).
    bool parent_visible{};             ///< Politics root shown and not hidden.
    unsigned char parent_flags{};      ///< Parent flag byte copy.
    unsigned char root_flags{};        ///< Own root flag byte copy.
    unsigned char desired{};           ///< Visible-trigger byte copy.
    unsigned char parent_seen{};       ///< Parent-visible cache byte copy.
    bool own_visible{};                ///< Own root visible.
    bool parent_present{};             ///< Parent pointer non-null.
};
/**
 * @brief Inspects a window against the three editors.
 * @tparam M Memory policy.
 * @param mem Memory policy.
 * @param image Image base.
 * @param window Window.
 * @param out Receives the copies.
 * @return Classification.
 */
template <class M>
Kind inspect(const M& mem, uptr image, uptr window, Own& out) noexcept {
    out = Own{};
    uptr model = 0;
    uptr model_vt = 0;
    if (!rd(mem, window, off::window_model, model) || !rd(mem, model, 0, model_vt) ||
        model_vt != image + rva::vt_sg_model) {
        return Kind::unrelated;
    }
    for (std::size_t i = 0; i < editor_count; ++i) {
        if (engine::string_equals(mem, model + off::model_name, editors[i].model)) {
            out.editor = i;
            break;
        }
    }
    if (out.editor == editor_count) {
        return Kind::unrelated;
    }
    const uptr context = window + off::window_context;
    uptr ctx_vt = 0;
    uptr ctx_window = 0;
    uptr ctx_model = 0;
    uptr root = 0;
    uptr ctx_root = 0;
    uptr root_vt = 0;
    uptr parent = 0;
    if (!rd(mem, context, 0, ctx_vt) || ctx_vt != image + rva::vt_sg_context ||
        !rd(mem, context, off::context_window, ctx_window) || ctx_window != window ||
        !rd(mem, context, off::context_model, ctx_model) || ctx_model != model ||
        !rd(mem, context, off::context_root, ctx_root) || !rd(mem, window, off::window_root, root) || !root ||
        root != ctx_root || !rd(mem, root, 0, root_vt) || root_vt != image + rva::vt_sg_root ||
        !rd(mem, window, off::window_parent, parent) || parent == root) {
        return Kind::invalid;  // also a root equal to the parent: the window factory's failure result
    }
    out.root = root;
    unsigned char flags = 0;
    out.parent_present = parent != 0;
    // Same hidden criterion as the caption core's own-root check: shown bit, no hidden bit.
    out.parent_visible = parent && rd(mem, parent, off::parent_flags, flags) && flag::shown_not_hidden(flags);
    out.parent_flags = flags;
    rd(mem, root, off::root_flags, out.root_flags);
    rd(mem, window, off::window_desired, out.desired);
    rd(mem, window, off::window_parent_seen, out.parent_seen);
    out.own_visible = out.desired && flag::shown_not_hidden(out.root_flags);
    return Kind::valid;
}

/**
 * @brief Focus release service.
 *
 * Env: `const M& mem()`, `uptr image()`, `uptr find_edit(uptr root, const char* name)`,
 * `bool call_focus_null(uptr manager)` (SEH-guarded 22BD430(manager, nullptr)),
 * `bool armed()`, `void log(const char*)`.
 * @tparam Env Environment adapter.
 */
template <class Env>
class Release {
public:
    /**
     * @brief Binds the service to its environment.
     * @param environment Adapter (outlives the service).
     */
    explicit Release(Env& environment) noexcept : env_(environment) {}
    /// @brief Counters for tests.
    struct Counters {
        u64 transitions{};   ///< Parent visible -> hidden transitions.
        u64 no_own_focus{};  ///< Transitions without our focus.
        u64 calls{};         ///< Focus-switch calls.
        u64 released{};      ///< Verified releases.
        u64 failed{};        ///< Unverified releases.
        u64 text_changed{};  ///< Draft changed or unreadable across the call.
        u64 root_hides{};    ///< Own root visible -> hidden transitions.
    };
    /**
     * @brief Classifies an editor-root hide from copied bits only.
     * @param own Inspection copy.
     * @return Source name.
     */
    static const char* hide_source(const Own& own) noexcept {
        if (!own.parent_present) {
            return "parent-detached";
        }
        if (!own.parent_visible) {
            return "parent-hidden";  // politics panel closed/hidden
        }
        if (!own.desired) {
            return "own-trigger-false";  // scripted visible trigger (e.g. flag cleared)
        }
        return "own-root-flags";  // own root hidden with parent shown
    }
    /**
     * @brief Post-original Bind: forget the editor's previous visibility.
     * @param window Window.
     */
    void on_bind(uptr window) noexcept {
        Own own{};
        if (inspect(env_.mem(), env_.image(), window, own) == Kind::unrelated) {
            return;
        }
        const std::scoped_lock lock(mutex_);
        known_[own.editor] = false;
    }
    /**
     * @brief Post-original Tick: log root hides, release focus once per parent hide.
     * @param window Window.
     */
    void on_tick(uptr window) noexcept {
        if (!env_.armed()) {
            return;
        }
        Own own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == Kind::unrelated) {
            return;
        }
        bool transition = false;
        bool root_hide = false;
        {
            const std::scoped_lock lock(mutex_);
            if (kind == Kind::invalid) {
                known_[own.editor] = false;
                return;
            }
            transition = known_[own.editor] && parent_visible_[own.editor] && !own.parent_visible;
            root_hide = known_[own.editor] && own_visible_[own.editor] && !own.own_visible;
            known_[own.editor] = true;
            parent_visible_[own.editor] = own.parent_visible;
            own_visible_[own.editor] = own.own_visible;
            if (transition) {
                ++counters_.transitions;
            }
            if (root_hide) {
                ++counters_.root_hides;
            }
        }
        if (root_hide) {
            char line[line_capacity]{};
            static_cast<void>(std::snprintf(
                line, sizeof(line),
                "RN_HIDE event=editor-root-hidden model=%s source=%s desired=%u root_flags=0x%02X parent_flags=0x%02X "
                "parent_seen=%u parent_present=%u; draft is dropped on reopen (re-prefill)\r\n",
                editors[own.editor].model, hide_source(own), own.desired, own.root_flags, own.parent_flags,
                own.parent_seen, own.parent_present ? 1U : 0U));
            env_.log(line);
        }
        if (!transition) {
            return;  // exactly once per visible->hidden parent transition
        }
        release(own);
    }
    /// @brief Counter copy. @return Counters.
    Counters counters() noexcept {
        std::scoped_lock lock(mutex_);
        return counters_;
    }

private:
    void release(const Own& own) noexcept {
        const auto& mem = env_.mem();
        const auto image = env_.image();
        uptr manager = 0;
        uptr focused = 0;
        uptr receiver = 0;
        const bool manager_ok = rd(mem, image, rva::manager_global, manager) && manager &&
                                rd(mem, manager, off::manager_focus, focused) &&
                                rd(mem, manager, off::manager_receiver, receiver);
        uptr edit = 0;
        const char* field = nullptr;
        for (const char* name : editors[own.editor].fields) {
            if (!name || !manager_ok) {
                continue;
            }
            const uptr candidate = env_.find_edit(own.root, name);
            unsigned char flags = 0;
            if (candidate && candidate == focused && edit_named(mem, image, candidate, name) &&
                receiver == candidate + off::edit_receiver && rd(mem, candidate, off::edit_flags, flags) &&
                (flags & flag::edit_active)) {
                edit = candidate;
                field = name;
                break;
            }
        }
        char line[line_capacity]{};
        if (!edit) {
            {
                const std::scoped_lock lock(mutex_);
                ++counters_.no_own_focus;
            }
            static_cast<void>(std::snprintf(
                line, sizeof(line),
                "RN_FOCUS event=parent-hidden model=%s parent_flags=0x%02X own_focus=0 root_flags=0x%02X desired=%u parent_seen=%u; no call\r\n",
                editors[own.editor].model, own.parent_flags, own.root_flags, own.desired, own.parent_seen));
            env_.log(line);
            return;
        }
        Text before{};
        Text after{};
        const bool text_before = edit_text(mem, edit, before);
        {
            const std::scoped_lock lock(mutex_);
            ++counters_.calls;
        }
        const bool returned = env_.call_focus_null(manager);  // no lock held; exactly one call
        uptr focus_after = 1;
        uptr receiver_after = 1;
        unsigned char flags_after = 1;
        const bool released = returned && rd(mem, manager, off::manager_focus, focus_after) && focus_after != edit &&
                              rd(mem, manager, off::manager_receiver, receiver_after) &&
                              receiver_after != edit + off::edit_receiver &&
                              rd(mem, edit, off::edit_flags, flags_after) && !(flags_after & flag::edit_active);
        const bool text_same = text_before && edit_text(mem, edit, after) && same(before, after);
        {
            const std::scoped_lock lock(mutex_);
            if (released) {
                ++counters_.released;
            } else {
                ++counters_.failed;
            }
            if (!text_same) {
                ++counters_.text_changed;
            }
        }
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_FOCUS event=parent-hidden model=%s field=%s parent_flags=0x%02X result=%s draft=%s root_flags=0x%02X desired=%u parent_seen=%u; no retry\r\n",
            editors[own.editor].model, field, own.parent_flags, released ? "released" : "release-unverified",
            text_same ? "unchanged" : "CHANGED-OR-UNREADABLE", own.root_flags, own.desired, own.parent_seen));
        env_.log(line);
    }
    Env& env_;
    std::mutex mutex_;
    bool known_[editor_count]{};
    bool parent_visible_[editor_count]{};
    bool own_visible_[editor_count]{};
    Counters counters_{};
};

}  // namespace capie::focus
