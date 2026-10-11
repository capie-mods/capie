// Caption refresh candidate core. INACTIVE: no hook is installed by the shipped
// build. Pure scalar state + a templated fresh guard chain over a bounded memory
// policy, so the identical code runs in-process and in the offline fake-memory tests.
//
// Exact target: hoi4.exe SHA256 7DC947BE...F8F06AA8B348F3A031D5, preferred base
// 0x140000000. Every RVA below is pinned by a full .text-unique fingerprint (see
// `fingerprints`) or by an exact image constant (vtable slot / token DWORD).
// Anything that is not proven is named OPEN and is enforced as a refusal, never
// as a guessed offset.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace rn::caption {
using u64 = std::uint64_t;
using uptr = std::uintptr_t;

inline constexpr char exe_sha256[] = "7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5";

namespace rva {
// Called natively (only one): void B6E210(CGameGui* rcx, signed int view_index edx).
inline constexpr uptr request_view_refresh = 0xB6E210;
// Leaves used by B6E210 / frame loop; pinned, never called by us.
inline constexpr uptr observer_mark_dirty = 0x1236950;   // mov byte [rcx+14],1 ; ret
inline constexpr uptr observer_consume = 0x1236980;      // call [vt+48] ; mov byte [rbx+14],0
inline constexpr uptr idler_gui_getter = 0xDCCF30;       // mov rax,[rcx+6B8] ; ret
inline constexpr uptr view_is_visible = 0x1237520;       // politics view primary slot 0x28
inline constexpr uptr view_get_root = 0x12374F0;         // mov rax,[rcx+578] ; ret
inline constexpr uptr observer_frame = 0x1582820;        // observer slot 0x38
inline constexpr uptr observer_refresh = 0x15863F0;      // observer slot 0x48 (leader+party writers)
// Hook targets (only compiled with RN_CAPTION_REFRESH_ARM_HOOKS; not in shipped build).
inline constexpr uptr window_tick = 0xB88950;
inline constexpr uptr window_bind = 0xB86320;
inline constexpr uptr find_instant = 0x22BC600;
// Image data (.rdata, constant in file).
inline constexpr uptr vt_politics_view = 0x29D9648;
inline constexpr uptr vt_politics_observer = 0x29D96F0;
inline constexpr uptr vt_ingame_idler = 0x2968FF0;
inline constexpr uptr vt_game_gui = 0x294A688;
inline constexpr uptr vt_sg_model = 0x29DB558;      // runtime-proven by render bridge QA
inline constexpr uptr vt_sg_context = 0x294E568;    // runtime-proven by render bridge QA
inline constexpr uptr vt_sg_root = 0x2B45278;       // runtime-proven by render bridge QA
inline constexpr uptr vt_instant_text = 0x2B468C8;  // runtime-proven by render bridge QA
inline constexpr uptr token_politics_tab = 0x294E4CC;    // DWORD 0x3AB2
inline constexpr uptr token_player_context = 0x2AECB3C;  // DWORD 0x3A70
}  // namespace rva

namespace off {
// ScriptedWindow (B86320 / B88950 raw-proven unless noted)
inline constexpr uptr window_idler = 0xE0;       // B88BEE: mov rcx,[rbx+E0]; call [rax+B8]
inline constexpr uptr window_model = 0xE8;
inline constexpr uptr window_root = 0xF8;        // render bridge (runtime-proven)
// window+F0 is the token-map owner read by Bind (B86350: r11=[window+F0], map fields at
// +78/+88/+A0). B8A1C0 inserts the politics_tab root into the map at this+70 and gets the
// GameGui from [this+18] vslot B8. Treating window+F0 as that same manager is an
// inference from the map layout (OPEN-identity); it is used only as an equality guard
// ([manager+18] == window idler), so a wrong inference can only refuse.
inline constexpr uptr window_manager = 0xF0;
inline constexpr uptr manager_idler = 0x18;
inline constexpr uptr window_parent = 0x100;     // B863F9: mov [rdi+100],rax
inline constexpr uptr window_parent_sg = 0x108;  // B8649E: mov qword [rdi+108],0
inline constexpr uptr window_context = 0x110;    // render bridge (runtime-proven)
inline constexpr uptr window_desired = 0xB79;    // render bridge (runtime-proven)
// ScriptedGui model
inline constexpr uptr model_name = 0x8;               // MsvcString, render bridge
inline constexpr uptr model_context_token = 0x28;     // B88B27: cmp dword [rax+28],3A70
inline constexpr uptr model_parent_token = 0x2C;      // B86346 + 159F33D
inline constexpr uptr model_parent_window_len = 0x60; // B864F1: [model+50].size at +60
inline constexpr uptr model_parent_sg_len = 0x80;     // B86414: cmp qword [r13+80],0
// Context (render bridge, runtime-proven)
inline constexpr uptr context_window = 0x10, context_model = 0x20, context_root = 0x28;
inline constexpr uptr root_flags = 0xA5, root_instant_list = 0x11E0;
// Idler / GameGui / politics view
inline constexpr uptr idler_gui = 0x6B8;      // DCCF30 body
inline constexpr uptr gui_idler = 0x180;      // B62D55: rdx=[gui+180] -> view ctor param_2
inline constexpr uptr gui_views = 0xC8;       // B6E225
inline constexpr uptr view_observer = 0x30;   // B6E242
inline constexpr uptr view_gui = 0x38;        // 12371DB rdx=param_3 -> 12362A0 [rcx+8]
inline constexpr uptr view_idler = 0x570;     // 1237358: mov [r15+570],r14 (param_2)
inline constexpr uptr view_root = 0x578;      // 12374F0
inline constexpr uptr view_dirty = 0x44;      // observer(+30) +14 : 1236950 / 1236980 / B68C68
inline constexpr uptr slot_view_visible = 0x28, slot_observer_frame = 0x38, slot_observer_refresh = 0x48;
inline constexpr uptr slot_idler_gui = 0xB8;
}  // namespace off

