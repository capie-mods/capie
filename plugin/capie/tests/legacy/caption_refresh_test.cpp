// Offline tests for the caption refresh candidate core. Fake memory only: no game
// process, no injection, no real native call. Optional static check of the pinned EXE
// file bytes (file read only) when the path argument exists.
#include "../compat/rn_caption_focus.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
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
constexpr uptr fake_manager = 0x18000000;
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
        mem.add(fake_gui, 0x200); mem.put<uptr>(fake_gui + off::gui_idler, fake_idler);  // word 0 is data (non-polymorphic container)
        mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, fake_view);
        mem.add(fake_view, 0x600); mem.put<uptr>(fake_view, image + rva::vt_politics_view);
        mem.put<uptr>(fake_view + off::view_observer, image + rva::vt_politics_observer);
        mem.put<uptr>(fake_view + off::view_gui, fake_gui); mem.put<uptr>(fake_view + off::view_idler, fake_idler);
        mem.put<uptr>(fake_view + off::view_root, politics_root);
        mem.add(politics_root, 0x100);
        mem.add(fake_manager, 0x100); mem.put<uptr>(fake_manager + off::manager_idler, fake_idler);
        for (std::size_t m = 0; m < model_count; ++m) add_window(m);
    }
    void add_window(std::size_t m) {
        const uptr w = window_addr(m), model = model_addr(m), root = root_addr(m), name = name_addr(m);
        mem.add(w, 0xC00); mem.add(model, 0x100); mem.add(root, 0x100); mem.add(name, 64);
        mem.put<uptr>(w + off::window_model, model); mem.put<uptr>(w + off::window_idler, fake_idler);
        mem.put<uptr>(w + off::window_manager, fake_manager);
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
    void flags(std::size_t m, unsigned char desired, unsigned char root_flags) {
        mem.put<unsigned char>(root_addr(m) + off::root_flags, root_flags);
        mem.put<unsigned char>(window_addr(m) + off::window_desired, desired);
    }
    void show(std::size_t m, bool visible) { flags(m, visible ? 1 : 0, visible ? 8 : 0); }
    unsigned char dirty() { unsigned char d = 0; mem.read(fake_view + off::view_dirty, &d, 1); return d; }
    void consume() { mem.put<unsigned char>(fake_view + off::view_dirty, 0); }  // native frame loop B68C60
};

enum class CallMode { mark, fault, no_mark, swap_view };
struct TestEnv {
    World* world{};
    std::map<std::pair<uptr, std::string>, std::string> carriers;
    bool armed = true;
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
    bool refresh_armed() const { return armed; }
    void log(const char* line) { lines.emplace_back(line); }
    void rev(std::size_t m, const std::string& value) { carriers[{root_addr(m), model_specs[m].carrier}] = value; }
    void rev_field(Field f, const std::string& value) {
        for (std::size_t m = 0; m < model_count; ++m) if (model_specs[m].field == f) rev(m, value);
    }
    std::size_t logged(const char* detail) const {
        std::size_t n = 0;
        for (const auto& l : lines) if (l.find(detail) != std::string::npos) ++n;
        return n;
    }
};

