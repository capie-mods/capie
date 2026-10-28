// Offline tests of focus_release.hpp: fake memory and a fake 225E320 that mirrors the
// byte-proven semantics (old->unfocus clears +271 bit0 and [m+230]; [m+158]=new).
#include "../compat/rn_caption_focus.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace rn::focus;
using rn::caption::uptr;
static int checks = 0, failures = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

struct FakeMem {
    std::map<uptr, std::vector<unsigned char>> regions;
    void add(uptr a, std::size_t n) { regions[a] = std::vector<unsigned char>(n, 0); }
    unsigned char* at(uptr a, std::size_t n) {
        auto it = regions.upper_bound(a); if (it == regions.begin()) return nullptr; --it;
        if (a - it->first + n > it->second.size()) return nullptr; return it->second.data() + (a - it->first);
    }
    bool read(uptr a, void* out, std::size_t n) const {
        auto it = regions.upper_bound(a); if (it == regions.begin()) return false; --it;
        if (a < it->first || a - it->first + n > it->second.size()) return false;
        std::memcpy(out, it->second.data() + (a - it->first), n); return true;
    }
    template <class T> void put(uptr a, T v) { auto* p = at(a, sizeof(T)); if (!p) std::abort(); std::memcpy(p, &v, sizeof(T)); }
    template <class T> T get(uptr a) { T v{}; read(a, &v, sizeof(T)); return v; }
};
constexpr uptr k_image = 0x140000000, k_manager = 0x30000000, k_parent = 0x31000000, foreign_widget = 0x32000000;
uptr window_of(std::size_t e) { return 0x40000000 + e * 0x01000000; }
uptr model_of(std::size_t e) { return window_of(e) + 0x10000; }
uptr root_of(std::size_t e) { return window_of(e) + 0x20000; }
uptr name_of(std::size_t e) { return window_of(e) + 0x30000; }
uptr edit_of(std::size_t e, std::size_t f) { return window_of(e) + 0x40000 + f * 0x10000; }
uptr def_of(std::size_t e, std::size_t f) { return window_of(e) + 0x60000 + f * 0x1000; }
uptr defname_of(std::size_t e, std::size_t f) { return window_of(e) + 0x70000 + f * 0x1000; }