inline constexpr std::uint32_t token_politics_tab_value = 0x3AB2;
inline constexpr std::uint32_t token_player_context_value = 0x3A70;
inline constexpr int politics_view_index = 12;  // B62D69 stores politics view at gui+128; (128-C8)/8
inline constexpr int gui_view_limit = 0x17;     // B6E214 cmp edx,17

struct Fingerprint { uptr rva; const char* hex; const char* proves; };
// Every entry starts and ends on an instruction boundary (whole instructions only) and has
// exactly one match in .text, at the stated RVA, of the pinned EXE (checked by the test from
// the EXE file). Byte runs avoid DIR64 relocations (independent review), so they also match
// the ASLR-relocated in-memory image.
inline constexpr Fingerprint fingerprints[] = {
    {0xDCCF30, "488b81b8060000c3", "idler slot B8 getter returns [idler+6B8]"},
    {0xB6E210, "4883ec2883fa1774404863c248895c2420488d1cc1488b8bc80000004885c97423488b01ff502884c07419488b8bc80000004883c130488b5c24204883c428e9fc866c00488b5c24204883c428c3",
     "void(gui,int) index!=23, gui+C8+8i, view slot28, tail 1236950(view+30)"},
    {0x1236950, "c6411401c3", "observer+14=1 (view+44 dirty)"},
    {0x1236980, "40534883ec20488b01488bd9ff5048c64314004883c4205bc3", "consume: observer slot48 then +14=0"},
    {0x1237520, "40534883ec20488bd9488b89780500004885c97426488b01ff904802000084c07519488b83780500000fb680a5000000c0e80324014883c4205bc3",
     "politics view visibility (slot28) reads view+578"},
    {0x12374F0, "488b8178050000c3", "view root = [view+578]"},
    {0x12362A0, "40534883ec20488d0553db760148895108488901488bd944894110c6411400e85c050000", "observer bind: [obs+8]=rdx, [obs+14]=0"},
    {0x1237180, "48895c2410488974241848897c242048894c24085541564157488d6c24d04881ec30010000498bf1498bf84c8bf24c8bf9",
     "view base ctor prologue: rsi=param_4, rdi=param_3, r14=param_2, r15=this"},
    {0x12371DB, "448b4570488bd7498d4f30e8b5f0ffff90", "observer bind(view+30, param_3=gui)"},
    {0x1237358, "4d89b77005000049899f78050000", "view+570=param_2, view+578 init"},
    {0xB62D52, "4c8bc6488b9680010000488bc8e89c98a100eb03498bc748898628010000", "politics ctor(gui+180, gui); gui+128=view"},
    {0xB68C52, "488d9fc80000000f1f8000000000488b0b4885c9740f4438714474094883c130e809dd6c004883c3084883ed0175df",
     "frame loop consumes view+44 via 1236980(view+30)"},
    {0xB88BEE, "488b8be0000000488b01ff90b8000000488bc8", "tick: window+E0 idler, vslot B8 -> gui"},
    {0xB88B27, "488b83e8000000400fb6ff817828703a0000", "model+28 == 3A70 player_context"},
    {0xB86346, "488db1e80000004c8b2e418b452c85c0", "bind: model=[window+E8], model+2C token"},
    {0xB86350, "418b452c85c00f8eb80000004c8b99f0000000", "bind: eax=[model+2C]; r11=[window+F0] (token map owner)"},
    {0xB8A1C0, "48895c24185556574154415541564157488d6c24d94881ecc00000000f29b424b0000000488bf1488b4918488b01ff90b80000004c8bf0488d4e70e8f0490000",
     "manager: rsi=this, [this+18] vslot B8 -> gui (r14), map at this+70"},
    {0xB86414, "4983bd80000000000f84cf000000", "bind: parent_scripted_gui length model+80"},
    {0xB863F9, "493bd07406488b4218eb0233c048898700010000", "bind: window+100 = token-map entry+18"},
    {0xB8649E, "488db7e800000048c7870801000000000000", "bind: window+108 = 0"},
    {0xB864F1, "4883bf00010000000f858a000000488b164883c25048837a1000747c", "bind: parent_window_window length model+60"},
    {0xB8A660, "ba0c000000498bcee883cefdff488bc8e87bce6a00488bd8c74567b23a00004c8d4567488d5597488d4e70e8709dffff488b0848895918",
     "token 3AB2 -> GetRoot(GetView(gui,12))"},
    {0xF86CC, "8b15fa5d8502488b8c24d0af0100e8713605004c8d05da079f028b10488d0db98d3203e87cc43c02", "politics_tab literal <- DWORD 294E4CC"},
    {0xF78FE, "8b15e4519f02488b8c24e0ad0100e83f4405004c8d05f80b9f028b10488d0d178a3203e84ad23c02", "parent_window_token literal <- DWORD 2AECAE8 (=3A6E)"},
    {0x159F33D, "8b86c00000004189452ce9d6090000", "parser copies token to model+2C"},
};
inline constexpr std::size_t fingerprint_count = sizeof(fingerprints) / sizeof(fingerprints[0]);