// Runs fn on another thread; returns false on timeout instead of hanging. A timed-out
// thread is detached (its captured state must outlive the process: callers leak it).
bool finishes_within(std::function<void()> fn, int milliseconds) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([fn, done] { fn(); done->store(true); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!done->load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (done->load()) { worker.join(); return true; }
    worker.detach();
    return false;
}

constexpr std::size_t LE = 0, LL = 1, PE = 2, PL = 3;  // leader editor/launcher, party editor/launcher
struct Fixture {
    World world; TestEnv env; Service<TestEnv> svc{env};
    Fixture() { env.world = &world; }
    void tick(std::size_t m) { svc.on_tick(window_addr(m)); }
    void ticks(std::size_t m, int n) { for (int i = 0; i < n; ++i) tick(m); }
    // Integrated opening: editor becomes visible, its first tick runs, then the bridge's
    // one-shot prefill seeds (the only baseline source).
    Tracker::Seed open(std::size_t editor) { world.show(editor, true); tick(editor); return svc.on_prefill(window_addr(editor)); }
    void close(std::size_t m) { world.show(m, false); tick(m); }
    bool apply_gate(std::size_t editor) { return svc.on_dispatch(window_addr(editor)); }
    Tracker t() { return svc.snapshot(); }
    u64 attempts() { return t().counters().native_calls; }
};

void test_parse() {
    u64 v = 99;
    CHECK(parse_revision("0", 1, v) && v == 0);
    CHECK(parse_revision("7", 1, v) && v == 7);
    CHECK(parse_revision("999", 3, v) && v == 999);
    CHECK(!parse_revision("1000", 4, v));
    CHECK(!parse_revision("", 0, v));
    CHECK(!parse_revision("-1", 2, v));
    CHECK(!parse_revision("1.0", 3, v));
    CHECK(!parse_revision("1.5", 3, v));
    CHECK(!parse_revision("01", 2, v));
    CHECK(!parse_revision("1K", 2, v));
    CHECK(!parse_revision("1 0", 3, v));
    CHECK(!parse_revision("1,000", 5, v));
    CHECK(!parse_revision("[?rn_ip_lui_rev]", 16, v));
    CHECK(!parse_revision(nullptr, 1, v));
    CHECK(v == 999);  // failed parses never write
}

// MAJOR-1: the baseline comes from prefill only; Apply before a successful seed is rejected.
void test_seed_only_at_prefill_and_apply_gate() {
    Fixture f; f.env.rev_field(Field::leader, "0");
    f.world.show(LE, true); f.ticks(LE, 5);                   // visible ticks alone never seed
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().observations_ignored_unseeded == 5);
    CHECK(!f.apply_gate(LE));                                 // Apply before seed: rejected
    CHECK(f.svc.on_prefill(window_addr(LE)) == Tracker::Seed::seeded);
    CHECK(f.apply_gate(LE));
    // Carrier not ready at prefill: the opening stays unseeded and Apply is blocked even if
    // the carrier becomes readable later in the same opening (prefill is one-shot).
    Fixture g; g.env.rev_field(Field::party, "[?rn_ip_pui_rev]");
    CHECK(g.open(PE) == Tracker::Seed::refused_bad);
    CHECK(!g.apply_gate(PE));
    g.env.rev_field(Field::party, "0"); g.ticks(PE, 5);
    CHECK(!g.apply_gate(PE) && !g.t().state(Field::party).seeded);
    CHECK(g.t().counters().dispatch_rejected == 2 && g.t().counters().seed_refusals == 1);
    CHECK(g.env.logged("seed-carrier-rejected-apply-blocked") == 1 && g.env.logged("apply-rejected-no-counter") == 1 && g.env.logged("apply-rejected-stale") == 1);
    g.close(PE);
    CHECK(g.open(PE) == Tracker::Seed::seeded && g.apply_gate(PE));  // next opening seeds 0
    // Launcher/hidden/foreign windows cannot seed.
    Fixture h; h.env.rev_field(Field::leader, "0");
    h.world.show(LL, true); h.tick(LL);
    CHECK(h.svc.on_prefill(window_addr(LL)) == Tracker::Seed::refused_bad);
    CHECK(h.svc.on_prefill(window_addr(LE)) == Tracker::Seed::refused_bad);  // editor hidden
    CHECK(!h.t().state(Field::leader).seeded);
    CHECK(!h.apply_gate(LL));  // launcher is never an Apply origin
}

void test_first_apply_without_launcher_tick() {
    Fixture f; f.env.rev_field(Field::leader, "0");
    CHECK(f.open(LE) == Tracker::Seed::seeded && f.apply_gate(LE));
    f.env.rev_field(Field::leader, "1");                      // script success counter after Apply
    f.close(LE);                                              // editor closes; no launcher tick yet
    CHECK(f.env.calls == 0);
    f.world.show(LL, true); f.tick(LL);                       // first launcher tick ever reads 1
    CHECK(f.env.calls == 1 && f.env.bad_args == 0 && f.attempts() == 1);
    CHECK(f.world.dirty() == 1);
    CHECK(f.t().state(Field::leader).acked == 1 && !f.t().state(Field::leader).pending);
    CHECK(f.t().counters().acks == 1);
    f.world.consume(); f.ticks(LL, 50);                       // no per-frame repeat
    CHECK(f.env.calls == 1 && f.attempts() == 1);
}

void test_first_apply_editor_stays_visible() {
    Fixture f; f.env.rev_field(Field::party, "0");
    f.open(PE);
    f.env.rev_field(Field::party, "1"); f.tick(PE);
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 1 && f.attempts() == 1);
}

void test_saved_seven_to_eight() {
    Fixture f; f.env.rev_field(Field::party, "7");
    f.open(PE);
    CHECK(f.t().state(Field::party).acked == 7 && f.env.calls == 0);
    f.env.rev_field(Field::party, "8"); f.close(PE); f.world.show(PL, true); f.tick(PL);
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 8);
}

void test_launcher_unseeded_never_baselines() {
    Fixture f; f.env.rev_field(Field::leader, "1");
    f.world.show(LL, true); f.ticks(LL, 5);
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().observations_ignored_unseeded == 5);
    CHECK(!f.svc.dispatch_allowed(Field::leader, 1));
    f.env.rev_field(Field::leader, "0");
    f.close(LL); f.open(LE);
    CHECK(f.t().state(Field::leader).acked == 0);
    f.env.rev_field(Field::leader, "1"); f.close(LE); f.world.show(LL, true); f.tick(LL);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);  // launcher 1 did not mask seed 0
}