void put_string(FakeMem& m, uptr at, uptr storage, const char* s) {
    const auto n = std::strlen(s);
    if (n <= 15) { std::memcpy(m.at(at, n), s, n); m.put<rn::caption::u64>(at + 0x18, 15); }
    else { std::memcpy(m.at(storage, n), s, n); m.put<uptr>(at, storage); m.put<rn::caption::u64>(at + 0x18, n); }
    m.put<rn::caption::u64>(at + 0x10, n);
}
struct World {
    FakeMem m;
    World() {
        m.add(k_image + rva::manager_global, 8); m.put<uptr>(k_image + rva::manager_global, k_manager);
        m.add(k_manager, 0x300); m.add(k_parent, 0x100); m.add(foreign_widget, 0x300);
        parent_flags(8);
        for (std::size_t e = 0; e < editor_count; ++e) {
            const uptr w = window_of(e), model = model_of(e), root = root_of(e);
            m.add(w, 0xC00); m.add(model, 0x100); m.add(root, 0x100); m.add(name_of(e), 64);
            m.put<uptr>(w + rn::caption::off::window_model, model); m.put<uptr>(w + rn::caption::off::window_root, root);
            m.put<uptr>(w + rn::caption::off::window_parent, k_parent);
            const uptr ctx = w + rn::caption::off::window_context;
            m.put<uptr>(ctx, k_image + rn::caption::rva::vt_sg_context); m.put<uptr>(ctx + 0x10, w);
            m.put<uptr>(ctx + 0x20, model); m.put<uptr>(ctx + 0x28, root);
            m.put<uptr>(model, k_image + rn::caption::rva::vt_sg_model);
            put_string(m, model + 8, name_of(e), editors[e].model);
            m.put<uptr>(root, k_image + rn::caption::rva::vt_sg_root);
            for (std::size_t f = 0; f < 2; ++f) {
                if (!editors[e].fields[f]) continue;
                const uptr edit = edit_of(e, f);
                m.add(edit, 0x300); m.add(def_of(e, f), 0x60); m.add(defname_of(e, f), 64); m.add(edit + 0x1000, 0x200);
                m.put<uptr>(edit, k_image + rva::vt_edit); m.put<uptr>(edit + off::edit_buffer, k_image + rva::vt_text_buffer);
                m.put<uptr>(edit + off::edit_definition, def_of(e, f));
                m.put<uptr>(def_of(e, f), k_image + rva::vt_edit_definition);
                put_string(m, def_of(e, f) + off::definition_name, defname_of(e, f), editors[e].fields[f]);
                m.put<uptr>(edit + off::edit_text, edit + 0x1000);
                m.put<rn::caption::u64>(edit + off::edit_size, 3); m.put<rn::caption::u64>(edit + off::edit_capacity, 64);
                m.put<std::uint32_t>(edit + 0x1000, 'J'); m.put<std::uint32_t>(edit + 0x1004, 'a'); m.put<std::uint32_t>(edit + 0x1008, 'n');
            }
        }
    }
    void parent_flags(unsigned char f) { m.put<unsigned char>(k_parent + off::parent_flags, f); }
    void focus(uptr edit) {  // as the game's 225E320(k_manager, edit) + 22F3FB0 would leave it
        m.put<uptr>(k_manager + off::manager_focus, edit);
        m.put<uptr>(k_manager + off::manager_receiver, edit + off::edit_receiver);
        m.put<unsigned char>(edit + off::edit_flags, static_cast<unsigned char>(m.get<unsigned char>(edit + off::edit_flags) | 1));
    }
};
enum class Mode { proper, no_effect, fault, mutate_text, keeps_focus_pointer };
struct Env {
    World* w{}; bool arm = true; Mode mode = Mode::proper; int calls = 0, bad = 0;
    std::vector<std::string> lines; std::function<void()> during;
    const FakeMem& mem() const { return w->m; }
    uptr image() const { return k_image; }
    uptr find_edit(uptr root, const char* name) {
        for (std::size_t e = 0; e < editor_count; ++e)
            for (std::size_t f = 0; f < 2; ++f)
                if (root == root_of(e) && editors[e].fields[f] && std::strcmp(name, editors[e].fields[f]) == 0) return edit_of(e, f);
        return 0;
    }
    bool call_focus_null(uptr m) {
        ++calls; if (m != k_manager) ++bad;
        if (during) during();
        if (mode == Mode::fault) return false;
        if (mode == Mode::no_effect) return true;
        const auto old = w->m.get<uptr>(k_manager + off::manager_focus);
        if (old && old != foreign_widget) {
            w->m.put<unsigned char>(old + off::edit_flags, static_cast<unsigned char>(w->m.get<unsigned char>(old + off::edit_flags) & 0xFE));
            w->m.put<uptr>(k_manager + off::manager_receiver, 0);
            if (mode == Mode::mutate_text) w->m.put<std::uint32_t>(old + 0x1000, 'X');
        }
        if (mode != Mode::keeps_focus_pointer) w->m.put<uptr>(k_manager + off::manager_focus, 0);
        return true;
    }
    bool armed() const { return arm; }
    void log(const char* l) { lines.emplace_back(l); }
    std::size_t logged(const char* s) const { std::size_t n = 0; for (auto& l : lines) if (l.find(s) != std::string::npos) ++n; return n; }
};
struct Fixture {
    World w; Env env; Release<Env> rel{env};
    Fixture() { env.w = &w; }
    void tick(std::size_t e, int n = 1) { for (int i = 0; i < n; ++i) rel.on_tick(window_of(e)); }
};

