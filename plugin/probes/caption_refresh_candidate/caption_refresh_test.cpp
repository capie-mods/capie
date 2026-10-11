// Offline tests for the caption refresh candidate core. Fake memory only: no game
// process, no injection, no real native call. Optional static check of the pinned EXE
// file bytes (file read only) when the path argument exists.
#include "caption_refresh_core.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <fstream>
#include <future>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace rn::caption;
static int checks = 0, failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

struct FakeMem {
    std::map<uptr, std::vector<unsigned char>> regions;
    void add(uptr address, std::size_t size) { regions[address] = std::vector<unsigned char>(size, 0); }
    unsigned char* at(uptr address, std::size_t size) {
        auto it = regions.upper_bound(address);
        if (it == regions.begin()) return nullptr;
        --it;
        if (address - it->first + size > it->second.size()) return nullptr;
        return it->second.data() + (address - it->first);
    }
    bool read(uptr address, void* out, std::size_t size) const {
        auto it = regions.upper_bound(address);
        if (it == regions.begin()) return false;
        --it;
        if (address < it->first || address - it->first + size > it->second.size()) return false;
        std::memcpy(out, it->second.data() + (address - it->first), size);
        return true;
    }
    template <class T> void put(uptr address, T value) {
        auto* p = at(address, sizeof(T));
        if (!p) { std::printf("bad fake write %llx\n", static_cast<unsigned long long>(address)); std::abort(); }
        std::memcpy(p, &value, sizeof(T));
    }
};

constexpr uptr image = 0x140000000;
constexpr uptr fake_idler = 0x13000000, fake_gui = 0x14000000, fake_view = 0x15000000, politics_root = 0x16000000;
uptr window_addr(std::size_t m) { return 0x20000000 + m * 0x01000000; }
uptr model_addr(std::size_t m) { return window_addr(m) + 0x10000; }
uptr root_addr(std::size_t m) { return window_addr(m) + 0x20000; }
uptr name_addr(std::size_t m) { return window_addr(m) + 0x30000; }

struct World {
    FakeMem mem;
    World() {
        mem.add(image + rva::token_politics_tab, 4);
        mem.put<std::uint32_t>(image + rva::token_politics_tab, token_politics_tab_value);
        mem.add(image + rva::token_player_context, 4);
        mem.put<std::uint32_t>(image + rva::token_player_context, token_player_context_value);
        for (const auto& pin : slot_pins) {
            mem.add(image + pin.vtable + pin.slot, 8);
            mem.put<uptr>(image + pin.vtable + pin.slot, image + pin.target);
        }
        mem.add(fake_idler, 0x700); mem.put<uptr>(fake_idler, image + rva::vt_ingame_idler); mem.put<uptr>(fake_idler + off::idler_gui, fake_gui);
        mem.add(fake_gui, 0x200); mem.put<uptr>(fake_gui, image + rva::vt_game_gui); mem.put<uptr>(fake_gui + off::gui_idler, fake_idler);
        mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, fake_view);
        mem.add(fake_view, 0x600); mem.put<uptr>(fake_view, image + rva::vt_politics_view);
        mem.put<uptr>(fake_view + off::view_observer, image + rva::vt_politics_observer);
        mem.put<uptr>(fake_view + off::view_gui, fake_gui); mem.put<uptr>(fake_view + off::view_idler, fake_idler);
        mem.put<uptr>(fake_view + off::view_root, politics_root);
        mem.add(politics_root, 0x100);
        for (std::size_t m = 0; m < model_count; ++m) add_window(m);
    }
    void add_window(std::size_t m) {
        const uptr w = window_addr(m), model = model_addr(m), root = root_addr(m), name = name_addr(m);
        mem.add(w, 0xC00); mem.add(model, 0x100); mem.add(root, 0x100); mem.add(name, 64);
        mem.put<uptr>(w + off::window_model, model); mem.put<uptr>(w + off::window_idler, fake_idler);
        mem.put<uptr>(w + off::window_root, root); mem.put<uptr>(w + off::window_parent, politics_root);
        mem.put<uptr>(w + off::window_parent_sg, 0);
        mem.put<uptr>(w + off::window_context, image + rva::vt_sg_context);
        mem.put<uptr>(w + off::window_context + off::context_window, w);
        mem.put<uptr>(w + off::window_context + off::context_model, model);
        mem.put<uptr>(w + off::window_context + off::context_root, root);
        mem.put<uptr>(model, image + rva::vt_sg_model);
        const char* n = model_specs[m].name; const auto len = std::strlen(n);
        std::memcpy(mem.at(name, len), n, len);
        mem.put<uptr>(model + off::model_name, name);  // >15 chars => heap pointer layout
        mem.put<u64>(model + off::model_name + 0x10, len); mem.put<u64>(model + off::model_name + 0x18, len);
        mem.put<std::uint32_t>(model + off::model_context_token, token_player_context_value);
        mem.put<std::uint32_t>(model + off::model_parent_token, token_politics_tab_value);
        mem.put<uptr>(root, image + rva::vt_sg_root);
        show(m, false);
    }
    void show(std::size_t m, bool visible) {
        mem.put<unsigned char>(root_addr(m) + off::root_flags, visible ? 8 : 0);
        mem.put<unsigned char>(window_addr(m) + off::window_desired, visible ? 1 : 0);
    }
    unsigned char dirty() { unsigned char d = 0; mem.read(fake_view + off::view_dirty, &d, 1); return d; }
    void consume() { mem.put<unsigned char>(fake_view + off::view_dirty, 0); }  // native frame loop B68C60
};

