// Focus release on politics-panel hide (PID49352 "Esc closes politics, our editor stays").
// When the politics root (our editor window's parent) goes visible -> not visible and one of
// OUR exact-named CEditBox fields holds keyboard focus, release it ONCE through the game's own
// focus switch 225E320(manager, nullptr). No script flag, epoch or draft is touched, no retry.
//
// Byte-proven chain (pinned EXE, see focus_fingerprints):
//   225E320 void(manager*, widget*): old=[manager+158]; if old: old->vslot C8(); [manager+158]=new;
//            if new: new->vslot C0(). The null branch is explicit code (test rbx,rbx; je).
//            OPEN: no direct native caller passes nullptr (3 callers, all non-null).
//   CEditBox VT 2B4A940 slot C8 -> 22F4770 (unfocus): clears edit+271 bit0, then tail-jumps
//            22FBEC0(edit+1A0) -> 225E3B0(manager): if [manager+230]: release, [manager+230]=0.
//            This is exactly what the game runs whenever focus moves away from an edit box.
//   manager = [image+3453230] (22FBEC0: mov rcx,[rip->3453230]).
//   Parent visibility as the scripted-window Tick reads it: [window+100] (B889AE), parent+A5
//            bit 4 (B889AE) and bit 3 (B88A5B).
#pragma once
#include "caption_refresh_core.hpp"