void test_release_once_per_transition() {
    Fixture f; f.w.focus(edit_of(0, 0));
    f.tick(0, 3);                                   // k_parent visible: nothing
    CHECK(f.env.calls == 0);
    f.w.parent_flags(0); f.tick(0);                 // visible -> hidden with our focus
    CHECK(f.env.calls == 1 && f.env.bad == 0);
    CHECK(f.w.m.get<uptr>(k_manager + off::manager_focus) == 0 && (f.w.m.get<unsigned char>(edit_of(0, 0) + off::edit_flags) & 1) == 0);
    CHECK(f.rel.counters().released == 1 && f.rel.counters().text_changed == 0 && f.env.logged("result=released draft=unchanged") == 1);
    f.tick(0, 30);                                  // stays hidden: no retry
    CHECK(f.env.calls == 1);
    f.w.parent_flags(8); f.tick(0); f.w.focus(edit_of(0, 0)); f.w.parent_flags(0); f.tick(0);  // new transition
    CHECK(f.env.calls == 2);
}
void test_no_call_without_own_focus() {
    Fixture f; f.tick(0); f.w.parent_flags(0); f.tick(0);  // nobody focused
    CHECK(f.env.calls == 0 && f.rel.counters().no_own_focus == 1 && f.env.logged("own_focus=0") == 1);
    Fixture g; g.w.m.put<uptr>(k_manager + off::manager_focus, foreign_widget); g.tick(0); g.w.parent_flags(0); g.tick(0);
    CHECK(g.env.calls == 0);                                 // foreign widget focused: never touched
    Fixture h; h.w.focus(edit_of(0, 0)); h.w.m.put<uptr>(k_manager + off::manager_receiver, foreign_widget);
    h.tick(0); h.w.parent_flags(0); h.tick(0);
    CHECK(h.env.calls == 0);                                 // receiver mismatch
    Fixture k; k.w.focus(edit_of(0, 0)); k.w.m.put<unsigned char>(edit_of(0, 0) + off::edit_flags, 0);
    k.tick(0); k.w.parent_flags(0); k.tick(0);
    CHECK(k.env.calls == 0);                                 // focus bit clear
    Fixture n; n.w.focus(edit_of(0, 0)); n.w.m.put<uptr>(edit_of(0, 0), k_image + 0x5);
    n.tick(0); n.w.parent_flags(0); n.tick(0);
    CHECK(n.env.calls == 0);                                 // not an exact CEditBox
    Fixture x; x.w.focus(edit_of(1, 0)); x.tick(0); x.w.parent_flags(0); x.tick(0);
    CHECK(x.env.calls == 0);                                 // focus belongs to ANOTHER editor (party), leader transition
}
void test_party_second_field_and_country() {
    Fixture f; f.w.focus(edit_of(1, 1)); f.tick(1); f.w.parent_flags(0); f.tick(1);
    CHECK(f.env.calls == 1 && f.env.logged("field=rn_ip_party_neutrality_long_edit") == 1);
    Fixture g; g.w.focus(edit_of(2, 1)); g.tick(2); g.w.parent_flags(8 | 0x10); g.tick(2);  // bit 4 => hidden by script
    CHECK(g.env.calls == 1 && g.env.logged("field=rn_ip_country_adj_edit") == 1);
}
void test_destroyed_is_invalid() {  // Ghidra review G3: root/parent flag 0x04 = being destroyed, never a hide
    Fixture f; f.w.focus(edit_of(2, 1)); f.tick(2); f.w.parent_flags(8 | 4); f.tick(2, 3);
    CHECK(f.env.calls == 0);                                 // parent destroyed: no FocusSwitch
    Own own{};
    CHECK(inspect(f.w.m, k_image, window_of(2), own) == Kind::invalid);
    Fixture g; g.w.focus(edit_of(0, 0)); g.tick(0);
    g.w.m.put<unsigned char>(root_of(0) + off::root_flags, 8 | 4);
    CHECK(inspect(g.w.m, k_image, window_of(0), own) == Kind::invalid);
    g.w.parent_flags(0); g.tick(0, 3);
    CHECK(g.env.calls == 0);                                 // own root destroyed: no FocusSwitch either
    Fixture h; h.w.m.put<unsigned char>(root_of(0) + off::root_flags, 8 | 0x10);
    CHECK(inspect(h.w.m, k_image, window_of(0), own) == Kind::valid && !own.own_visible);  // hidden stays valid
}
void test_disarmed_bind_invalid() {
    Fixture f; f.env.arm = false; f.w.focus(edit_of(0, 0)); f.tick(0); f.w.parent_flags(0); f.tick(0);
    CHECK(f.env.calls == 0);
    Fixture g; g.w.focus(edit_of(0, 0)); g.tick(0); g.rel.on_bind(window_of(0)); g.w.parent_flags(0); g.tick(0);
    CHECK(g.env.calls == 0);                                 // bind forgets the previous k_parent state
    Fixture h; h.w.focus(edit_of(0, 0)); h.tick(0);
    h.w.m.put<uptr>(window_of(0) + rn::caption::off::window_context + 0x10, 0); h.tick(0);   // invalid
    h.w.m.put<uptr>(window_of(0) + rn::caption::off::window_context + 0x10, window_of(0));
    h.w.parent_flags(0); h.tick(0);
    CHECK(h.env.calls == 0);                                 // invalid forgets, no transition across it
    Fixture p; p.w.focus(edit_of(0, 0)); p.tick(0); p.w.m.put<uptr>(window_of(0) + rn::caption::off::window_parent, 0); p.tick(0);
    CHECK(p.env.calls == 1);                                 // k_parent detached counts as hidden
}
void test_failures_and_draft() {
    for (Mode mode : {Mode::no_effect, Mode::fault, Mode::keeps_focus_pointer}) {
        Fixture f; f.env.mode = mode; f.w.focus(edit_of(0, 0)); f.tick(0); f.w.parent_flags(0); f.tick(0, 20);
        CHECK(f.env.calls == 1 && f.rel.counters().failed == 1 && f.rel.counters().released == 0);
        CHECK(f.env.logged("release-unverified") == 1);
    }
    Fixture t; t.env.mode = Mode::mutate_text; t.w.focus(edit_of(0, 0)); t.tick(0); t.w.parent_flags(0); t.tick(0);
    CHECK(t.rel.counters().text_changed == 1 && t.env.logged("draft=CHANGED") == 1);
}
void test_no_lock_across_call() {
    auto* f = new Fixture; f->w.focus(edit_of(0, 0)); f->tick(0);
    bool progressed = false;
    f->env.during = [f, &progressed] {
        auto done = std::make_shared<std::atomic<bool>>(false);
        std::thread t([f, done] { f->rel.counters(); done->store(true); });
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!done->load() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        progressed = done->load();
        if (progressed) t.join(); else t.detach();
    };
    f->w.parent_flags(0); f->tick(0);
    CHECK(f->env.calls == 1 && progressed);
}
// N10 part 1 (scan H2): a root equal to the parent is the window factory's failure result: invalid.
void test_root_equal_to_parent_invalid() {
    Fixture f; f.w.focus(edit_of(0, 0)); f.tick(0);
    Own own{};
    f.w.m.put<uptr>(window_of(0) + rn::caption::off::window_parent, root_of(0));
    CHECK(inspect(f.w.m, k_image, window_of(0), own) == Kind::invalid);
    f.w.m.put<unsigned char>(root_of(0) + rn::caption::off::root_flags, 0);  // the "parent" now reads hidden
    f.tick(0);
    CHECK(f.env.calls == 0);                                  // invalid forgets, no release
    f.w.m.put<uptr>(window_of(0) + rn::caption::off::window_parent, k_parent);
    CHECK(inspect(f.w.m, k_image, window_of(0), own) == Kind::valid);
}
// Scan M3: an unreadable flag byte is "unknown", never "parent hidden": invalid, no release.
void test_unreadable_flags_invalid() {
    Fixture f; f.w.focus(edit_of(0, 0)); f.tick(0);
    f.w.m.regions.erase(k_parent);                            // parent page decommitted (teardown)
    Own own{};
    CHECK(inspect(f.w.m, k_image, window_of(0), own) == Kind::invalid);
    f.tick(0, 3);
    CHECK(f.env.calls == 0);
    f.w.m.add(k_parent, 0x100); f.w.parent_flags(8); f.tick(0);
    f.w.parent_flags(0); f.tick(0);
    CHECK(f.env.calls == 1);                                  // readable again: a real hide still releases
    struct Case { uptr base; std::size_t offset; };
    for (const Case c : {Case{root_of(0), rn::caption::off::root_flags}, Case{window_of(0), rn::caption::off::window_desired},
                         Case{window_of(0), rn::caption::off::window_parent_seen}}) {
        World w;                                              // only this one byte is unreadable
        auto tail = w.m.regions[c.base];
        w.m.regions[c.base].resize(c.offset);
        tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(c.offset + 1));
        w.m.regions[c.base + c.offset + 1] = tail;
        Own o{};
        CHECK(inspect(w.m, k_image, window_of(0), o) == Kind::invalid);
    }
}
// R3-MINOR-3: parent bit 16 means hidden, same criterion as the caption core.
void test_parent_bit16_hidden() {
    Fixture f; f.w.focus(edit_of(0, 0)); f.tick(0);
    f.w.parent_flags(8 | 16); f.tick(0);
    CHECK(f.env.calls == 1 && f.env.logged("parent_flags=0x18") == 1);
}