struct SlotPin { uptr vtable; uptr slot; uptr target; };
inline constexpr SlotPin slot_pins[] = {
    {rva::vt_politics_view, off::slot_view_visible, rva::view_is_visible},
    {rva::vt_politics_observer, off::slot_observer_frame, rva::observer_frame},
    {rva::vt_politics_observer, off::slot_observer_refresh, rva::observer_refresh},
    {rva::vt_ingame_idler, off::slot_idler_gui, rva::idler_gui_getter},
};

inline int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Decodes a fingerprint into `out` (capacity cap). Returns byte count or 0 on error.
inline std::size_t decode_hex(const char* hex, unsigned char* out, std::size_t cap) noexcept {
    const auto length = std::strlen(hex);
    if (length == 0 || length % 2 || length / 2 > cap) return 0;
    for (std::size_t i = 0; i < length / 2; ++i) {
        const int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = static_cast<unsigned char>(hi * 16 + lo);
    }
    return length / 2;
}


// ---------------------------------------------------------------------------
// Revision carriers (script side is OPEN: names/fixture not yet implemented).
// The revision is an ordinary success counter (rn_ip_lui_rev / rn_ip_pui_rev) shown by an
// instant text carrier. HOI4 variables are fixed-point (int32 scaled by 1000, ~2147483 max;
// exact range OPEN) and `[?var]` formatting of values >= 1000 (separators/suffixes) is not
// verified. Accepted range is therefore 0..999 as plain digits; Apply is gated below 999
// so the post-Apply value is still representable. Anything else is rejected, never read as 0.
// Consequence (fail-safe): after 999 successes per field and save, refresh stops (OPEN).
inline constexpr u64 max_revision = 999;
inline constexpr unsigned rollback_confirm_reads = 3;  // consecutive lower reads => rollback

inline bool parse_revision(const char* text, std::size_t size, u64& out) noexcept {
    if (!text || size == 0 || size > 3) return false;
    if (size > 1 && text[0] == '0') return false;
    u64 value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    if (value > max_revision) return false;
    out = value;
    return true;
}

enum class Field : unsigned { leader = 0, party = 1 };
inline constexpr std::size_t field_count = 2;
enum class Role : unsigned { editor, launcher };
struct ModelSpec { const char* name; Field field; Role role; const char* carrier; };
// Model names are the existing fixture names. Carrier widget names are OPEN proposals.
inline constexpr ModelSpec model_specs[] = {
    {"rn_ip_leader_gui", Field::leader, Role::editor, "rn_ip_leader_rev"},
    {"rn_ip_leader_launcher_gui", Field::leader, Role::launcher, "rn_ip_leader_rev"},
    {"rn_ip_party_neutrality_gui", Field::party, Role::editor, "rn_ip_party_neutrality_rev"},
    {"rn_ip_party_neutrality_launcher_gui", Field::party, Role::launcher, "rn_ip_party_neutrality_rev"},
};
inline constexpr std::size_t model_count = sizeof(model_specs) / sizeof(model_specs[0]);

// ---------------------------------------------------------------------------
// Scalar tracker. Never stores an engine pointer. Not internally locked: Service holds a
// mutex around every call and never across a native call.
struct FieldState {
    bool seeded{}, pending{}, suspended{}, deferred{}, poisoned{};
    u64 acked{}, observed{}, suspended_rev{}, deferred_edge{}, poisoned_edge{};
    unsigned lower_streak{};
};
// native_calls counts ATTEMPTS (recorded before the call); acks/failures count only
// verified results after the call.
struct Counters {
    u64 seeds{}, seed_refusals{}, observations_ignored_unseeded{}, stale_lower_reads{}, rollbacks{};
    u64 binds{}, invalid_resets{}, dispatch_allowed{}, dispatch_rejected{};
    u64 plans{}, precall_rejects{}, native_calls{}, acks{}, failures{}, stale_results{};
};
struct Plan { bool valid{}; u64 serial{}; bool fields[field_count]{}; u64 target[field_count]{}; };