namespace rn::focus {
using rn::caption::u64;
using rn::caption::uptr;
using rn::caption::rd;

namespace rva {
inline constexpr uptr focus_switch = 0x225E320;
inline constexpr uptr edit_unfocus = 0x22F4770;
inline constexpr uptr manager_global = 0x3453230;
inline constexpr uptr vt_edit = 0x2B4A940, vt_text_buffer = 0x2B4ABD0, vt_edit_definition = 0x2B54050;
}  // namespace rva
namespace off {
inline constexpr uptr manager_focus = 0x158, manager_receiver = 0x230;
inline constexpr uptr edit_flags = 0x271, edit_receiver = 0x1A0, edit_definition = 0x60, definition_name = 0x28;
inline constexpr uptr edit_text = 0xB0, edit_size = 0xC0, edit_capacity = 0xC8, edit_buffer = 0x80;
inline constexpr uptr slot_focus = 0xC0, slot_unfocus = 0xC8;
inline constexpr uptr parent_flags = 0xA5;
inline constexpr uptr window_parent_seen = 0xB78;  // B88AFF: cmp cl,[rbx+B78] (diagnostic copy only)
}  // namespace off

inline constexpr rn::caption::Fingerprint focus_fingerprints[] = {
    {0x225E320, "48895c2408574883ec20488bf9488bda488b89580100004885c97409488b01ff90c800000048899f580100004885db740c488b03488bcbff90c0000000488b5c24304883c4205fc3",
     "focus switch: old->C8, [m+158]=new, new->C0 if non-null (whole function)"},
    {0x22F4770, "48895c2408574883ec200fb68171020000488bd924fe888171020000a8107415", "edit unfocus: clear +271 bit0"},
    {0x22F47F0, "488d8ba0010000488b5c24304883c4205fe9ba760000", "edit unfocus tail: 22FBEC0(edit+1A0)"},
    {0x22FBEC0, "488bd1488b0d66731501e9e124f6ff", "receiver release via manager global 3453230"},
    {0x225E3B0, "40534883ec204883b93002000000488bd97419e838fdfeff488bc8488b10ff523048c78330020000000000004883c4205bc3",
     "manager: release [m+230] and zero it (whole function)"},
    {0xB889AE, "488b83000100004885c07444f680a500000004743b", "tick: parent=[w+100]; parent+A5 bit4"},
    {0xB88A5B, "4885c00f84ae0000000fb688a5000000c0e90380e1010f8488000000", "tick: parent+A5 bit3 visible"},
    {0xB88AFF, "3a8b780b0000", "tick: cmp cl,[window+B78] parent-visible cache (diagnostic)"},
};
inline constexpr std::size_t focus_fingerprint_count = sizeof(focus_fingerprints) / sizeof(focus_fingerprints[0]);
inline constexpr rn::caption::SlotPin focus_slot_pins[] = {
    {rva::vt_edit, off::slot_unfocus, rva::edit_unfocus},
    {rva::vt_edit, off::slot_focus, 0x22F3FB0},
};

struct Editor { const char* model; const char* fields[2]; };
inline constexpr Editor editors[] = {
    {"rn_ip_leader_gui", {"rn_ip_leader_edit", nullptr}},
    {"rn_ip_party_neutrality_gui", {"rn_ip_party_neutrality_edit", "rn_ip_party_neutrality_long_edit"}},
    {"rn_ip_country_gui", {"rn_ip_country_name_edit", "rn_ip_country_adj_edit"}},
};
inline constexpr std::size_t editor_count = 3;

template <class M> bool edit_named(const M& mem, uptr image, uptr edit, const char* name) noexcept {
    uptr vt = 0, tb_vt = 0, definition = 0, definition_vt = 0;
    return rd(mem, edit, 0, vt) && vt == image + rva::vt_edit && rd(mem, edit, off::edit_buffer, tb_vt) &&
           tb_vt == image + rva::vt_text_buffer && rd(mem, edit, off::edit_definition, definition) &&
           rd(mem, definition, 0, definition_vt) && definition_vt == image + rva::vt_edit_definition &&
           rn::caption::string_equals(mem, definition + off::definition_name, name);
}
struct Text { std::uint32_t points[128]{}; u64 size{}; };
template <class M> bool edit_text(const M& mem, uptr edit, Text& out) noexcept {
    u64 size = 0, capacity = 0; uptr data = edit + off::edit_text;
    if (!rd(mem, edit, off::edit_size, size) || !rd(mem, edit, off::edit_capacity, capacity) || size > 128 ||
        size > capacity || capacity > 65536) return false;
    if (capacity > 3 && !rd(mem, edit, off::edit_text, data)) return false;
    out.size = size;
    return size == 0 || mem.read(data, out.points, static_cast<std::size_t>(size) * 4);
}
inline bool same(const Text& a, const Text& b) noexcept {
    return a.size == b.size && std::memcmp(a.points, b.points, static_cast<std::size_t>(a.size) * 4) == 0;
}

enum class Kind { unrelated, invalid, valid };
struct Own {
    std::size_t editor{editor_count}; uptr root{}; bool parent_visible{};
    unsigned char parent_flags{}, root_flags{}, desired{}, parent_seen{};  // copied diagnostics
};
template <class M> Kind inspect(const M& mem, uptr image, uptr window, Own& out) noexcept {
    out = Own{};
    uptr model = 0, model_vt = 0;
    if (!rd(mem, window, rn::caption::off::window_model, model) || !rd(mem, model, 0, model_vt) ||
        model_vt != image + rn::caption::rva::vt_sg_model) return Kind::unrelated;
    for (std::size_t i = 0; i < editor_count; ++i)
        if (rn::caption::string_equals(mem, model + rn::caption::off::model_name, editors[i].model)) { out.editor = i; break; }
    if (out.editor == editor_count) return Kind::unrelated;
    const uptr context = window + rn::caption::off::window_context;
    uptr ctx_vt = 0, ctx_window = 0, ctx_model = 0, root = 0, ctx_root = 0, root_vt = 0, parent = 0;
    if (!rd(mem, context, 0, ctx_vt) || ctx_vt != image + rn::caption::rva::vt_sg_context ||
        !rd(mem, context, rn::caption::off::context_window, ctx_window) || ctx_window != window ||
        !rd(mem, context, rn::caption::off::context_model, ctx_model) || ctx_model != model ||
        !rd(mem, context, rn::caption::off::context_root, ctx_root) || !rd(mem, window, rn::caption::off::window_root, root) ||
        !root || root != ctx_root || !rd(mem, root, 0, root_vt) || root_vt != image + rn::caption::rva::vt_sg_root ||
        !rd(mem, window, rn::caption::off::window_parent, parent)) return Kind::invalid;
    out.root = root;
    unsigned char flags = 0;
    out.parent_visible = parent && rd(mem, parent, off::parent_flags, flags) && (flags & 8) && !(flags & 4);
    out.parent_flags = flags;
    rd(mem, root, rn::caption::off::root_flags, out.root_flags);
    rd(mem, window, rn::caption::off::window_desired, out.desired);
    rd(mem, window, off::window_parent_seen, out.parent_seen);
    return Kind::valid;
}

// Env: const M& mem(); uptr image(); uptr find_edit(uptr root, const char* name);
//      bool call_focus_null(uptr manager);  // SEH-guarded 225E320(manager, nullptr)
//      bool armed(); void log(const char*);
template <class Env> class Release {
public:
    explicit Release(Env& environment) noexcept : env_(environment) {}
    struct Counters { u64 transitions{}, no_own_focus{}, calls{}, released{}, failed{}, text_changed{}; };
    void on_bind(uptr window) noexcept {
        Own own{};
        if (inspect(env_.mem(), env_.image(), window, own) == Kind::unrelated) return;
        std::lock_guard<std::mutex> lock(mutex_);
        known_[own.editor] = false;
    }
    void on_tick(uptr window) noexcept {
        if (!env_.armed()) return;
        Own own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == Kind::unrelated) return;
        bool transition = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (kind == Kind::invalid) { known_[own.editor] = false; return; }
            transition = known_[own.editor] && parent_visible_[own.editor] && !own.parent_visible;
            known_[own.editor] = true; parent_visible_[own.editor] = own.parent_visible;
            if (transition) ++counters_.transitions;
        }
        if (!transition) return;  // exactly once per visible->hidden parent transition
        release(own);
    }
    Counters counters() noexcept { std::lock_guard<std::mutex> lock(mutex_); return counters_; }