void test_dispatch_gate() {
    Fixture f;
    CHECK(!f.svc.dispatch_allowed(Field::leader, 0));
    f.env.rev_field(Field::leader, "3"); f.open(LE);
    CHECK(f.svc.dispatch_allowed(Field::leader, 3));
    CHECK(!f.svc.dispatch_allowed(Field::leader, 4));  // unobserved jump
    CHECK(!f.svc.dispatch_allowed(Field::party, 3));
    f.env.rev_field(Field::leader, "4"); CHECK(!f.apply_gate(LE));  // carrier jumped before observation
    Tracker t; t.seed(Field::leader, max_revision);
    CHECK(!t.dispatch_allowed(Field::leader, max_revision));  // no headroom
    t.seed(Field::party, max_revision - 1);
    CHECK(t.dispatch_allowed(Field::party, max_revision - 1));
    CHECK(t.seed(Field::party, max_revision + 1) == Tracker::Seed::refused_bad);
}

void test_rapid_reopen_keeps_pending() {
    Fixture f; f.env.armed = false;
    f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1");
    f.close(LE);
    CHECK(f.open(LE) == Tracker::Seed::observed);              // reopen prefill keeps the seed
    CHECK(f.t().state(Field::leader).pending && f.t().state(Field::leader).acked == 0 && f.t().state(Field::leader).observed == 1);
    CHECK(f.env.calls == 0 && f.apply_gate(LE));
    f.env.armed = true; f.close(LE); f.open(LE);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

void test_disarmed_zero_calls() {
    Fixture f; f.env.armed = false;
    f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1"); f.ticks(LE, 100);
    CHECK(f.env.calls == 0 && f.attempts() == 0);
    CHECK(f.t().counters().precall_rejects == 1);
    CHECK(f.t().state(Field::leader).pending);
}

void test_hidden_preserves_and_no_call_while_hidden() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1"); f.world.show(LE, false);
    f.ticks(LE, 10); f.ticks(LL, 10);
    CHECK(f.env.calls == 0 && f.t().state(Field::leader).seeded && f.t().state(Field::leader).observed == 0);
    f.world.show(LL, true); f.tick(LL);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

// Root+A5 bits 8 (shown), 4/16 (hidden/disabled) and window+B79 desired are all required.
void test_visibility_bits() {
    struct Case { unsigned char desired, flags; bool visible; };
    for (const Case c : {Case{1, 8, true}, Case{0, 8, false}, Case{1, 0, false}, Case{1, 8 | 4, false}, Case{1, 8 | 16, false},
                         Case{1, 8 | 1 | 2 | 32 | 64 | 128, true}}) {
        Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
        f.env.rev_field(Field::leader, "1");
        f.close(LE);
        f.world.flags(LL, c.desired, c.flags); f.tick(LL);
        CHECK((f.env.calls == 1) == c.visible);
        OwnWindow own{};
        CHECK((inspect(f.world.mem, image, window_addr(LL), own) == WindowKind::visible) == c.visible);
    }
}

// N10 part 1 (scan H2): Bind returns the PARENT when the window factory fails; a root equal to the
// parent has the right class but is not the window's root, so the window is invalid (no request).
void test_root_equal_to_parent_invalid() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1");
    f.close(LE);
    f.world.mem.put<uptr>(window_addr(LL) + off::window_parent, root_addr(LL));
    f.world.show(LL, true);
    OwnWindow own{};
    CHECK(inspect(f.world.mem, image, window_addr(LL), own) == WindowKind::invalid && own.root == 0);
    f.tick(LL);
    CHECK(f.env.calls == 0);
    f.world.mem.put<uptr>(window_addr(LL) + off::window_parent, politics_root);
    CHECK(inspect(f.world.mem, image, window_addr(LL), own) == WindowKind::visible);
}

void test_coalesced_jump_and_both_fields() {
    Fixture f; f.env.armed = false;
    f.env.rev_field(Field::leader, "0"); f.env.rev_field(Field::party, "4");
    f.open(LE); f.close(LE); f.open(PE); f.close(PE);
    f.env.rev_field(Field::leader, "3"); f.env.rev_field(Field::party, "5");
    f.env.armed = true;
    f.world.show(PL, true); f.tick(PL);
    CHECK(f.env.calls == 1 && f.t().state(Field::party).acked == 5 && f.t().state(Field::leader).acked == 0);
    f.world.consume(); f.world.show(LL, true); f.tick(LL);
    CHECK(f.env.calls == 2 && f.t().state(Field::leader).acked == 3);
    f.ticks(LL, 20); f.ticks(PL, 20);
    CHECK(f.env.calls == 2 && f.attempts() == 2);
}