// R4 planning: every editor-root visible->hidden transition is logged once with its source,
// classified only from copied bits (parent detached / parent hidden / trigger false / own flags).
void own_show(World& w, std::size_t e, unsigned char root_flags, unsigned char desired) {
    w.m.put<unsigned char>(root_of(e) + rn::caption::off::root_flags, root_flags);
    w.m.put<unsigned char>(window_of(e) + rn::caption::off::window_desired, desired);
}
void test_hide_source_log() {
    struct Case { const char* source; std::function<void(World&)> hide; };
    const std::vector<Case> cases = {
        {"source=parent-hidden", [](World& w) { w.parent_flags(0); own_show(w, 0, 0x10, 1); }},
        {"source=own-trigger-false", [](World& w) { own_show(w, 0, 0x10, 0); }},
        {"source=parent-detached", [](World& w) { w.m.put<uptr>(window_of(0) + rn::caption::off::window_parent, 0); own_show(w, 0, 0x10, 1); }},
        {"source=own-root-flags", [](World& w) { own_show(w, 0, 8 | 16, 1); }},
    };
    for (const auto& c : cases) {
        Fixture f; own_show(f.w, 0, 8, 1); f.tick(0, 2);
        CHECK(f.env.logged("RN_HIDE") == 0);
        c.hide(f.w); f.tick(0, 5);
        CHECK(f.env.logged("RN_HIDE") == 1 && f.env.logged(c.source) == 1 && f.rel.counters().root_hides == 1);
    }
    Fixture g; g.env.arm = false; own_show(g.w, 0, 8, 1); g.tick(0); own_show(g.w, 0, 0x10, 0); g.tick(0);
    CHECK(g.env.logged("RN_HIDE") == 0);                       // disarmed: nothing
}