enum class CallMode { mark, fault, no_mark, swap_view, unmark_rev };
struct TestEnv {
    World* world{};
    std::map<std::pair<uptr, std::string>, std::string> carriers;
    bool authority = true;
    CallMode mode = CallMode::mark;
    int calls = 0, bad_args = 0;
    std::vector<std::string> lines;
    std::function<void()> during_call;
    const FakeMem& mem() const { return world->mem; }
    uptr image() const { return ::image; }
    bool carrier(uptr root, const char* name, char* out, std::size_t cap, std::size_t& size) {
        auto it = carriers.find({root, name});
        if (it == carriers.end() || it->second.size() > cap) return false;
        std::memcpy(out, it->second.data(), it->second.size()); size = it->second.size(); return true;
    }
    bool call_refresh(uptr g, int index) {
        ++calls;
        if (g != fake_gui || index != 12) ++bad_args;
        if (during_call) during_call();
        switch (mode) {
        case CallMode::fault: return false;
        case CallMode::no_mark: return true;
        case CallMode::swap_view: world->mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, 0); return true;
        default: world->mem.put<unsigned char>(fake_view + off::view_dirty, 1); return true;
        }
    }
    bool authority_proven() const { return authority; }
    void log(const char* line) { lines.emplace_back(line); }
    void rev(std::size_t m, const std::string& value) { carriers[{root_addr(m), model_specs[m].carrier}] = value; }
    void rev_field(Field f, const std::string& value) {
        for (std::size_t m = 0; m < model_count; ++m) if (model_specs[m].field == f) rev(m, value);
    }
};

constexpr std::size_t LE = 0, LL = 1, PE = 2, PL = 3;  // leader editor/launcher, party editor/launcher
struct Fixture {
    World world; TestEnv env; Service<TestEnv> svc{env};
    Fixture() { env.world = &world; }
    void tick(std::size_t m) { svc.on_tick(window_addr(m)); }
    void ticks(std::size_t m, int n) { for (int i = 0; i < n; ++i) tick(m); }
    Tracker t() { return svc.snapshot(); }
};