void test_failures_no_retry() {
    for (CallMode mode : {CallMode::fault, CallMode::no_mark, CallMode::swap_view}) {
        Fixture f; f.env.mode = mode;
        f.env.rev_field(Field::leader, "0"); f.open(LE);
        f.env.rev_field(Field::leader, "1"); f.ticks(LE, 30);
        CHECK(f.env.calls == 1 && f.attempts() == 1);
        CHECK(f.t().counters().failures == 1 && f.t().counters().acks == 0);
        CHECK(f.t().state(Field::leader).suspended && f.t().state(Field::leader).acked == 0);
        f.close(LE); f.world.show(LE, true); f.ticks(LE, 5);  // visibility edge: still suspended
        CHECK(f.env.calls == 1);
        if (mode == CallMode::swap_view) f.world.mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, fake_view);
        f.env.mode = CallMode::mark; f.env.rev_field(Field::leader, "2"); f.ticks(LE, 5);  // new success => one new attempt
        CHECK(f.env.calls == 2 && f.t().state(Field::leader).acked == 2 && f.attempts() == 2);
    }
}

void test_readback_revision_rollback_after_call() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1");
    f.env.during_call = [&] { f.env.rev_field(Field::leader, "0"); };
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().failures == 1 && f.t().counters().acks == 0);
}

// MAJOR-2: a lower value is stale text until confirmed; pending survives a short stale read.
void test_stale_lower_text_keeps_pending() {
    Fixture f; f.env.armed = false;
    f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1"); f.tick(LE);  // pending 1 observed by editor
    f.close(LE);
    f.env.armed = true;
    f.env.rev(LL, "0");                                // launcher carrier not yet refreshed
    f.world.show(LL, true); f.ticks(LL, rollback_confirm_reads - 1);
    CHECK(f.t().state(Field::leader).seeded && f.t().state(Field::leader).pending && f.env.calls == 0);
    CHECK(f.t().counters().stale_lower_reads == rollback_confirm_reads - 1 && f.t().counters().rollbacks == 0);
    CHECK(f.env.logged("lower-revision-pending-kept") == 1);
    CHECK(!f.svc.dispatch_allowed(Field::leader, 1));  // no Apply while a rollback is suspected
    f.env.rev(LL, "1"); f.tick(LL);                    // text catches up
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1 && f.t().state(Field::leader).lower_streak == 0);
}

void test_rollback_bind_invalid() {
    Fixture f; f.env.armed = false;
    f.env.rev_field(Field::leader, "5"); f.open(LE);
    f.env.rev_field(Field::leader, "4"); f.ticks(LE, rollback_confirm_reads);  // confirmed rollback
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().rollbacks == 1);
    CHECK(f.env.logged("rollback-confirmed-reset") == 1);
    CHECK(f.svc.on_prefill(window_addr(LE)) == Tracker::Seed::refused_poisoned);  // same opening
    CHECK(!f.apply_gate(LE));
    f.close(LE); f.open(LE);                                                     // next Open seeds
    CHECK(f.t().state(Field::leader).seeded && f.t().state(Field::leader).acked == 4);
    f.env.rev_field(Field::leader, "5"); f.tick(LE);
    CHECK(f.t().state(Field::leader).pending);
    f.svc.on_bind(window_addr(LL));
    CHECK(!f.t().state(Field::leader).seeded && !f.t().state(Field::leader).pending && f.t().counters().binds == 1);
    f.env.armed = true; f.close(LE); f.open(LE);
    CHECK(f.env.calls == 0 && f.t().state(Field::leader).acked == 5);
}

// MINOR-2: invalid context resets once per valid->invalid transition and is logged once.
void test_invalid_edge_triggered() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.world.mem.put<uptr>(window_addr(LL) + off::window_context + off::context_window, 0);
    f.ticks(LL, 25);
    CHECK(!f.t().state(Field::leader).seeded && f.t().counters().invalid_resets == 1);
    CHECK(f.env.logged("invalid-context-reset") == 1);
    f.world.mem.put<uptr>(window_addr(LL) + off::window_context + off::context_window, window_addr(LL));
    f.tick(LL);
    f.world.mem.put<uptr>(window_addr(LL) + off::window_context + off::context_window, 0);
    f.ticks(LL, 3);
    CHECK(f.t().counters().invalid_resets == 2 && f.env.logged("invalid-context-reset") == 2);
}

void test_postcall_identity_swap() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    constexpr uptr other_view = 0x17000000;
    f.world.mem.add(other_view, 0x600);
    std::memcpy(f.world.mem.at(other_view, 0x600), f.world.mem.at(fake_view, 0x600), 0x600);
    f.world.mem.put<unsigned char>(other_view + off::view_dirty, 1);
    f.env.rev_field(Field::leader, "1");
    f.env.during_call = [&] { f.world.mem.put<uptr>(fake_gui + off::gui_views + 8 * 12, other_view); };
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().failures == 1 && f.t().counters().acks == 0);
    CHECK(f.env.logged("postcall-identity") == 1);
}

void test_already_dirty_labelled() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.world.mem.put<unsigned char>(fake_view + off::view_dirty, 1);
    f.env.rev_field(Field::leader, "1"); f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().counters().acks == 1 && f.env.logged("ok-already-dirty") == 1);
}