private:
    void release(const Own& own) noexcept {
        const auto& mem = env_.mem(); const auto image = env_.image();
        uptr manager = 0, focused = 0, receiver = 0;
        const bool manager_ok = rd(mem, image, rva::manager_global, manager) && manager &&
                                rd(mem, manager, off::manager_focus, focused) && rd(mem, manager, off::manager_receiver, receiver);
        uptr edit = 0; const char* field = nullptr;
        for (const char* name : editors[own.editor].fields) {
            if (!name || !manager_ok) continue;
            const uptr candidate = env_.find_edit(own.root, name);
            unsigned char flags = 0;
            if (candidate && candidate == focused && edit_named(mem, image, candidate, name) &&
                receiver == candidate + off::edit_receiver && rd(mem, candidate, off::edit_flags, flags) && (flags & 1)) {
                edit = candidate; field = name; break;
            }
        }
        char line[256]{};
        if (!edit) {
            { std::lock_guard<std::mutex> lock(mutex_); ++counters_.no_own_focus; }
            std::snprintf(line, sizeof(line), "RN_FOCUS event=parent-hidden model=%s parent_flags=0x%02X own_focus=0 root_flags=0x%02X desired=%u parent_seen=%u; no call\r\n",
                          editors[own.editor].model, own.parent_flags, own.root_flags, own.desired, own.parent_seen);
            env_.log(line);
            return;
        }
        Text before{}, after{};
        const bool text_before = edit_text(mem, edit, before);
        { std::lock_guard<std::mutex> lock(mutex_); ++counters_.calls; }
        const bool returned = env_.call_focus_null(manager);  // no lock held; exactly one call
        uptr focus_after = 1, receiver_after = 1; unsigned char flags_after = 1;
        const bool released = returned && rd(mem, manager, off::manager_focus, focus_after) && focus_after != edit &&
                              rd(mem, manager, off::manager_receiver, receiver_after) && receiver_after != edit + off::edit_receiver &&
                              rd(mem, edit, off::edit_flags, flags_after) && !(flags_after & 1);
        const bool text_same = text_before && edit_text(mem, edit, after) && same(before, after);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (released) ++counters_.released; else ++counters_.failed;
            if (!text_same) ++counters_.text_changed;
        }
        std::snprintf(line, sizeof(line), "RN_FOCUS event=parent-hidden model=%s field=%s parent_flags=0x%02X result=%s draft=%s root_flags=0x%02X desired=%u parent_seen=%u; no retry\r\n",
                      editors[own.editor].model, field, own.parent_flags, released ? "released" : "release-unverified",
                      text_same ? "unchanged" : "CHANGED-OR-UNREADABLE", own.root_flags, own.desired, own.parent_seen);
        env_.log(line);
    }
    Env& env_;
    std::mutex mutex_;
    bool known_[editor_count]{}, parent_visible_[editor_count]{};
    Counters counters_{};
};
}  // namespace rn::focus