class Tracker {
public:
    enum class Seed { seeded, observed, refused_poisoned, refused_bad };
    enum class Observe { ignored_unseeded, unchanged, increased, lower_unconfirmed, rollback_confirmed, invalid_value };
    // Prefill-only entry point (integrated render bridge prefill, before Apply is possible).
    Seed seed(Field f, u64 rev) noexcept {
        auto& s = state_[idx(f)];
        if (rev > max_revision) { ++counters_.seed_refusals; return Seed::refused_bad; }
        if (s.poisoned && s.poisoned_edge == edge_[idx(f)]) { ++counters_.seed_refusals; return Seed::refused_poisoned; }
        if (!s.seeded) {
            s = FieldState{};
            s.seeded = true; s.acked = rev; s.observed = rev;
            ++counters_.seeds;
            return Seed::seeded;
        }
        observe(f, rev);  // an existing seed is never overwritten by a later value
        return Seed::observed;
    }
    // Editor/launcher tick observation. Never creates a baseline. A lower value is first
    // treated as stale carrier text (pending kept); only rollback_confirm_reads consecutive
    // lower reads confirm a real rollback, which drops baseline and pending.
    Observe observe(Field f, u64 rev) noexcept {
        auto& s = state_[idx(f)];
        if (rev > max_revision) return Observe::invalid_value;
        if (!s.seeded) { ++counters_.observations_ignored_unseeded; return Observe::ignored_unseeded; }
        if (rev < s.observed) {
            if (++s.lower_streak >= rollback_confirm_reads) { poison(f); ++counters_.rollbacks; return Observe::rollback_confirmed; }
            ++counters_.stale_lower_reads;
            return Observe::lower_unconfirmed;
        }
        s.lower_streak = 0;
        if (rev == s.observed) return Observe::unchanged;
        s.observed = rev;
        if (s.suspended && rev > s.suspended_rev) s.suspended = false;  // new success = new attempt
        s.deferred = false;                                              // new success = new attempt
        s.pending = s.observed > s.acked;
        return Observe::increased;
    }
    // Hard gate for the integrated Apply dispatch: seed required, no unobserved jump, no
    // pending rollback suspicion, headroom for the +1. Unseeded => reject (MAJOR-1).
    bool dispatch_allowed(Field f, u64 rev_now) noexcept {
        const auto& s = state_[idx(f)];
        const bool ok = s.seeded && s.lower_streak == 0 && rev_now == s.observed && rev_now < max_revision;
        if (ok) ++counters_.dispatch_allowed; else ++counters_.dispatch_rejected;
        return ok;
    }
    void count_dispatch_reject() noexcept { ++counters_.dispatch_rejected; }
    void bind(Field f) noexcept { ++bind_gen_[idx(f)]; reset(f); ++counters_.binds; }
    void invalid(Field f) noexcept { poison(f); ++counters_.invalid_resets; }
    void visibility(std::size_t model, bool visible) noexcept {
        if (model >= model_count) return;
        if (visible && !visible_[model]) ++edge_[idx(model_specs[model].field)];
        visible_[model] = visible;
    }
    Plan plan() noexcept {
        Plan p{};
        if (inflight_.valid) return p;  // also the re-entrancy guard
        for (std::size_t i = 0; i < field_count; ++i) {
            const auto& s = state_[i];
            if (!s.seeded || !s.pending || s.observed <= s.acked) continue;
            if (s.lower_streak) continue;  // stale/rollback suspicion: no request on unconfirmed text
            if (s.suspended && s.suspended_rev >= s.observed) continue;
            if (s.deferred && s.deferred_edge == edge_[i]) continue;
            p.fields[i] = true; p.target[i] = s.observed; p.valid = true;
        }
        if (!p.valid) return p;
        p.serial = ++serial_;
        inflight_ = p;
        for (std::size_t i = 0; i < field_count; ++i) inflight_gen_[i] = bind_gen_[i];
        ++counters_.plans;
        return p;
    }
    // Guards failed before any native call: wait for a new visibility edge or a new success.
    void precall_rejected(u64 serial) noexcept {
        if (!take(serial)) return;
        for (std::size_t i = 0; i < field_count; ++i) if (last_.fields[i] && current(i)) {
            state_[i].deferred = true; state_[i].deferred_edge = edge_[i];
        }
        ++counters_.precall_rejects;
    }
    void called(u64 serial) noexcept { if (inflight_.valid && inflight_.serial == serial) ++counters_.native_calls; }
    // ok only after fresh post-call relookup + same gui/view + dirty==1 + revision readback.
    void finished(u64 serial, bool ok) noexcept {
        if (!take(serial)) { ++counters_.stale_results; return; }
        bool any = false;
        for (std::size_t i = 0; i < field_count; ++i) if (last_.fields[i] && current(i)) {
            any = true;
            auto& s = state_[i];
            if (ok) { if (last_.target[i] > s.acked) s.acked = last_.target[i]; s.pending = s.observed > s.acked; }
            else { s.suspended = true; s.suspended_rev = last_.target[i]; }
        }
        if (!any) { ++counters_.stale_results; return; }
        if (ok) ++counters_.acks; else ++counters_.failures;
    }
    const FieldState& state(Field f) const noexcept { return state_[idx(f)]; }
    const Counters& counters() const noexcept { return counters_; }
    bool inflight() const noexcept { return inflight_.valid; }
    u64 bind_generation(Field f) const noexcept { return bind_gen_[idx(f)]; }

private:
    static std::size_t idx(Field f) noexcept { return static_cast<std::size_t>(f); }
    bool current(std::size_t i) const noexcept { return inflight_gen_[i] == bind_gen_[i] && state_[i].seeded; }
    bool take(u64 serial) noexcept {
        if (!inflight_.valid || inflight_.serial != serial) return false;
        last_ = inflight_; inflight_ = Plan{};
        return true;
    }
    void reset(Field f) noexcept {
        state_[idx(f)] = FieldState{};
        if (inflight_.valid && inflight_.fields[idx(f)]) inflight_.fields[idx(f)] = false;
    }
    void poison(Field f) noexcept {
        reset(f);
        state_[idx(f)].poisoned = true; state_[idx(f)].poisoned_edge = edge_[idx(f)];
    }
    FieldState state_[field_count]{};
    u64 bind_gen_[field_count]{}, inflight_gen_[field_count]{}, edge_[field_count]{};
    bool visible_[model_count]{};
    Plan inflight_{}, last_{};
    u64 serial_{};
    Counters counters_{};
};

