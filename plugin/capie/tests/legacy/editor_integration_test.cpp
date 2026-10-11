// Integration tests of the GENERATED armed bridge (caption_refresh_bridge.cpp) on the shared
// hooks, in an owned fake image. No game process, no hooks installed, no real native call.
#include "../compat/rn_editor_bridge.hpp"
#include <array>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
template <class T> void put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}
using rn::caption::uptr;
namespace off = rn::caption::off;
namespace crva = rn::caption::rva;

struct OwnWindow {
    alignas(8) std::array<unsigned char, 0xC00> window{};
    alignas(8) std::array<unsigned char, 0x1360> root{};
    alignas(8) std::array<unsigned char, 0x100> model{};
    alignas(8) std::array<unsigned char, 0x140> revision{};  // instantTextBox carrier
};
struct World {
    alignas(8) std::array<unsigned char, 0x700> idler{};
    alignas(8) std::array<unsigned char, 0x200> gui{};
    alignas(8) std::array<unsigned char, 0x600> view{};
    alignas(8) std::array<unsigned char, 0x100> politics_root{};
    alignas(8) std::array<unsigned char, 0x100> manager{};
    std::array<OwnWindow, rn::caption::model_count> own{};
    std::array<std::string, rn::caption::model_count> rev{};
    alignas(8) std::array<unsigned char, 0x280> leader_edit{};
    alignas(8) std::array<unsigned char, 0x50> leader_definition{};
    alignas(8) std::array<unsigned char, 0x238> focus_manager{};
    alignas(8) std::array<unsigned char, 0x140> leader_prefill{};
    alignas(8) std::array<unsigned char, 0x140> leader_epoch{};
    std::uint32_t leader_text[64]{};
};
World* world = nullptr;
unsigned tick_originals = 0, bind_originals = 0, dispatch_originals = 0, refresh_calls = 0, refresh_bad = 0;
bool refresh_saw_lock = false;
std::string revision_storage[rn::caption::model_count];