void test_bind_during_inflight_is_stale() {
    auto* f = new Fixture;  // leaked on purpose: a timed-out worker may still reference it
    f->env.rev_field(Field::leader, "0"); f->open(LE);
    f->env.rev_field(Field::leader, "1");
    bool bound = false;
    f->env.during_call = [f, &bound] { bound = finishes_within([f] { f->svc.on_bind(window_addr(LE)); }, 3000); };
    f->tick(LE);
    CHECK(bound);
    if (!bound) return;
    CHECK(f->env.calls == 1 && f->t().counters().acks == 0 && f->t().counters().stale_results == 1);
    CHECK(!f->t().state(Field::leader).seeded);
    delete f;
}

void test_no_lock_across_native_call() {
    auto* f = new Fixture;  // leaked on purpose on timeout
    f->env.rev_field(Field::leader, "0"); f->open(LE);
    f->env.rev_field(Field::leader, "1");
    bool progressed = false;
    f->env.during_call = [f, &progressed] { progressed = finishes_within([f] { f->svc.dispatch_allowed(Field::leader, 1); }, 3000); };
    f->tick(LE);
    CHECK(f->env.calls == 1);
    CHECK(progressed);  // FAIL (not hang) if a lock were held across the native call
    if (progressed) delete f;
}

// Same-thread re-entrancy: native code inside B6E210 reaching our Tick again must not plan
// a second request while one is in flight.
void test_reentrant_tick_inside_call() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1");
    int depth = 0;
    f.env.during_call = [&] { if (++depth < 4) { f.tick(LE); f.tick(LL); } };
    f.world.show(LL, true);
    f.tick(LE);
    CHECK(f.env.calls == 1 && f.attempts() == 1 && f.t().counters().plans == 1 && f.t().counters().acks == 1);
}

void test_guard_rejections() {
    struct Mutation { const char* name; std::function<void(World&)> apply; Reject expected; };
    const std::vector<Mutation> mutations = {
        {"model vt", [](World& w) { w.mem.put<uptr>(model_addr(LE), image + 0x7); }, Reject::model_vt},
        {"model ctx token", [](World& w) { w.mem.put<std::uint32_t>(model_addr(LE) + off::model_context_token, 0x3A71); }, Reject::context_token},
        {"parent token", [](World& w) { w.mem.put<std::uint32_t>(model_addr(LE) + off::model_parent_token, 0x3A94); }, Reject::parent_token},
        {"parent_window_window", [](World& w) { w.mem.put<u64>(model_addr(LE) + off::model_parent_window_len, 3); }, Reject::parent_override},
        {"parent_scripted_gui", [](World& w) { w.mem.put<u64>(model_addr(LE) + off::model_parent_sg_len, 3); }, Reject::parent_override},
        {"window+108", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_parent_sg, 0x1234); }, Reject::parent_sg_window},
        {"idler null", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_idler, 0); }, Reject::idler_null},
        {"idler vt", [](World& w) { w.mem.put<uptr>(fake_idler, image + 0x1000); }, Reject::idler_vt},
        {"idler slot", [](World& w) { w.mem.put<uptr>(image + rva::vt_ingame_idler + off::slot_idler_gui, image + 0x1); }, Reject::idler_slot},
        {"manager+18", [](World& w) { w.mem.put<uptr>(fake_manager + off::manager_idler, fake_idler + 8); }, Reject::manager_idler},
        {"manager null", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_manager, 0); }, Reject::manager_idler},
        {"gui null", [](World& w) { w.mem.put<uptr>(fake_idler + off::idler_gui, 0); }, Reject::gui_null},
        {"gui idler", [](World& w) { w.mem.put<uptr>(fake_gui + off::gui_idler, fake_idler + 8); }, Reject::gui_idler},
        {"view null", [](World& w) { w.mem.put<uptr>(fake_gui + off::gui_views + 96, 0); }, Reject::view_null},
        {"view vt", [](World& w) { w.mem.put<uptr>(fake_view, image + 0x3); }, Reject::view_vt},
        {"observer vt", [](World& w) { w.mem.put<uptr>(fake_view + off::view_observer, image + 0x4); }, Reject::observer_vt},
        {"view slot28", [](World& w) { w.mem.put<uptr>(image + rva::vt_politics_view + 0x28, image + 0x5); }, Reject::view_slots},
        {"observer slot38", [](World& w) { w.mem.put<uptr>(image + rva::vt_politics_observer + 0x38, image + 0x6); }, Reject::view_slots},
        {"observer slot48", [](World& w) { w.mem.put<uptr>(image + rva::vt_politics_observer + 0x48, image + 0x6); }, Reject::view_slots},
        {"view gui", [](World& w) { w.mem.put<uptr>(fake_view + off::view_gui, fake_gui + 8); }, Reject::view_gui},
        {"view idler", [](World& w) { w.mem.put<uptr>(fake_view + off::view_idler, 0); }, Reject::view_idler},
        {"root null", [](World& w) { w.mem.put<uptr>(fake_view + off::view_root, 0); }, Reject::root_null},
        {"parent != root", [](World& w) { w.mem.put<uptr>(window_addr(LE) + off::window_parent, politics_root + 0x10); }, Reject::parent_root},
        {"dirty garbage", [](World& w) { w.mem.put<unsigned char>(fake_view + off::view_dirty, 7); }, Reject::dirty_read},
        {"token image", [](World& w) { w.mem.put<std::uint32_t>(image + rva::token_politics_tab, 0x3AB3); }, Reject::token_image},
        {"player token image", [](World& w) { w.mem.put<std::uint32_t>(image + rva::token_player_context, 0x3A71); }, Reject::token_image},
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
    // The gui object is non-polymorphic (R4): word 0 is data and never gates; it and the +B8
    // structural candidate are copied for the log. A vt mismatch elsewhere fills observed/expected.
    {
        World w; Target t{}; ResolveDiag diag{};
        w.mem.put<uptr>(fake_gui, 0x0000123400005678ull);                        // arbitrary data word
        w.mem.put<uptr>(fake_gui + gui_structural_offset, image + gui_structural_rva);
        CHECK(resolve(w.mem, image, window_addr(LE), t, &diag) == Reject::none && t.gui == fake_gui);
        CHECK(diag.gui_word0 == 0x0000123400005678ull && diag.gui_b8_rva == gui_structural_rva && diag.reject == Reject::none);
        w.mem.put<uptr>(fake_gui + gui_structural_offset, 0);                      // candidate pin absent: still no gate
        CHECK(resolve(w.mem, image, window_addr(LE), t, &diag) == Reject::none && diag.gui_b8_rva == 0);
    }
    {
        World w; Target t{}; ResolveDiag diag{};
        w.mem.put<uptr>(fake_view, image + 0x1234);  // politics view vtable mismatch
        CHECK(resolve(w.mem, image, window_addr(LE), t, &diag) == Reject::view_vt);
        CHECK(diag.observed_rva == 0x1234 && diag.expected_rva == rva::vt_politics_view);
    }
    {
        World w; Target t{}; ResolveDiag diag{};
        w.mem.put<uptr>(image + rva::vt_politics_observer + 0x48, image + 0x4321);
        CHECK(resolve(w.mem, image, window_addr(LE), t, &diag) == Reject::view_slots);
        CHECK(diag.observed_rva == 0x4321 && diag.expected_rva == 0x1620890 && diag.slot_rva == rva::vt_politics_observer + 0x48);
    }
    // Service level: a guard reject never calls native and is deferred to the next edge.
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, 0);
    f.env.rev_field(Field::leader, "1"); f.ticks(LE, 40);
    CHECK(f.env.calls == 0 && f.t().counters().precall_rejects == 1 && f.attempts() == 0);
    f.close(LE); f.world.show(LE, true); f.ticks(LE, 40);
    CHECK(f.env.calls == 0 && f.t().counters().precall_rejects == 2);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, fake_idler);
    f.close(LE); f.world.show(LE, true); f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 1);
}