void test_parse() {
    u64 v = 99;
    CHECK(parse_revision("0", 1, v) && v == 0);
    CHECK(parse_revision("7", 1, v) && v == 7);
    CHECK(parse_revision("12345678901234", 14, v) && v == 12345678901234ull);
    CHECK(!parse_revision("123456789012345", 15, v));
    CHECK(!parse_revision("", 0, v));
    CHECK(!parse_revision("-1", 2, v));
    CHECK(!parse_revision("1.0", 3, v));
    CHECK(!parse_revision("1.5", 3, v));
    CHECK(!parse_revision("01", 2, v));
    CHECK(!parse_revision("1K", 2, v));
    CHECK(!parse_revision("1 000", 5, v));
    CHECK(!parse_revision("1234567890123456", 16, v));
    CHECK(!parse_revision("999999999999999", 15, v));  // > max_revision headroom
    CHECK(parse_revision("92233720368546", 14, v) && v == max_revision);
    CHECK(!parse_revision("92233720368547", 14, v));
    CHECK(!parse_revision("[?rn_ip_lui_rev]", 16, v));
    CHECK(!parse_revision(nullptr, 1, v));
}

void test_first_apply_without_launcher_tick() {
    Fixture f;
    f.env.rev_field(Field::leader, "0");
    f.world.show(LE, true); f.tick(LE);                       // Open/prefill seeds 0
    CHECK(f.t().state(Field::leader).seeded && f.t().state(Field::leader).acked == 0);
    CHECK(f.svc.dispatch_allowed(Field::leader, 0));
    f.env.rev_field(Field::leader, "1");                      // script success counter after Apply
    f.world.show(LE, false); f.tick(LE);                      // editor closes; no launcher tick yet
    CHECK(f.env.calls == 0);
    f.world.show(LL, true); f.tick(LL);                       // first launcher tick ever reads 1
    CHECK(f.env.calls == 1 && f.env.bad_args == 0);
    CHECK(f.world.dirty() == 1);
    CHECK(f.t().state(Field::leader).acked == 1 && !f.t().state(Field::leader).pending);
    CHECK(f.t().counters().acks == 1);
    f.world.consume(); f.ticks(LL, 50);                       // no per-frame repeat
    CHECK(f.env.calls == 1);
}

void test_first_apply_editor_stays_visible() {
    Fixture f; f.env.rev_field(Field::party, "0");
    f.world.show(PE, true); f.tick(PE);
    f.env.rev_field(Field::party, "1"); f.tick(PE);
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 1);
}

void test_saved_seven_to_eight() {
    Fixture f; f.env.rev_field(Field::party, "7");
    f.world.show(PE, true); f.tick(PE);
    CHECK(f.t().state(Field::party).acked == 7 && f.env.calls == 0);
    f.env.rev_field(Field::party, "8"); f.world.show(PE, false); f.tick(PE); f.world.show(PL, true); f.tick(PL);
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 8);
}

void test_launcher_unseeded_never_baselines() {
    Fixture f; f.env.rev_field(Field::leader, "1");
    f.world.show(LL, true); f.ticks(LL, 5);
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().observations_ignored_unseeded == 5);
    CHECK(!f.svc.dispatch_allowed(Field::leader, 1));
    f.env.rev_field(Field::leader, "0");  // editor opens on a different (true) value -> seed 0
    f.world.show(LL, false); f.tick(LL); f.world.show(LE, true); f.tick(LE);
    CHECK(f.t().state(Field::leader).acked == 0);
    f.env.rev_field(Field::leader, "1"); f.world.show(LE, false); f.tick(LE); f.world.show(LL, true); f.tick(LL);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);  // launcher 1 did not mask seed 0
}

void test_dispatch_gate() {
    Fixture f;
    CHECK(!f.svc.dispatch_allowed(Field::leader, 0));  // unseeded dispatch rejected
    f.env.rev_field(Field::leader, "3"); f.world.show(LE, true); f.tick(LE);
    CHECK(f.svc.dispatch_allowed(Field::leader, 3));
    CHECK(!f.svc.dispatch_allowed(Field::leader, 4));  // unobserved jump
    CHECK(!f.svc.dispatch_allowed(Field::party, 3));
    Tracker t; t.seed(Field::leader, max_revision);
    CHECK(!t.dispatch_allowed(Field::leader, max_revision));  // no headroom
    CHECK(t.seed(Field::party, max_revision + 1) == Tracker::Seed::refused_bad);
}