// ---------------------------------------------------------------------------
// Fresh guard chain. Mem must provide: bool read(uptr address, void* out, size_t n) const
// (bounded/SEH in process). All results are copied scalars; pointers live on the stack.
enum class Reject : unsigned {
    none, window_read, model_vt, token_image, context_token, parent_token, parent_override,
    parent_sg_window, idler_null, idler_vt, idler_slot, manager_idler, gui_null, gui_vt, gui_idler,
    view_null, view_vt, observer_vt, view_slots, view_gui, view_idler, root_null, parent_root, dirty_read,
};
inline const char* reject_name(Reject r) noexcept {
    static constexpr const char* names[] = {"none", "window-read", "model-vt", "token-image", "context-token",
        "parent-token", "parent-override", "parent-sg-window", "idler-null", "idler-vt", "idler-slot", "manager-idler",
        "gui-null", "gui-vt", "gui-idler", "view-null", "view-vt", "observer-vt", "view-slots", "view-gui", "view-idler",
        "root-null", "parent-root", "dirty-read"};
    const auto i = static_cast<unsigned>(r);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "unknown";
}
struct Target { uptr gui{}, view{}; unsigned char dirty{}; };  // stack-only
// Bounded diagnostics for a rejecting resolve: RVA (relative to module base) of the observed
// vtable/slot and the expected value, so a runtime run reveals the real class. observed==0
// means the field was unreadable. gui_vt_rva is always filled when gui is readable (the gui
// vtable is logged but NOT a gate; see resolve).
struct ResolveDiag { Reject reject{Reject::none}; uptr observed_rva{}, expected_rva{}, slot_rva{}, gui_vt_rva{}; };

template <class T, class M> bool rd(const M& mem, uptr object, uptr offset, T& out) noexcept {
    if (!object || offset > UINTPTR_MAX - object) return false;
    return mem.read(object + offset, &out, sizeof(T));
}

template <class M> Reject resolve(const M& mem, uptr image, uptr window, Target& out, ResolveDiag* diag = nullptr) noexcept {
    out = Target{};
    if (diag) *diag = ResolveDiag{};
    auto vt_reject = [&](Reject r, uptr observed, uptr expected) noexcept {
        if (diag) { diag->reject = r; diag->observed_rva = observed ? observed - image : 0; diag->expected_rva = expected - image; }
        return r;
    };
    uptr model = 0, model_vt = 0;
    if (!rd(mem, window, off::window_model, model) || !rd(mem, model, 0, model_vt)) return Reject::window_read;
    if (model_vt != image + rva::vt_sg_model) return vt_reject(Reject::model_vt, model_vt, image + rva::vt_sg_model);
    std::uint32_t politics_token = 0, context_token = 0, model_context = 0, model_parent = 0;
    if (!rd(mem, image, rva::token_politics_tab, politics_token) || !rd(mem, image, rva::token_player_context, context_token) ||
        politics_token != token_politics_tab_value || context_token != token_player_context_value) return Reject::token_image;
    if (!rd(mem, model, off::model_context_token, model_context) || model_context != context_token) return Reject::context_token;
    if (!rd(mem, model, off::model_parent_token, model_parent) || model_parent != politics_token) return Reject::parent_token;
    u64 parent_window_len = 1, parent_sg_len = 1;
    if (!rd(mem, model, off::model_parent_window_len, parent_window_len) || !rd(mem, model, off::model_parent_sg_len, parent_sg_len) ||
        parent_window_len != 0 || parent_sg_len != 0) return Reject::parent_override;
    uptr parent_sg = 1, parent = 0;
    if (!rd(mem, window, off::window_parent_sg, parent_sg) || parent_sg != 0) return Reject::parent_sg_window;
    if (!rd(mem, window, off::window_parent, parent)) return Reject::window_read;
    uptr idler = 0, idler_vt = 0, getter = 0;
    if (!rd(mem, window, off::window_idler, idler) || !idler) return Reject::idler_null;
    if (!rd(mem, idler, 0, idler_vt) || idler_vt != image + rva::vt_ingame_idler) return vt_reject(Reject::idler_vt, idler_vt, image + rva::vt_ingame_idler);
    if (!rd(mem, idler_vt, off::slot_idler_gui, getter) || getter != image + rva::idler_gui_getter) {
        if (diag) { diag->reject = Reject::idler_slot; diag->observed_rva = getter ? getter - image : 0; diag->expected_rva = rva::idler_gui_getter; diag->slot_rva = rva::vt_ingame_idler + off::slot_idler_gui; }
        return Reject::idler_slot;
    }
    uptr manager = 0, manager_idler = 0;
    if (!rd(mem, window, off::window_manager, manager) || !rd(mem, manager, off::manager_idler, manager_idler) ||
        manager_idler != idler) return Reject::manager_idler;
    // Fresh getter: DCCF30 is exactly `mov rax,[rcx+6B8]; ret` (fingerprinted), so the
    // bounded read below is its exact semantics without executing native code.
    uptr gui = 0, gui_vt = 0, gui_idler = 0;
    if (!rd(mem, idler, off::idler_gui, gui) || !gui) return Reject::gui_null;
    // gui vtable is DIAGNOSTIC ONLY (PID3044: live GameGui is a more-derived singleton whose
    // offset-0 vtable is not the plain CGameGui 294A688). B6E210/B674F0 read only gui+C8, so
    // the gui identity is closed by the idler round-trip (gui+180==idler) and the politics-view
    // checks below (view vtable, view+38==gui, view+570==idler), never by the gui vtable.
    if (rd(mem, gui, 0, gui_vt) && diag) diag->gui_vt_rva = gui_vt ? gui_vt - image : 0;
    if (!rd(mem, gui, off::gui_idler, gui_idler) || gui_idler != idler) return Reject::gui_idler;
    uptr view = 0, view_vt = 0, observer_vt = 0;
    if (!rd(mem, gui, off::gui_views + 8 * static_cast<uptr>(politics_view_index), view) || !view) return Reject::view_null;
    if (!rd(mem, view, 0, view_vt) || view_vt != image + rva::vt_politics_view) return vt_reject(Reject::view_vt, view_vt, image + rva::vt_politics_view);
    if (!rd(mem, view, off::view_observer, observer_vt) || observer_vt != image + rva::vt_politics_observer) return vt_reject(Reject::observer_vt, observer_vt, image + rva::vt_politics_observer);
    for (const auto& pin : slot_pins) {
        if (pin.vtable == rva::vt_ingame_idler) continue;  // checked above
        uptr slot = 0;
        if (!rd(mem, image + pin.vtable, pin.slot, slot) || slot != image + pin.target) {
            if (diag) { diag->reject = Reject::view_slots; diag->observed_rva = slot ? slot - image : 0; diag->expected_rva = pin.target; diag->slot_rva = pin.vtable + pin.slot; }
            return Reject::view_slots;
        }
    }
    uptr view_gui = 0, view_idler = 0, root = 0;
    if (!rd(mem, view, off::view_gui, view_gui) || view_gui != gui) return Reject::view_gui;
    if (!rd(mem, view, off::view_idler, view_idler) || view_idler != idler) return Reject::view_idler;
    if (!rd(mem, view, off::view_root, root) || !root) return Reject::root_null;
    if (parent != root) return Reject::parent_root;
    unsigned char dirty = 0xFF;
    if (!rd(mem, view, off::view_dirty, dirty) || dirty > 1) return Reject::dirty_read;
    out.gui = gui; out.view = view; out.dirty = dirty;
    return Reject::none;
}