// A new success after a precall reject lifts the deferral without a visibility edge.
void test_new_revision_lifts_deferral() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, 0);
    f.env.rev_field(Field::leader, "1"); f.ticks(LE, 10);
    CHECK(f.env.calls == 0 && f.t().state(Field::leader).deferred);
    f.world.mem.put<uptr>(fake_gui + off::gui_idler, fake_idler);
    f.ticks(LE, 10);
    CHECK(f.env.calls == 0);  // same revision, same edge: still deferred
    f.env.rev_field(Field::leader, "2"); f.tick(LE);
    CHECK(f.env.calls == 1 && f.t().state(Field::leader).acked == 2);
}

void test_foreign_and_malformed() {
    Fixture f;
    const char other[] = "rn_ip_country_gui______";
    std::memcpy(f.world.mem.at(name_addr(LE), sizeof(other) - 1), other, sizeof(other) - 1);
    f.world.mem.put<u64>(model_addr(LE) + off::model_name + 0x10, sizeof(other) - 1);
    f.world.mem.put<u64>(model_addr(LE) + off::model_name + 0x18, sizeof(other) - 1);
    f.env.rev_field(Field::leader, "0");
    CHECK(f.open(LE) == Tracker::Seed::refused_bad && !f.t().state(Field::leader).seeded);
    Fixture g;
    for (const char* bad : {"", "1.0", "-1", "abc", "01", "1K", "1000"}) {
        g.env.rev_field(Field::leader, bad);
        g.close(LE);
        CHECK(g.open(LE) == Tracker::Seed::refused_bad);
        CHECK(!g.t().state(Field::leader).seeded && !g.apply_gate(LE));
    }
    g.env.carriers.clear(); g.ticks(LE, 5);
    CHECK(!g.t().state(Field::leader).seeded && g.env.calls == 0);
    CHECK(g.env.logged("revision-carrier-rejected") == 1);  // per ok->bad transition, not per frame
}