void test_rapid_reopen_keeps_pending() {
    Fixture f; f.env.authority = false;  // block calls to observe the pending state
    f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1");
    f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.tick(LE);  // reopen seeds again with 1
    CHECK(f.t().state(Field::leader).pending && f.t().state(Field::leader).acked == 0 && f.t().state(Field::leader).observed == 1);
    CHECK(f.env.calls == 0);
    f.env.authority = true; f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

void test_authority_open_zero_calls() {
    Fixture f; f.env.authority = false;
    f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1"); f.ticks(LE, 100);
    CHECK(f.env.calls == 0);
    CHECK(f.t().counters().precall_rejects == 1);  // one deferred attempt, not per frame
    CHECK(f.t().state(Field::leader).pending);
}

void test_hidden_preserves_and_no_call_while_hidden() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1"); f.world.show(LE, false);
    f.ticks(LE, 10); f.ticks(LL, 10);  // all hidden (politics closed)
    CHECK(f.env.calls == 0 && f.t().state(Field::leader).seeded && f.t().state(Field::leader).observed == 0);
    f.world.show(LL, true); f.tick(LL);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

void test_coalesced_jump_and_both_fields() {
    Fixture f; f.env.authority = false;
    f.env.rev_field(Field::leader, "0"); f.env.rev_field(Field::party, "4");
    f.world.show(LE, true); f.tick(LE); f.world.show(LE, false); f.tick(LE);
    f.world.show(PE, true); f.tick(PE); f.world.show(PE, false); f.tick(PE);
    f.env.rev_field(Field::leader, "3"); f.env.rev_field(Field::party, "5");
    f.env.authority = true;
    f.world.show(PL, true); f.tick(PL);  // party launcher sees 5; leader 3 not yet observed
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 5 && f.t().state(Field::leader).acked == 0);
    f.world.consume(); f.world.show(LL, true); f.tick(LL);  // leader jump 0->3 = one refresh
    CHECK(f.env.calls == 2 && f.t().state(Field::leader).acked == 3);
    f.ticks(LL, 20); f.ticks(PL, 20);
    CHECK(f.env.calls == 2);
}

void test_failures_no_retry() {
    for (CallMode mode : {CallMode::fault, CallMode::no_mark, CallMode::swap_view}) {
        Fixture f; f.env.mode = mode;
        f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
        f.env.rev_field(Field::leader, "1"); f.ticks(LE, 30);
        CHECK(f.env.calls == 1);
        CHECK(f.t().counters().failures == 1 && f.t().counters().acks == 0);
        CHECK(f.t().state(Field::leader).suspended && f.t().state(Field::leader).acked == 0);
        f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.ticks(LE, 5);  // visibility edge: still suspended
        CHECK(f.env.calls == 1);
        if (mode == CallMode::swap_view) f.world.mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, fake_view);
        f.env.mode = CallMode::mark; f.env.rev_field(Field::leader, "2"); f.ticks(LE, 5);  // new success => one new attempt
        CHECK(f.env.calls == 2 && f.t().state(Field::leader).acked == 2);
    }
}

void test_readback_revision_rollback_after_call() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1");
    f.env.during_call = [&] { f.env.rev_field(Field::leader, "0"); };
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().failures == 1 && f.t().counters().acks == 0);
}

void test_rollback_bind_invalid() {
    Fixture f; f.env.authority = false;
    f.env.rev_field(Field::leader, "5"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "4"); f.tick(LE);  // rollback
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().rollbacks == 1);
    f.ticks(LE, 3);
    CHECK(!f.t().state(Field::leader).seeded);  // same opening may not reseed
    CHECK(!f.svc.dispatch_allowed(Field::leader, 4));
    f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.tick(LE);  // next Open seeds
    CHECK(f.t().state(Field::leader).seeded && f.t().state(Field::leader).acked == 4);
    f.env.rev_field(Field::leader, "5"); f.tick(LE);
    CHECK(f.t().state(Field::leader).pending);
    f.svc.on_bind(window_addr(LL));  // new bind drops pending and baseline
    CHECK(!f.t().state(Field::leader).seeded && !f.t().state(Field::leader).pending && f.t().counters().binds == 1);
    f.env.authority = true; f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.tick(LE);
    CHECK(f.env.calls == 0 && f.t().state(Field::leader).acked == 5);  // load-time value adopted, no refresh
    // invalid context resets
    f.world.mem.put<uptr>(window_addr(LE) + off::window_context + off::context_window, 0);
    f.tick(LE);
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().invalid_resets >= 1);
}