void set_rev(std::size_t m, const std::string& value) {
    revision_storage[m] = value;
    MsvcString text{}; text.size = value.size();
    if (value.size() <= 15) { std::memcpy(text.data.small, value.data(), value.size()); text.capacity = 15; }
    else { text.data.pointer = revision_storage[m].c_str(); text.capacity = value.size(); }
    put(world->own[m].revision.data(), 0x110, text);
}
void show(std::size_t m, bool visible) {
    put(world->own[m].root.data(), 0xA5, static_cast<unsigned char>(visible ? 8 : 0));
    put(world->own[m].window.data(), 0xB79, static_cast<unsigned char>(visible ? 1 : 0));
}
void build_world(World& w) {
    const auto img = reinterpret_cast<unsigned char*>(image_base);
    put(img, crva::token_politics_tab, rn::caption::token_politics_tab_value);
    put(img, crva::token_player_context, rn::caption::token_player_context_value);
    for (const auto& pin : rn::caption::slot_pins) put(img, pin.vtable + pin.slot, image_base + pin.target);
    put(w.idler.data(), 0, image_base + crva::vt_ingame_idler); put(w.idler.data(), off::idler_gui, static_cast<void*>(w.gui.data()));
    put(w.gui.data(), off::gui_idler, static_cast<void*>(w.idler.data()));  // word 0 is data
    put(w.gui.data(), off::gui_views + 8 * 12, static_cast<void*>(w.view.data()));
    put(w.view.data(), 0, image_base + crva::vt_politics_view); put(w.view.data(), off::view_observer, image_base + crva::vt_politics_observer);
    put(w.view.data(), off::view_gui, static_cast<void*>(w.gui.data())); put(w.view.data(), off::view_idler, static_cast<void*>(w.idler.data()));
    put(w.view.data(), off::view_root, static_cast<void*>(w.politics_root.data()));
    put(w.manager.data(), off::manager_idler, static_cast<void*>(w.idler.data()));
    for (std::size_t m = 0; m < rn::caption::model_count; ++m) {
        auto& o = w.own[m]; auto* win = o.window.data();
        put(o.model.data(), 0, image_base + 0x29DB558);
        put(o.model.data(), 8, borrowed_string(rn::caption::model_specs[m].name, std::strlen(rn::caption::model_specs[m].name)));
        put(o.model.data(), off::model_context_token, rn::caption::token_player_context_value);
        put(o.model.data(), off::model_parent_token, rn::caption::token_politics_tab_value);
        put(win, 0xE8, static_cast<void*>(o.model.data())); put(win, 0xF8, static_cast<void*>(o.root.data()));
        put(win, 0x110, image_base + 0x294E568); put(win, 0x120, static_cast<void*>(win));
        put(win, 0x130, static_cast<void*>(o.model.data())); put(win, 0x138, static_cast<void*>(o.root.data()));
        put(win, off::window_idler, static_cast<void*>(w.idler.data())); put(win, off::window_manager, static_cast<void*>(w.manager.data()));
        put(win, off::window_parent, static_cast<void*>(w.politics_root.data()));
        put(o.root.data(), 0, image_base + 0x2B45278);
        put(o.revision.data(), 0, image_base + 0x2B468C8);
    }
}
std::size_t own_index_of_root(void* root) {
    for (std::size_t m = 0; m < rn::caption::model_count; ++m) if (root == world->own[m].root.data()) return m;
    return rn::caption::model_count;
}
void* __fastcall fake_instant(void* root, const MsvcString* name, bool, bool) {
    const auto m = own_index_of_root(root);
    if (m == 0 && name_equals(name, "rn_ip_leader_prefill")) return world->leader_prefill.data();
    if (m == 0 && name_equals(name, "rn_ip_leader_epoch")) return world->leader_epoch.data();
    if (m < rn::caption::model_count && name_equals(name, rn::caption::model_specs[m].carrier) && !revision_storage[m].empty())
        return world->own[m].revision.data();
    return nullptr;
}
unsigned leader_lookups = 0, bump_generation_on_lookup = 0;
void* __fastcall fake_edit(void* root, const MsvcString* name, bool, bool) {
    if (own_index_of_root(root) == 0 && name_equals(name, "rn_ip_leader_edit")) {
        // B02: simulate a lifecycle change (e.g. a concurrent rebind) on a chosen lookup, AFTER the
        // prefill's last same-opening check, so prefill completes but readiness is not committed.
        if (++leader_lookups == bump_generation_on_lookup) ++lifecycle[0].generation;
        return world->leader_edit.data();
    }
    return nullptr;
}
unsigned set_text_calls = 0;
void __fastcall fake_set_text(void* tb, const MsvcString* text) {
    ++set_text_calls;
    auto* edit = static_cast<unsigned char*>(tb) - 0x80;
    const char* bytes = text->capacity <= 15 ? reinterpret_cast<const char*>(text->data.small) : text->data.pointer;
    for (std::size_t i = 0; i < text->size && i < 64; ++i) world->leader_text[i] = static_cast<unsigned char>(bytes[i]);
    put(edit, 0xB0, static_cast<void*>(world->leader_text));
    put(edit, 0xC0, static_cast<std::uint64_t>(text->size));
    put(edit, 0xC8, std::uint64_t{64});
}
void __fastcall fake_refresh_edit(void*) {}
void prepare_leader_prefill(World& w) {
    auto* edit = w.leader_edit.data();
    put(edit, 0, image_base + 0x2B4A940); put(edit, 0x80, image_base + 0x2B4ABD0);
    put(edit, 0x60, static_cast<void*>(w.leader_definition.data()));
    put(w.leader_definition.data(), 0, image_base + 0x2B54050);
    put(w.leader_definition.data(), 0x28, borrowed_string("rn_ip_leader_edit", 17));
    put(edit, 0x75, static_cast<unsigned char>(8)); put(edit, 0x271, static_cast<unsigned char>(0));
    put(edit, 0xB0, static_cast<void*>(w.leader_text)); put(edit, 0xC0, std::uint64_t{0}); put(edit, 0xC8, std::uint64_t{64});
    put(reinterpret_cast<void*>(image_base), 0x3453230, static_cast<void*>(w.focus_manager.data()));
    put(w.leader_prefill.data(), 0, image_base + 0x2B468C8); put(w.leader_prefill.data(), 0x110, borrowed_string("Jan", 3));
    put(w.leader_epoch.data(), 0, image_base + 0x2B468C8); put(w.leader_epoch.data(), 0x110, borrowed_string("7", 1));
    set_text = fake_set_text; refresh_edit = fake_refresh_edit;
}
void __fastcall fake_tick(void*, char) { ++tick_originals; }
void __fastcall fake_bind(void*) { ++bind_originals; }
void __fastcall fake_dispatch(void*, void*) { ++dispatch_originals; }
// RN6: a native-like dispatch that runs the (bridged) effect constructor, so the injected record
// view is observable exactly as the game would copy it.
struct CapturedRecord { std::string key; std::int64_t raw; };
std::vector<CapturedRecord> captured;
unsigned ctor_count = 0;
alignas(8) unsigned char fake_effect[0x80]{};
void* __fastcall fake_ctor(void* self, const MsvcString*, const MsvcString*, const void*, const TempVector* view) {
    ++ctor_count; captured.clear();
    for (int i = 0; i < view->count; ++i) {
        char key[129]{};
        record_key(view->data[i], key);
        captured.push_back({key, view->data[i].raw_value});
    }
    return self;
}
void __fastcall fake_dispatch_ctor(void*, void*) {
    ++dispatch_originals;
    if (origin) constructor_bridged(fake_effect, origin->model_name, origin->selector, origin->scope, origin->source);
}
std::size_t captured_count(const char* key) {
    std::size_t n = 0;
    for (const auto& r : captured) if (r.key == key) ++n;
    return n;
}
std::int64_t captured_raw(const char* key) {
    for (const auto& r : captured) if (r.key == key) return r.raw;
    return 0x7FFFFFFF;
}
Record spoof(const char* key, std::int64_t raw) {
    Record r{}; const auto n = std::strlen(key);
    std::memcpy(r.key.data.small, key, n); r.key.size = n; r.key.capacity = 15; r.raw_value = raw;
    return r;
}
void __fastcall fake_refresh(void* gui, int index) {
    ++refresh_calls;
    if (gui != world->gui.data() || index != 12) ++refresh_bad;
    // No bridge lock may be held across the native request.
    for (SRWLOCK* lock : {&state_lock, &logger_lock, &debug_file_lock}) {
        if (!TryAcquireSRWLockExclusive(lock)) refresh_saw_lock = true; else ReleaseSRWLockExclusive(lock);
    }
    put(world->view.data(), off::view_dirty, static_cast<unsigned char>(1));
}
unsigned focus_calls = 0, focus_bad = 0;
void __fastcall fake_focus_switch(void* manager, void* widget) {
    ++focus_calls;
    if (manager != world->focus_manager.data() || widget != nullptr) ++focus_bad;
    auto* m = world->focus_manager.data();
    void* old = nullptr; std::memcpy(&old, m + 0x158, sizeof(old));
    if (old) {
        auto* edit = static_cast<unsigned char*>(old);
        edit[0x271] = static_cast<unsigned char>(edit[0x271] & 0xFE);
        put(m, 0x230, static_cast<void*>(nullptr));
    }
    put(m, 0x158, static_cast<void*>(nullptr));
}
std::string logs() {
    LARGE_INTEGER size{}, zero{};
    GetFileSizeEx(log_file, &size); SetFilePointerEx(log_file, zero, nullptr, FILE_BEGIN);
    std::string value(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD read = 0;
    ReadFile(log_file, value.data(), static_cast<DWORD>(value.size()), &read, nullptr);
    SetFilePointerEx(log_file, zero, nullptr, FILE_END);
    return value;
}
std::size_t count(const std::string& text, const char* marker) {
    std::size_t n = 0, p = 0;
    while ((p = text.find(marker, p)) != std::string::npos) { ++n; p += std::strlen(marker); }
    return n;
}
MsvcString selector(const char* name) { return borrowed_string(name, std::strlen(name)); }
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    void* memory = VirtualAlloc(nullptr, 0x3454000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(memory != nullptr, "owned fake image");
    image_base = reinterpret_cast<std::uintptr_t>(memory);
    log_file = CreateFileW(L"caption_refresh_bridge_test.log", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    auto* w = new World; world = w; build_world(*w);
    find_instant = fake_instant; find_edit = fake_edit;
    original_tick = fake_tick; original_bind = fake_bind; original_dispatch = fake_dispatch; original_ctor = nullptr;
    caption_request = fake_refresh;
    active.store(true);
    constexpr std::size_t LE = 0, LL = 1, PE = 2;
    void* le = w->own[LE].window.data(); void* ll = w->own[LL].window.data(); void* pe = w->own[PE].window.data();
    void* le_context = static_cast<unsigned char*>(le) + 0x110;
    void* pe_context = static_cast<unsigned char*>(pe) + 0x110;
    const auto leader_apply = selector("rn_ip_leader_apply_click");
    const auto party_apply = selector("rn_ip_party_neutrality_apply_click");

    // 1. Disarmed (as in the inherited suite): no caption effect on hooks or Apply.
    set_rev(LE, "0"); show(LE, true);
    tick_observed(le, 0);
    check(tick_originals == 1 && refresh_calls == 0, "disarmed tick forwards once, no caption work");
    check(caption_dispatch_allowed(le_context, 0) && caption_dispatch_allowed(le_context, 1) && caption_dispatch_allowed(le_context, 2),
          "disarmed gate is transparent");
    check(caption_service.snapshot().counters().observations_ignored_unseeded == 0, "disarmed service untouched");

    // 2. Armed: unseeded leader Apply is dropped before original dispatch; country unaffected.
    caption_armed.store(true);
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(dispatch_originals == 0, "unseeded leader Apply dropped (no original dispatch)");
    check(count(logs(), "caption-seed-gate-apply-rejected") == 1, "gate rejection logged once");
    check(caption_dispatch_allowed(le_context, 2), "country model never gated");

    // 3. The REAL bridge prefill (shared Tick -> observe_tick -> prefill_open) seeds the caption
    //    baseline only after its own one-shot prefill succeeded.
    prepare_leader_prefill(*w);
    tick_observed(le, 0);
    check(set_text_calls == 1 && count(logs(), "prefill-readback-pass") == 1, "bridge prefill succeeded once");
    check(caption_service.snapshot().state(rn::caption::Field::leader).seeded, "bridge prefill seeds leader");
    check(count(logs(), "event=prefill model=rn_ip_leader_gui detail=seeded") == 1, "seed logged from prefill");
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(count(logs(), "caption-seed-gate-apply-rejected") == 1, "seeded Apply passes caption gate");
    check(caption_service.snapshot().counters().dispatch_allowed == 1, "gate allowed exactly once");
    set_rev(PE, "4"); show(PE, true);
    caption_prefill(pe, 2);  // a country-model prefill must never seed, whatever the window
    check(caption_service.snapshot().counters().seeds == 1 && !caption_service.snapshot().state(rn::caption::Field::party).seeded,
          "country prefill never seeds");

    // 4. Success counter rises: one tick on the shared hook issues exactly one B6E210(gui,12).
    set_rev(LE, "1"); show(LE, false); tick_observed(le, 0);
    set_rev(LL, "1"); show(LL, true);
    const auto before = tick_originals;
    tick_observed(ll, 0);
    check(tick_originals == before + 1, "armed tick forwards exactly once");
    check(refresh_calls == 1 && refresh_bad == 0, "exactly one refresh with (gui,12)");
    check(!refresh_saw_lock, "no bridge lock across native refresh");
    check(caption_service.snapshot().state(rn::caption::Field::leader).acked == 1, "verified ack");
    check(count(logs(), "event=refresh model=rn_ip_leader_launcher_gui detail=ok") == 1, "refresh logged");
    put(w->view.data(), off::view_dirty, static_cast<unsigned char>(0));
    for (int i = 0; i < 30; ++i) tick_observed(ll, 0);
    check(refresh_calls == 1, "no per-frame repeat");

    // 5. Party: unseeded Apply dropped, then party flow independent of leader.
    set_rev(PE, "4"); show(PE, true);
    dispatch_observed(pe_context, const_cast<MsvcString*>(&party_apply));
    check(dispatch_originals == 0 && count(logs(), "caption-seed-gate-apply-rejected") == 2, "unseeded party Apply dropped");

    // 6. Bind on the shared hook: forwards once and resets the caption baseline.
    const auto binds = bind_originals;
    bind_observed(ll);
    check(bind_originals == binds + 1, "bind forwards exactly once");
    check(!caption_service.snapshot().state(rn::caption::Field::leader).seeded, "bind resets leader baseline");

    // 7. Focus release on the shared Tick: politics root visible -> hidden while OUR leader
    //    field holds focus => exactly one 225E320(manager, nullptr); then never again.
    focus_switch = fake_focus_switch; focus_armed.store(true);
    put(w->politics_root.data(), 0xA5, static_cast<unsigned char>(8));
    put(w->focus_manager.data(), 0x158, static_cast<void*>(w->leader_edit.data()));
    put(w->focus_manager.data(), 0x230, static_cast<void*>(w->leader_edit.data() + 0x1A0));
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(1));
    show(LE, true); tick_observed(le, 0);
    check(focus_calls == 0, "no release while politics visible");
    put(w->politics_root.data(), 0xA5, static_cast<unsigned char>(0));
    const auto focus_ticks = tick_originals;
    tick_observed(le, 0);
    check(tick_originals == focus_ticks + 1, "focus tick forwards original exactly once");
    check(focus_calls == 1 && focus_bad == 0, "exactly one focus release with (manager, nullptr)");
    check((w->leader_edit[0x271] & 1) == 0 && count(logs(), "RN_FOCUS event=parent-hidden model=rn_ip_leader_gui field=rn_ip_leader_edit") == 1,
          "release verified and logged");
    check(count(logs(), "draft=unchanged") == 1, "draft text untouched");
    for (int i = 0; i < 20; ++i) tick_observed(le, 0);
    check(focus_calls == 1, "no per-frame retry");

    // 8. Foreign window: forwarded, no caption state change.
    alignas(8) std::array<unsigned char, 0xC00> foreign{};
    const auto ticks_before = tick_originals; const auto seeds = caption_service.snapshot().counters().seeds;
    tick_observed(foreign.data(), 0);
    check(tick_originals == ticks_before + 1 && caption_service.snapshot().counters().seeds == seeds, "foreign window untouched");

    // 9. RN3 re-prefill on reopen: panel close hides the editor root with flag/epoch kept; a
    //    later show must re-prefill (fresh SetText) so Apply is not silently dropped.
    show(LE, false); tick_observed(le, 0);          // ensure hidden baseline
    show(LE, true); tick_observed(le, 0);           // first show -> prefill
    const auto reopen_before = set_text_calls;
    show(LE, false); tick_observed(le, 0);          // panel close (visible -> hidden)
    show(LE, true);  tick_observed(le, 0);          // reopen with a fresh widget -> re-prefill
    check(set_text_calls == reopen_before + 1, "editor re-prefills once after hide/show (no silent Apply drop)");

    // 10. R02/R04: a real visible->hidden transition clears BOTH readiness bits and bumps the
    //     generation (invalidates any in-flight transport bound to the old opening).
    show(LE, true); tick_observed(le, 0);
    check(lifecycle[0].prefilled, "leader prefilled before hide");
    const auto generation_before = lifecycle[0].generation;
    show(LE, false); tick_observed(le, 0);
    check(!lifecycle[0].attempted && !lifecycle[0].prefilled, "hide clears attempted AND prefilled");
    check(lifecycle[0].generation == generation_before + 1, "hide bumps the generation once");

    // 11. R05: the INVALID path (not a hide) keeps the inherited rule: no silent SetText retry
    //     in the same epoch; a new Open or Bind is required.
    show(LE, true); tick_observed(le, 0);
    const auto invalid_before = set_text_calls;
    put(le, 0x120, static_cast<void*>(nullptr)); tick_observed(le, 0);     // context->window broken
    put(le, 0x120, static_cast<void*>(le)); tick_observed(le, 0);          // valid again, same epoch
    check(set_text_calls == invalid_before && !lifecycle[0].prefilled, "invalid path never re-prefills");

    // 12. B02: a FAILED prefill never seeds the caption baseline.
    bind_observed(le);                                                     // caption + base reset
    check(!caption_service.snapshot().state(rn::caption::Field::leader).seeded, "bind cleared leader seed");
    put(w->focus_manager.data(), 0x230, static_cast<void*>(w->leader_edit.data() + 0x1A0));
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(1));     // our field holds a draft
    show(LE, false); tick_observed(le, 0); show(LE, true); tick_observed(le, 0);
    check(count(logs(), "prefill-focused-draft-preserved") >= 1, "prefill refused over a focused draft");
    check(!caption_service.snapshot().state(rn::caption::Field::leader).seeded, "failed prefill does not seed");

    // 12b. B02: prefill reached its end but the lifecycle changed => readiness NOT committed and the
    //      caption baseline NOT seeded (seed only after a committed prefill).
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(0));
    put(w->focus_manager.data(), 0x230, static_cast<void*>(nullptr));
    show(LE, false); tick_observed(le, 0);
    leader_lookups = 0; bump_generation_on_lookup = 3;      // edits[0], fresh_edit, rendered_edit
    show(LE, true); tick_observed(le, 0);
    bump_generation_on_lookup = 0;
    check(!lifecycle[0].prefilled, "lifecycle change at the end of prefill: readiness not committed");
    check(!caption_service.snapshot().state(rn::caption::Field::leader).seeded, "uncommitted prefill never seeds");
    // restore the focused-draft state expected by step 13 (field still unseeded)
    put(w->focus_manager.data(), 0x230, static_cast<void*>(w->leader_edit.data() + 0x1A0));
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(1));

    // 13. RN6 stale sentinel for an unseeded field + spoof protection: forwarded with ONLY the
    //     non-reserved originals and rn_ip_cmd_epoch=-1; spoofed reserved keys are removed.
    original_ctor = fake_ctor; original_dispatch = fake_dispatch_ctor;
    std::array<Record, 5> spoofed{spoof("user_key", 300000), spoof("rn_native_err", 500000), spoof("rn_nativeb_err", 600000),
                                  spoof("rn_ip_cmd_epoch", 700000), spoof("rn_native_ok", 100000)};
    TempVector source{spoofed.data(), 5, 5, nullptr};
    put(le_context, 0x470, source);
    const auto stale_before = dispatch_originals;
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(dispatch_originals == stale_before + 1 && ctor_count == 1, "stale Apply forwarded once through the ctor");
    check(captured_count("rn_ip_cmd_epoch") == 1 && captured_raw("rn_ip_cmd_epoch") == -100000, "sentinel epoch = -1");
    check(captured_count("user_key") == 1 && captured_count("rn_native_ok") == 0, "originals kept, no payload");
    check(captured_count("rn_native_err") == 0 && captured_count("rn_nativeb_err") == 0, "spoofed error keys removed");
    check(count(logs(), "stale-sentinel epoch=-1") == 1, "sentinel logged");

    // 14. PID3044 scenario: seeded field, but the reopened editor failed its re-prefill
    //     (prefilled=false) -> capture fails -> sentinel instead of a silent drop.
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(0));
    put(w->focus_manager.data(), 0x230, static_cast<void*>(nullptr));
    show(LE, false); tick_observed(le, 0); show(LE, true); tick_observed(le, 0);  // successful prefill seeds
    check(caption_service.snapshot().state(rn::caption::Field::leader).seeded && lifecycle[0].prefilled, "reopen re-prefilled");
    put(w->focus_manager.data(), 0x230, static_cast<void*>(w->leader_edit.data() + 0x1A0));
    put(w->leader_edit.data(), 0x271, static_cast<unsigned char>(1));
    show(LE, false); tick_observed(le, 0); show(LE, true); tick_observed(le, 0);  // re-prefill refused
    check(!lifecycle[0].prefilled && caption_service.snapshot().state(rn::caption::Field::leader).seeded, "seeded but not prefilled");
    const auto scenario_before = dispatch_originals;
    const auto dropped_before = count(logs(), "own-action-dropped-before-dispatch");
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(dispatch_originals == scenario_before + 1 && captured_raw("rn_ip_cmd_epoch") == -100000, "failed re-prefill -> sentinel");
    check(count(logs(), "own-action-dropped-before-dispatch") == dropped_before, "no silent drop");

    // 15. No sentinel when the own editor window is not live: the drop stays (logged).
    show(LE, false); tick_observed(le, 0);
    const auto hidden_before = dispatch_originals;
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(dispatch_originals == hidden_before && count(logs(), "own-action-dropped-before-dispatch") == dropped_before + 1,
          "hidden editor: logged drop, no forwarding");
    put(le_context, 0x470, TempVector{});

    // 15b. R5-MINOR-1: permanent caption states get distinct sentinels, not "stale" (-1):
    //      missing revision carrier -> epoch -2; counter at the 999 limit -> epoch -3.
    put(le_context, 0x470, TempVector{});
    show(LE, true);                                                   // live own editor (no tick needed)
    set_rev(LE, "");                                                  // carrier widget absent
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(captured_count("rn_ip_cmd_epoch") == 1 && captured_raw("rn_ip_cmd_epoch") == -200000, "no counter -> epoch -2");
    check(count(logs(), "stale-sentinel epoch=-2 reason=no-counter") == 1 && captured_count("rn_native_ok") == 0, "-2 logged, no payload");
    set_rev(LE, "999");                                               // seeded field at the limit
    dispatch_observed(le_context, const_cast<MsvcString*>(&leader_apply));
    check(captured_raw("rn_ip_cmd_epoch") == -300000 && count(logs(), "stale-sentinel epoch=-3 reason=counter-limit") == 1,
          "counter limit -> epoch -3");
    check(count(logs(), "stale-sentinel epoch=-1 reason=stale") == 2, "only the two real stale cases used -1");
    set_rev(LE, "1"); show(LE, false); tick_observed(le, 0);

    // 16. B03: an unreadable context fails CLOSED (gate false, no sentinel eligibility).
    void* noaccess = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    check(noaccess != nullptr && !caption_dispatch_allowed(noaccess, 0) && !stale_eligible(noaccess, 0), "unreadable context fails closed");

    // 17. RN6 error codes + spoof protection in the real transport builder: invalid A carries
    //     rn_native_err=InputError in its last slot (budget unchanged); a spoofed one is removed.
    std::array<Record, 2> spoofed_err{spoof("rn_native_err", 900000), spoof("rn_nativeb_err", 900000)};
    put(le_context, 0x470, TempVector{spoofed_err.data(), 2, 2, nullptr});
    {
        Origin current{}; current.context = le_context; current.model = 0; current.epoch = 7;
        InputSnapshot inputs[2]{}; inputs[0].size = 33;
        for (std::size_t i = 0; i < 33; ++i) inputs[0].points[i] = 0x61;
        check(prepare_transport(current, inputs), "transport built for invalid leader");
        captured.clear();
        for (int i = 0; i < current.view.count; ++i) {
            char key[129]{}; record_key(current.view.data[i], key); captured.push_back({key, current.view.data[i].raw_value});
        }
        check(captured_count("rn_native_err") == 1 && captured_raw("rn_native_err") == 200000, "too_long -> rn_native_err=2");
        check(captured_count("rn_native_31") == 0 && captured_count("rn_native_ok") == 1 && captured_raw("rn_native_ok") == 0, "slot 31 replaced, ok=0");
        check(current.view.count == 35 && captured_count("rn_nativeb_err") == 0, "leader 34 field records + epoch (unchanged), spoof removed");
        Origin empty{}; empty.context = le_context; empty.model = 0; empty.epoch = 7;
        InputSnapshot none[2]{};
        check(prepare_transport(empty, none), "transport built for empty leader");
        bool empty_code = false;
        for (int i = 0; i < empty.view.count; ++i) {
            char key[129]{}; record_key(empty.view.data[i], key);
            if (std::strcmp(key, "rn_native_err") == 0) empty_code = empty.view.data[i].raw_value == 100000;
        }
        check(empty_code, "empty -> rn_native_err=1");
        Origin valid{}; valid.context = le_context; valid.model = 0; valid.epoch = 7;
        InputSnapshot ok[2]{}; ok[0].size = 3; ok[0].points[0] = 'J'; ok[0].points[1] = 'a'; ok[0].points[2] = 'n';
        check(prepare_transport(valid, ok), "transport built for valid leader");
        bool has_err = false, has_31 = false;
        for (int i = 0; i < valid.view.count; ++i) {
            char key[129]{}; record_key(valid.view.data[i], key);
            has_err |= std::strcmp(key, "rn_native_err") == 0; has_31 |= std::strcmp(key, "rn_native_31") == 0;
        }
        check(!has_err && has_31, "valid field: no error record, slot 31 present");
    }
    put(le_context, 0x470, TempVector{});

    std::printf("caption refresh bridge integration: %u checks, %u failures\n", checks, failures);
    std::fflush(stdout);
    std::_Exit(failures ? 1 : 0);
}