// Bridge ordering: the debug bridge prefill runs inside observe_tick BEFORE caption on_tick.
// After a confirmed rollback the next opening must still be able to seed.
void test_prefill_before_tick_after_rollback() {
    Fixture f; f.env.rev_field(Field::leader, "5");
    f.world.show(LE, true); CHECK(f.svc.on_prefill(window_addr(LE)) == Tracker::Seed::seeded); f.tick(LE);
    f.env.rev_field(Field::leader, "4"); f.ticks(LE, rollback_confirm_reads);
    CHECK(f.t().counters().rollbacks == 1 && !f.t().state(Field::leader).seeded);
    f.close(LE);
    f.world.show(LE, true);
    CHECK(f.svc.on_prefill(window_addr(LE)) == Tracker::Seed::seeded);  // prefill first, then tick
    f.tick(LE);
    CHECK(f.t().state(Field::leader).acked == 4 && f.apply_gate(LE));
}

// Minimal refresh guard (replacement of "authority"): seed + increase + own visible window +
// fresh chain with parent == politics view root. Each missing element => zero native calls.
void test_minimal_guard_matrix() {
    {   Fixture f; f.env.rev_field(Field::leader, "0"); f.world.show(LL, true); f.env.rev_field(Field::leader, "1"); f.ticks(LL, 5);
        CHECK(f.env.calls == 0); }                                              // no seed
    {   Fixture f; f.env.rev_field(Field::leader, "3"); f.open(LE); f.ticks(LE, 20); f.close(LE);
        f.world.show(LL, true); f.ticks(LL, 20); CHECK(f.env.calls == 0); }   // no increase
    {   Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE); f.env.rev_field(Field::leader, "1"); f.close(LE);
        f.ticks(LL, 20); CHECK(f.env.calls == 0); }                           // all own windows hidden
    {   Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
        f.world.mem.put<uptr>(window_addr(LE) + off::window_parent, politics_root + 0x40);
        f.env.rev_field(Field::leader, "1"); f.ticks(LE, 20); CHECK(f.env.calls == 0); }  // other parent/view
    {   Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
        f.world.mem.put<std::uint32_t>(model_addr(LE) + off::model_context_token, 0x3A71);
        f.env.rev_field(Field::leader, "1"); f.ticks(LE, 20); CHECK(f.env.calls == 0); }  // not player_context
    {   Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE); f.env.rev_field(Field::leader, "1"); f.tick(LE);
        CHECK(f.env.calls == 1 && f.env.bad_args == 0); }                      // all present: exactly one
}

// HOI4 "[?ROOT.var|0]" renders integers as plain digits (runtime-proven for rn_ip_epoch);
// every value of the accepted range must round-trip, nothing else may parse.
void test_counter_format_0_999() {
    int ok = 0;
    for (u64 n = 0; n <= 999; ++n) {
        const auto text = std::to_string(n); u64 v = 1000;
        if (parse_revision(text.data(), text.size(), v) && v == n) ++ok;
    }
    CHECK(ok == 1000);
    u64 v = 0;
    for (const char* bad : {"1000", "1.000", "1,000", "1.0K", "1K", "0.5", "1.00", " 1", "1 ", "+1", "-0"})
        CHECK(!parse_revision(bad, std::strlen(bad), v));
}

// N05: a launcher is never an Apply origin, even when its field IS seeded.
void test_dispatch_from_launcher_seeded() {
    Fixture f; f.env.rev_field(Field::leader, "4"); f.open(LE);
    CHECK(f.apply_gate(LE));                                   // editor origin allowed
    f.close(LE); f.world.show(LL, true); f.tick(LL);
    CHECK(f.t().state(Field::leader).seeded);
    CHECK(!f.apply_gate(LL));                                  // launcher origin rejected
}

// N08: literal headroom: 998 may Apply (999 still representable), 999 may not.
void test_headroom_literal() {
    Tracker t;
    t.seed(Field::leader, 998);
    CHECK(t.dispatch_allowed(Field::leader, 998));
    t.seed(Field::party, 999);
    CHECK(!t.dispatch_allowed(Field::party, 999));
    u64 v = 0;
    CHECK(parse_revision("999", 3, v) && v == 999 && !parse_revision("1000", 4, v));
}

// N10: a valid Bind clears the invalid latch, so a later invalid context resets/logs again.
void test_bind_clears_invalid_latch() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    const uptr ctx_window = window_addr(LL) + off::window_context + off::context_window;
    f.world.mem.put<uptr>(ctx_window, 0); f.tick(LL);            // invalid #1
    CHECK(f.t().counters().invalid_resets == 1);
    f.world.mem.put<uptr>(ctx_window, window_addr(LL));
    f.svc.on_bind(window_addr(LL));                              // valid bind clears the latch
    f.world.mem.put<uptr>(ctx_window, 0); f.tick(LL);            // invalid #2 without a valid tick between
    CHECK(f.t().counters().invalid_resets == 2 && f.env.logged("invalid-context-reset") == 2);
}