enum class Outcome : unsigned { ok, precall_reject, call_fault, postcall_reject, postcall_identity, readback_dirty, readback_revision };
inline const char* outcome_name(Outcome o) noexcept {
    static constexpr const char* names[] = {"ok", "precall-reject", "call-fault", "postcall-reject", "postcall-identity",
                                            "readback-dirty", "readback-revision"};
    return names[static_cast<unsigned>(o)];
}

// ---------------------------------------------------------------------------
// Own scripted window inspection (render bridge layout, runtime-proven there).
enum class WindowKind { unrelated, invalid, hidden, visible };
struct OwnWindow { std::size_t model{model_count}; uptr root{}; };

template <class M> bool string_equals(const M& mem, uptr native, const char* expected) noexcept {
    struct S { union { unsigned char small[16]; uptr pointer; } data; u64 size, capacity; } s{};
    const auto wanted = std::strlen(expected);
    if (!mem.read(native, &s, sizeof(s)) || s.size != wanted || s.size > s.capacity || s.capacity > 1048576 || wanted > 128) return false;
    char bytes[129]{};
    if (s.capacity <= 15) std::memcpy(bytes, s.data.small, 16);
    else if (!mem.read(s.data.pointer, bytes, static_cast<std::size_t>(s.size))) return false;
    return std::memcmp(bytes, expected, wanted) == 0;
}

template <class M> WindowKind inspect(const M& mem, uptr image, uptr window, OwnWindow& out) noexcept {
    out = OwnWindow{};
    uptr model = 0, model_vt = 0;
    if (!rd(mem, window, off::window_model, model) || !rd(mem, model, 0, model_vt) || model_vt != image + rva::vt_sg_model)
        return WindowKind::unrelated;
    for (std::size_t i = 0; i < model_count; ++i)
        if (string_equals(mem, model + off::model_name, model_specs[i].name)) { out.model = i; break; }
    if (out.model == model_count) return WindowKind::unrelated;
    const uptr context = window + off::window_context;
    uptr ctx_vt = 0, ctx_window = 0, ctx_model = 0, ctx_root = 0, root = 0, root_vt = 0;
    unsigned char flags = 0, desired = 0;
    if (!rd(mem, context, 0, ctx_vt) || ctx_vt != image + rva::vt_sg_context || !rd(mem, context, off::context_window, ctx_window) ||
        ctx_window != window || !rd(mem, context, off::context_model, ctx_model) || ctx_model != model ||
        !rd(mem, context, off::context_root, ctx_root) || !rd(mem, window, off::window_root, root) || !root || ctx_root != root ||
        !rd(mem, root, 0, root_vt) || root_vt != image + rva::vt_sg_root || !rd(mem, root, off::root_flags, flags) ||
        !rd(mem, window, off::window_desired, desired))
        return WindowKind::invalid;
    out.root = root;
    return desired && (flags & 8) && !(flags & (4 | 16)) ? WindowKind::visible : WindowKind::hidden;
}