void test_postcall_identity_swap() {
    // A fully valid but different politics view after the call must not be acknowledged.
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    constexpr uptr other_view = 0x17000000;
    f.world.mem.add(other_view, 0x600);
    std::memcpy(f.world.mem.at(other_view, 0x600), f.world.mem.at(fake_view, 0x600), 0x600);
    f.world.mem.put<unsigned char>(other_view + off::view_dirty, 1);
    f.env.rev_field(Field::leader, "1");
    f.env.during_call = [&] { f.world.mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, other_view); };
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().failures == 1 && f.t().counters().acks == 0);
    bool logged = false;
    for (const auto& l : f.env.lines) if (l.find("postcall-identity") != std::string::npos) logged = true;
    CHECK(logged);
}

void test_bind_during_inflight_is_stale() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1");
    f.env.during_call = [&] { f.svc.on_bind(window_addr(LE)); };
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().acks == 0 && f.t().counters().stale_results == 1);
    CHECK(!f.t().state(Field::leader).seeded);
}

void test_no_lock_across_native_call() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.env.rev_field(Field::leader, "1");
    bool other_thread_progressed = false;
    f.env.during_call = [&] {
        auto task = std::async(std::launch::async, [&] { return f.svc.dispatch_allowed(Field::leader, 1); });
        other_thread_progressed = task.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        if (other_thread_progressed) task.get();
    };
    f.tick(LE);
    CHECK(f.env.calls == 1 && other_thread_progressed);
}

void test_guard_rejections() {
    struct Mutation { const char* name; std::function<void(World&)> apply; Reject expected; };
    const std::vector<Mutation> mutations = {
        {"model ctx token", [](World& w) { w.mem.put<std::uint32_t>(model_addr(LE) + off::model_context_token, 0x3A71); }, Reject::context_token},
        {"parent token", [](World& w) { w.mem.put<std::uint32_t>(model_addr(LE) + off::model_parent_token, 0x3A94); }, Reject::parent_token},
        {"parent_window_window", [](World& w) { w.mem.put<u64>(model_addr(LE) + off::model_parent_window_len, 3); }, Reject::parent_override},
        {"parent_scripted_gui", [](World& w) { w.mem.put<u64>(model_addr(LE) + off::model_parent_sg_len, 3); }, Reject::parent_override},
        {"window+108", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_parent_sg, 0x1234); }, Reject::parent_sg_window},
        {"idler null", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_idler, 0); }, Reject::idler_null},
        {"idler vt", [](World& w) { w.mem.put<uptr>(fake_idler, image + 0x1000); }, Reject::idler_vt},
        {"idler slot", [](World& w) { w.mem.put<uptr>(image + rva::vt_ingame_idler + off::slot_idler_gui, image + 0x1); }, Reject::idler_slot},
        {"gui null", [](World& w) { w.mem.put<uptr>(fake_idler + off::idler_gui, 0); }, Reject::gui_null},
        {"gui vt", [](World& w) { w.mem.put<uptr>(fake_gui, image + 0x2); }, Reject::gui_vt},
        {"gui idler", [](World& w) { w.mem.put<uptr>(fake_gui + off::gui_idler, fake_idler + 8); }, Reject::gui_idler},
        {"view null", [](World& w) { w.mem.put<uptr>(fake_gui + off::gui_views + 96, 0); }, Reject::view_null},
        {"view vt", [](World& w) { w.mem.put<uptr>(fake_view, image + 0x3); }, Reject::view_vt},
        {"observer vt", [](World& w) { w.mem.put<uptr>(fake_view + off::view_observer, image + 0x4); }, Reject::observer_vt},
        {"view slot28", [](World& w) { w.mem.put<uptr>(image + rva::vt_politics_view + 0x28, image + 0x5); }, Reject::view_slots},
        {"observer slot48", [](World& w) { w.mem.put<uptr>(image + rva::vt_politics_observer + 0x48, image + 0x6); }, Reject::view_slots},
        {"view gui", [](World& w) { w.mem.put<uptr>(fake_view + off::view_gui, fake_gui + 8); }, Reject::view_gui},
        {"view idler", [](World& w) { w.mem.put<uptr>(fake_view + off::view_idler, 0); }, Reject::view_idler},
        {"root null", [](World& w) { w.mem.put<uptr>(fake_view + off::view_root, 0); }, Reject::root_null},
        {"parent != root", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_parent, politics_root + 0x10); }, Reject::parent_root},
        {"dirty garbage", [](World& w) { w.mem.put<unsigned char>(fake_view + off::view_dirty, 7); }, Reject::dirty_read},
        {"token image", [](World& w) { w.mem.put<std::uint32_t>(image + rva::token_politics_tab, 0x3AB3); }, Reject::token_image},
        {"unreadable model", [](World& w) { w.mem.regions.erase(model_addr(LE)); }, Reject::window_read},
    };
    for (const auto& m : mutations) {
        World w; Target t{};
        CHECK(resolve(w.mem, image, window_addr(LE), t) == Reject::none && t.gui == fake_gui && t.view == fake_view && t.dirty == 0);
        m.apply(w);
        const auto r = resolve(w.mem, image, window_addr(LE), t);
        if (r != m.expected) std::printf("  mutation %s -> %s\n", m.name, reject_name(r));
        CHECK(r == m.expected);
        CHECK(t.gui == 0 && t.view == 0);
    }
    // Service level: a guard reject never calls native and is deferred to the next visibility edge.
    Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, 0);
    f.env.rev_field(Field::leader, "1"); f.ticks(LE, 40);
    CHECK(f.env.calls == 0 && f.t().counters().precall_rejects == 1);
    f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.ticks(LE, 40);
    CHECK(f.env.calls == 0 && f.t().counters().precall_rejects == 2);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, fake_idler);
    f.world.show(LE, false); f.tick(LE); f.world.show(LE, true); f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