int exe_check(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { std::printf("EXE check SKIPPED\n"); return 0; }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, d.data() + o, 2); return v; };
    const auto e = u32(0x3C); const auto ns = u16(e + 6); const auto opt = u16(e + 20);
    struct S { char name[9]; std::uint32_t va, vs, raw, rs; }; std::vector<S> secs;
    for (unsigned i = 0; i < ns; ++i) { const auto o = e + 24 + opt + i * 40; S s{}; std::memcpy(s.name, d.data() + o, 8);
        s.vs = u32(o + 8); s.va = u32(o + 12); s.rs = u32(o + 16); s.raw = u32(o + 20); secs.push_back(s); }
    const S* text = nullptr; for (auto& s : secs) if (std::strcmp(s.name, ".text") == 0) text = &s;
    auto map = [&](uptr r) -> const unsigned char* { for (auto& s : secs) if (r >= s.va && r < s.va + s.rs) return d.data() + s.raw + (r - s.va); return nullptr; };
    const unsigned char* t = d.data() + text->raw; const std::size_t ts = text->vs < text->rs ? text->vs : text->rs;
    int ok = 0;
    for (const auto& fp : focus_fingerprints) {
        unsigned char b[128]{}; const auto n = rn::caption::decode_hex(fp.hex, b, sizeof(b)); std::size_t c = 0; uptr where = 0;
        for (std::size_t i = 0; i + n <= ts; ++i) if (t[i] == b[0] && std::memcmp(t + i, b, n) == 0) { ++c; where = i + text->va; }
        const bool good = n && c == 1 && where == fp.rva; CHECK(good); ok += good;
        std::printf("  FOCUS FP %07llX %3zuB matches=%zu %s\n", static_cast<unsigned long long>(fp.rva), n, c, good ? "OK" : "BAD");
    }
    for (const auto& pin : focus_slot_pins) { rn::caption::u64 v = 0; if (auto* p = map(pin.vtable + pin.slot)) std::memcpy(&v, p, 8); CHECK(v == 0x140000000ull + pin.target); }
    // 2379B70: mov rcx,[rip+disp] must address the k_manager global 3500FC0.
    std::int32_t disp = 0; if (auto* p = map(0x2379B76)) std::memcpy(&disp, p, 4);
    CHECK(0x2379B7All + disp == static_cast<long long>(rva::manager_global));
    std::printf("EXE focus check: %d/%zu fingerprints, slot pins C0/C8, manager global rip target\n", ok, focus_fingerprint_count);
    return 0;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_release_once_per_transition();
    test_no_call_without_own_focus();
    test_party_second_field_and_country();
    test_destroyed_is_invalid();
    test_disarmed_bind_invalid();
    test_failures_and_draft();
    test_no_lock_across_call();
    test_parent_bit16_hidden();
    test_root_equal_to_parent_invalid();
    test_unreadable_flags_invalid();
    test_hide_source_log();
    if (argc > 1) exe_check(argv[1]);
    std::printf("focus release tests: %d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    std::_Exit(failures ? 1 : 0);
}