// ---------------------------------------------------------------------------
// Service. Entry points (all on the UI thread, AFTER the original native function):
//   on_bind(window)      post-original Bind B86320
//   on_tick(window)      post-original Tick B88950: observation + at most one refresh plan
//   on_prefill(window)   INTEGRATION ONLY: the reviewed bridge's one-shot opening prefill.
//                        The ONLY place a baseline is created (MAJOR-1).
//   on_dispatch(window)  INTEGRATION ONLY: hard gate before the own Apply dispatch; false =>
//                        the Apply must be dropped (unseeded, jump, rollback suspicion, cap).
// Without the integration (this standalone shell) no seed can exist, so no refresh happens.
//
// Load-without-Bind policy (MAJOR-3), chosen explicitly:
//   * Bind => full reset (observed once per model per load: run17884/run36204 logs).
//   * Confirmed lower counter (3 consecutive reads) => reset, new prefill seed required.
//   * Equal/higher counter after a hypothetical bindless load is ACCEPTED: the worst case is
//     one extra or one missed B6E210 dirty request (a plain native panel refresh, as when
//     reopening the panel); never a write, command or retry. World identity stays OPEN and
//     no country/world authority is required (see "Minimal refresh guard" below).
// Env must provide (all noexcept):
//   const M& mem() ; uptr image();
//   bool carrier(uptr root, const char* name, char* out, size_t cap, size_t& size)  // fresh lookup
//   bool call_refresh(uptr gui, int index)   // SEH-guarded B6E210; false = fault
//   bool refresh_armed()                     // operational arm switch: false in the inactive
//                                            // shell, true only after the armed bridge verified all pins
//
// Minimal refresh guard (replaces the former "authority" requirement). A B6E210 request is
// issued only when ALL hold, freshly in the same post-Tick call on the UI thread:
//   1. the field was seeded by the bridge prefill (own editor opening) in this bind generation;
//   2. the script success counter of that field increased above the acked value (and is not
//      under rollback/stale suspicion);
//   3. the ticking window is one of our 4 own player_context/politics_tab models, visible;
//   4. resolve(): its idler/manager/GameGui/politics view chain is consistent and the window's
//      parent IS the current politics view root (same GUI/view the editor lives in).
// Why a false positive is harmless: B6E210(gui,12) only sets view+44=1 if the politics view is
// visible; the frame loop then calls the view's own observer (15863F0) which re-reads the
// CURRENT data and rewrites captions/rows/portrait/chart, exactly as on panel reopen and as
// 7 native callers already do. It takes no data argument, writes no game state, runs no command
// and is local UI only (no MP sync). Worst case of a wrong guess: one redundant redraw.
//   void log(const char* line)
template <class Env> class Service {
public:
    explicit Service(Env& environment) noexcept : env_(environment) {}
    void on_bind(uptr window) noexcept {
        OwnWindow own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == WindowKind::unrelated) return;
        const auto f = model_specs[own.model].field;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (kind == WindowKind::invalid) tracker_.invalid(f); else tracker_.bind(f);
            invalid_[own.model] = kind == WindowKind::invalid;
            carrier_bad_[own.model] = false;
        }
        line("bind", own.model, kind == WindowKind::invalid ? "invalid-reset" : "baseline-reset", 0);
    }
    void on_tick(uptr window) noexcept {
        OwnWindow own{};
        const auto kind = inspect(env_.mem(), env_.image(), window, own);
        if (kind == WindowKind::unrelated) return;
        const auto& spec = model_specs[own.model];
        if (kind == WindowKind::invalid) {
            bool transition = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tracker_.visibility(own.model, false);
                transition = !invalid_[own.model];
                invalid_[own.model] = true;
                if (transition) tracker_.invalid(spec.field);  // edge-triggered, not per frame
            }
            if (transition) line("observe", own.model, "invalid-context-reset", 0);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            invalid_[own.model] = false;
            tracker_.visibility(own.model, kind == WindowKind::visible);  // hidden keeps seed/ack/pending
        }
        if (kind != WindowKind::visible) return;
        u64 rev = 0;
        if (!read_revision(own.root, spec.carrier, rev)) { carrier_rejected(own.model); return; }
        Tracker::Observe result{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            carrier_bad_[own.model] = false;
            result = tracker_.observe(spec.field, rev);
            if (result == Tracker::Observe::lower_unconfirmed && tracker_.state(spec.field).lower_streak != 1)
                result = Tracker::Observe::unchanged;  // log only the first lower read of a streak
        }
        if (result == Tracker::Observe::lower_unconfirmed) line("observe", own.model, "lower-revision-pending-kept", rev);
        if (result == Tracker::Observe::rollback_confirmed) line("observe", own.model, "rollback-confirmed-reset", rev);
        service(window, own);
    }
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
        { std::lock_guard<std::mutex> lock(mutex_); tracker_.visibility(own.model, true); }
        if (!read_revision(own.root, model_specs[own.model].carrier, rev)) {
            { std::lock_guard<std::mutex> lock(mutex_); tracker_.seed(f, max_revision + 1); }  // counted refusal
            line("prefill", own.model, "seed-carrier-rejected-apply-blocked", 0);
            return Tracker::Seed::refused_bad;
        }
        Tracker::Seed result{};
        { std::lock_guard<std::mutex> lock(mutex_); result = tracker_.seed(f, rev); }
        line("prefill", own.model,
             result == Tracker::Seed::seeded ? "seeded" : result == Tracker::Seed::observed ? "existing-seed-kept" : "seed-refused", rev);
        return result;
    }
    bool on_dispatch(uptr window) noexcept {
        OwnWindow own{};
        u64 rev = 0;
        const bool readable = inspect(env_.mem(), env_.image(), window, own) == WindowKind::visible &&
                              model_specs[own.model].role == Role::editor &&
                              read_revision(own.root, model_specs[own.model].carrier, rev);
        bool allowed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (readable) allowed = tracker_.dispatch_allowed(model_specs[own.model].field, rev);
            else tracker_.count_dispatch_reject();
        }
        if (!allowed) line("dispatch", own.model, "apply-rejected-no-valid-seed", rev);
        return allowed;
    }
    bool dispatch_allowed(Field f, u64 rev_now) noexcept { std::lock_guard<std::mutex> lock(mutex_); return tracker_.dispatch_allowed(f, rev_now); }
    Tracker snapshot() noexcept { std::lock_guard<std::mutex> lock(mutex_); return tracker_; }