void test_foreign_and_malformed() {
    Fixture f;
    // Foreign model name: unrelated, no state.
    const char other[] = "rn_ip_country_gui______";
    std::memcpy(f.world.mem.at(name_addr(LE), sizeof(other) - 1), other, sizeof(other) - 1);
    f.world.mem.put<u64>(model_addr(LE) + off::model_name + 0x10, sizeof(other) - 1);
    f.world.mem.put<u64>(model_addr(LE) + off::model_name + 0x18, sizeof(other) - 1);
    f.env.rev_field(Field::leader, "0"); f.world.show(LE, true); f.tick(LE);
    CHECK(!f.t().state(Field::leader).seeded);
    Fixture g;
    for (const char* bad : {"", "1.0", "-1", "abc", "01", "1K"}) {
        g.env.rev_field(Field::leader, bad); g.world.show(LE, true); g.tick(LE);
        CHECK(!g.t().state(Field::leader).seeded && !g.svc.dispatch_allowed(Field::leader, 0));
    }
    g.env.carriers.clear(); g.tick(LE);  // missing carrier
    CHECK(!g.t().state(Field::leader).seeded && g.env.calls == 0);
    std::size_t carrier_logs = 0;
    for (const auto& l : g.env.lines) if (l.find("revision-carrier-rejected") != std::string::npos) ++carrier_logs;
    CHECK(carrier_logs == 1);  // bounded diagnostic
}

void test_forward_exactly_once() {
    int original = 0, observed = 0;
    auto orig = [&](int) { ++original; };
    forward_then_observe(orig, [&](int) { ++observed; }, true, 1);
    forward_then_observe(orig, [&](int) { ++observed; }, false, 1);
    CHECK(original == 2 && observed == 1);
}