// R5-MINOR-1: permanent states get their own verdict (sentinel -2 / -3), not "stale" (-1).
void test_dispatch_verdicts() {
    using V = Service<TestEnv>::Verdict;
    {   Fixture f; f.env.rev_field(Field::leader, "4"); f.open(LE);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::allowed); }
    {   Fixture f; f.env.rev_field(Field::leader, "4"); f.world.show(LE, true); f.tick(LE);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::stale); }            // unseeded
    {   Fixture f; f.env.rev_field(Field::leader, "4"); f.open(LE);
        f.env.carriers.clear();
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::no_counter);           // carrier missing
        f.env.rev_field(Field::leader, "1,000");
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::no_counter);           // unreadable format
        CHECK(f.env.logged("apply-rejected-no-counter") == 2); }
    {   Fixture f; f.env.rev_field(Field::leader, "999"); f.open(LE);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::cap_reached);          // limit, reopen won't help
        f.close(LE); f.open(LE);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::cap_reached && f.env.logged("apply-rejected-cap") == 2); }
    {   Fixture f; f.env.rev_field(Field::leader, "998"); f.open(LE);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::allowed); }           // 998 still allowed
    {   Fixture f; f.env.rev_field(Field::leader, "4"); f.open(LE); f.close(LE); f.world.show(LL, true); f.tick(LL);
        CHECK(f.svc.on_dispatch_verdict(window_addr(LL)) == V::stale); }              // launcher origin
    {   Fixture f; f.env.rev_field(Field::leader, "5"); f.open(LE);
        f.env.rev_field(Field::leader, "4"); f.tick(LE);                              // lower read (suspicion)
        CHECK(f.svc.on_dispatch_verdict(window_addr(LE)) == V::stale); }
}

// R5-NOTE-1: the gui diagnostics are logged once on the first successful resolve, not only on rejects.
void test_first_success_diag_logged_once() {
    Fixture f; f.env.rev_field(Field::leader, "0"); f.open(LE);
    f.env.rev_field(Field::leader, "1"); f.tick(LE);
    f.world.consume(); f.env.rev_field(Field::leader, "2"); f.tick(LE);
    CHECK(f.env.calls == 2 && f.env.logged("detail=resolve-ok-first") == 1 && f.env.logged("gui_b8_rva=") == 1);
}

void test_forward_order_and_once() {
    std::vector<int> order;
    auto orig = [&](int v) { order.push_back(v); };
    forward_then_observe(orig, [&](int v) { order.push_back(100 + v); }, true, 1);
    forward_then_observe(orig, [&](int v) { order.push_back(100 + v); }, false, 2);
    CHECK((order == std::vector<int>{1, 101, 2}));
}

void test_fingerprint_table() {
    for (const auto& fp : fingerprints) {
        unsigned char bytes[128]{};
        CHECK(decode_hex(fp.hex, bytes, sizeof(bytes)) > 0);
    }
    unsigned char b[2]{};
    CHECK(decode_hex("zz", b, 2) == 0 && decode_hex("abc", b, 2) == 0 && decode_hex("aabbcc", b, 2) == 0);
    static_assert(fingerprint_count == 25);
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // FAIL lines survive a crashing mutant
    test_parse();
    test_seed_only_at_prefill_and_apply_gate();
    test_first_apply_without_launcher_tick();
    test_first_apply_editor_stays_visible();
    test_saved_seven_to_eight();
    test_launcher_unseeded_never_baselines();
    test_dispatch_gate();
    test_rapid_reopen_keeps_pending();
    test_disarmed_zero_calls();
    test_hidden_preserves_and_no_call_while_hidden();
    test_visibility_bits();
    test_root_equal_to_parent_invalid();
    test_coalesced_jump_and_both_fields();
    test_failures_no_retry();
    test_readback_revision_rollback_after_call();
    test_stale_lower_text_keeps_pending();
    test_rollback_bind_invalid();
    test_invalid_edge_triggered();
    test_postcall_identity_swap();
    test_already_dirty_labelled();
    test_no_lock_across_native_call();
    test_bind_during_inflight_is_stale();
    test_reentrant_tick_inside_call();
    test_guard_rejections();
    test_new_revision_lifts_deferral();
    test_foreign_and_malformed();
    test_prefill_before_tick_after_rollback();
    test_minimal_guard_matrix();
    test_counter_format_0_999();
    test_dispatch_from_launcher_seeded();
    test_headroom_literal();
    test_bind_clears_invalid_latch();
    test_dispatch_verdicts();
    test_first_success_diag_logged_once();
    test_forward_order_and_once();
    test_fingerprint_table();
    if (argc > 1) exe_check(argv[1]);
    std::printf("caption refresh tests: %d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    // Leaked fixtures of timed-out workers may still be blocked: exit without joining them.
    std::_Exit(failures ? 1 : 0);
}