private:
    bool read_revision(uptr root, const char* name, u64& rev) noexcept {
        char text[32]{}; std::size_t size = 0;
        return env_.carrier(root, name, text, sizeof(text) - 1, size) && size < sizeof(text) && parse_revision(text, size, rev);
    }
    void carrier_rejected(std::size_t model) noexcept {
        bool transition = false;
        { std::lock_guard<std::mutex> lock(mutex_); transition = !carrier_bad_[model]; carrier_bad_[model] = true; }
        if (transition) line("observe", model, "revision-carrier-rejected", 0);  // once per ok->bad transition
    }
    void service(uptr window, const OwnWindow& own) noexcept {
        Plan plan{};
        { std::lock_guard<std::mutex> lock(mutex_); plan = tracker_.plan(); }
        if (!plan.valid) return;
        if (!env_.refresh_armed()) {  // operational switch, not an authority proof
            { std::lock_guard<std::mutex> lock(mutex_); tracker_.precall_rejected(plan.serial); }
            line("plan", own.model, "disarmed-no-call", plan.serial);
            return;
        }
        Target before{};
        ResolveDiag diag{};
        const auto pre = resolve(env_.mem(), env_.image(), window, before, &diag);
        if (pre != Reject::none) {
            { std::lock_guard<std::mutex> lock(mutex_); tracker_.precall_rejected(plan.serial); }
            reject_line(own.model, reject_name(pre), plan.serial, diag);
            return;
        }
        { std::lock_guard<std::mutex> lock(mutex_); tracker_.called(plan.serial); }
        // No lock is held across the native call. Exactly one call per plan.
        const bool returned = env_.call_refresh(before.gui, politics_view_index);
        Outcome outcome = Outcome::ok;
        Target after{};
        if (!returned) outcome = Outcome::call_fault;
        else if (resolve(env_.mem(), env_.image(), window, after) != Reject::none) outcome = Outcome::postcall_reject;
        else if (after.gui != before.gui || after.view != before.view) outcome = Outcome::postcall_identity;
        else if (after.dirty != 1) outcome = Outcome::readback_dirty;
        else {
            OwnWindow again{}; u64 rev = 0;
            if (inspect(env_.mem(), env_.image(), window, again) != WindowKind::visible || again.model != own.model ||
                !read_revision(again.root, model_specs[own.model].carrier, rev) ||
                rev < plan.target[static_cast<std::size_t>(model_specs[own.model].field)])
                outcome = Outcome::readback_revision;
        }
        { std::lock_guard<std::mutex> lock(mutex_); tracker_.finished(plan.serial, outcome == Outcome::ok); }
        // dirty already 1 before the call: accepted (refresh is scheduled anyway) but labelled
        // as non-discriminating evidence of this call's effect.
        line("refresh", own.model, outcome == Outcome::ok && before.dirty == 1 ? "ok-already-dirty" : outcome_name(outcome), plan.serial);
    }
    void line(const char* event, std::size_t model, const char* detail, u64 value) noexcept {
        char text[256]{};
        std::snprintf(text, sizeof(text), "RN_CAPTION event=%s model=%s detail=%s value=%llu\r\n", event,
                      model < model_count ? model_specs[model].name : "?", detail, static_cast<unsigned long long>(value));
        env_.log(text);
    }
    // Bounded vtable/slot diagnostic for a rejecting resolve: observed/expected RVA relative to
    // module base, plus the gui vtable RVA (logged, not a gate), so a run reveals the real class.
    void reject_line(std::size_t model, const char* detail, u64 serial, const ResolveDiag& diag) noexcept {
        char text[320]{};
        std::snprintf(text, sizeof(text),
                      "RN_CAPTION event=precall model=%s detail=%s serial=%llu observed_vt_rva=0x%llX expected_vt_rva=0x%llX "
                      "slot_rva=0x%llX gui_vt_rva=0x%llX\r\n",
                      model < model_count ? model_specs[model].name : "?", detail, static_cast<unsigned long long>(serial),
                      static_cast<unsigned long long>(diag.observed_rva), static_cast<unsigned long long>(diag.expected_rva),
                      static_cast<unsigned long long>(diag.slot_rva), static_cast<unsigned long long>(diag.gui_vt_rva));
        env_.log(text);
    }
    Env& env_;
    std::mutex mutex_;
    Tracker tracker_;
    bool invalid_[model_count]{}, carrier_bad_[model_count]{};
};

// Hook body shape: forward to the original exactly once, unconditionally and first,
// with no lock held; observation runs afterwards and cannot suppress or repeat it.
template <class Original, class Observe, class... Args>
void forward_then_observe(Original original, Observe&& observe, bool active, Args... args) {
    original(args...);
    if (active) observe(args...);
}
}  // namespace rn::caption