void test_fingerprint_table() {
    for (const auto& fp : fingerprints) {
        unsigned char bytes[128]{};
        CHECK(decode_hex(fp.hex, bytes, sizeof(bytes)) > 0);
    }
    unsigned char b[2]{};
    CHECK(decode_hex("zz", b, 2) == 0 && decode_hex("abc", b, 2) == 0 && decode_hex("aabbcc", b, 2) == 0);
    static_assert(fingerprint_count == 23);
}

// Static file check of the pinned EXE (no process access). Skipped if absent.
int exe_check(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::printf("EXE check SKIPPED (not found)\n"); return 0; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, d.data() + o, 2); return v; };
    const auto e = u32(0x3C); const auto sections = u16(e + 6); const auto opt = u16(e + 20);
    u64 base = 0; std::memcpy(&base, d.data() + e + 24 + 24, 8);
    CHECK(base == 0x140000000ull);
    struct Sec { char name[9]; std::uint32_t va, vsize, raw, rawsize; };
    std::vector<Sec> secs;
    for (unsigned i = 0; i < sections; ++i) {
        const auto o = e + 24 + opt + i * 40; Sec s{}; std::memcpy(s.name, d.data() + o, 8);
        s.vsize = u32(o + 8); s.va = u32(o + 12); s.rawsize = u32(o + 16); s.raw = u32(o + 20); secs.push_back(s);
    }
    auto map = [&](uptr r) -> const unsigned char* {
        for (auto& s : secs) if (r >= s.va && r < s.va + s.rawsize) return d.data() + s.raw + (r - s.va);
        return nullptr;
    };
    const Sec* text = nullptr; for (auto& s : secs) if (std::strcmp(s.name, ".text") == 0) text = &s;
    CHECK(text != nullptr); if (!text) return 1;
    const unsigned char* t = d.data() + text->raw; const std::size_t tsize = std::min(text->vsize, text->rawsize);
    int unique = 0;
    for (const auto& fp : fingerprints) {
        unsigned char bytes[128]{}; const auto n = decode_hex(fp.hex, bytes, sizeof(bytes));
        std::size_t count = 0; uptr where = 0;
        for (std::size_t i = 0; i + n <= tsize; ++i)
            if (t[i] == bytes[0] && std::memcmp(t + i, bytes, n) == 0) { ++count; where = i + text->va; }
        const bool ok = count == 1 && where == fp.rva;
        CHECK(ok); unique += ok;
        std::printf("  FP %07llX %3zuB matches=%zu %s\n", static_cast<unsigned long long>(fp.rva), n, count, ok ? "OK" : "BAD");
    }
    for (const auto& pin : slot_pins) {
        u64 v = 0; const auto* p = map(pin.vtable + pin.slot); if (p) std::memcpy(&v, p, 8);
        CHECK(v == 0x140000000ull + pin.target);
    }
    std::uint32_t a = 0, b = 0;
    if (auto* p = map(rva::token_politics_tab)) std::memcpy(&a, p, 4);
    if (auto* p = map(rva::token_player_context)) std::memcpy(&b, p, 4);
    CHECK(a == token_politics_tab_value && b == token_player_context_value);
    std::printf("EXE check: %d/%zu fingerprints unique at RVA; slot pins and tokens checked\n", unique, fingerprint_count);
    return 0;
}

int main(int argc, char** argv) {
    test_parse();
    test_first_apply_without_launcher_tick();
    test_first_apply_editor_stays_visible();
    test_saved_seven_to_eight();
    test_launcher_unseeded_never_baselines();
    test_dispatch_gate();
    test_rapid_reopen_keeps_pending();
    test_authority_open_zero_calls();
    test_hidden_preserves_and_no_call_while_hidden();
    test_coalesced_jump_and_both_fields();
    test_failures_no_retry();
    test_readback_revision_rollback_after_call();
    test_rollback_bind_invalid();
    test_postcall_identity_swap();
    test_bind_during_inflight_is_stale();
    test_no_lock_across_native_call();
    test_guard_rejections();
    test_foreign_and_malformed();
    test_forward_exactly_once();
    test_fingerprint_table();
    if (argc > 1) exe_check(argv[1]);
    std::printf("caption refresh tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
