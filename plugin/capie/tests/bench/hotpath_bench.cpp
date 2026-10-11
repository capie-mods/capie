/**
 * @file hotpath_bench.cpp
 * @brief Micro-benchmarks of the per-frame and per-lookup hot paths: accepted sources vs CAPIE.
 *
 * Built three times by build.ps1:
 *   CAPIE_BENCH_LEGACY=1  the accepted rev7 bridge source and the accepted country-status
 *                         candidate source (old code, same fakes);
 *   CAPIE_BENCH_LEGACY=0  CAPIE with CAPIE_DEBUG_DIAGNOSTICS=0 (release) or =1 (debug).
 * Scenarios (fake image and widgets, no game, no hooks installed):
 *   tick_plain          Tick of a window that is not a scripted GUI;
 *   tick_foreign_sg     Tick of a scripted GUI window of another mod/vanilla;
 *   tick_editor_open    Tick of our visible, prefilled leader editor (steady state);
 *   tick_launcher       Tick of the visible leader launcher (caption observation);
 *   lookup_n_foreign / lookup_z_foreign / exists_foreign   vanilla localisation keys;
 *   lookup_n_own / lookup_z_own                            an RN1 key (repeat lookup);
 *   lookup_z_status                                        RN_PLUGIN_STATUS_PL.
 * Output: one line per scenario with the median ns/call over 9 runs (QueryPerformanceCounter).
 * Two TUs are needed because the old bridge and old country sources both define
 * file-local `active`; the country part is in bench_country.inl, compiled separately
 * via CAPIE_BENCH_PART.
 */
#ifndef CAPIE_BENCH_PART
#define CAPIE_BENCH_PART 0  // NOLINT(cppcoreguidelines-macro-usage): build switch
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

/// @brief Median ns/call of `body` over 9 runs of `iterations` calls.
template <class Body> double measure(unsigned iterations, Body&& body) {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::array<double, 9> runs{};
    for (auto& run : runs) {
        LARGE_INTEGER start{};
        LARGE_INTEGER stop{};
        QueryPerformanceCounter(&start);
        for (unsigned i = 0; i < iterations; ++i) body();
        QueryPerformanceCounter(&stop);
        run = static_cast<double>(stop.QuadPart - start.QuadPart) * 1e9 / static_cast<double>(frequency.QuadPart) / iterations;
    }
    std::sort(runs.begin(), runs.end());
    return runs[runs.size() / 2];
}

#if CAPIE_BENCH_PART == 0
// ================================ Editor Tick ================================================
#if CAPIE_BENCH_LEGACY
#include "caption_refresh_bridge.cpp"
#define BENCH_LABEL "legacy-rev7"
#else
#include "../compat/rn_editor_bridge.hpp"
#define BENCH_LABEL (debug_diagnostics ? "capie-debug" : "capie-release")
#endif

namespace {
template <class T> void put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}
struct Own {
    alignas(8) std::array<unsigned char, 0xC00> window{};
    alignas(8) std::array<unsigned char, 0x1360> root{};
    alignas(8) std::array<unsigned char, 0x100> model{};
    alignas(8) std::array<unsigned char, 0x140> revision{};
};
struct World {
    alignas(8) std::array<unsigned char, 0x700> idler{};
    alignas(8) std::array<unsigned char, 0x200> gui{};
    alignas(8) std::array<unsigned char, 0x600> view{};
    alignas(8) std::array<unsigned char, 0x100> politics_root{};
    alignas(8) std::array<unsigned char, 0x100> manager{};
    Own editor{}, launcher{}, foreign{};
    alignas(8) std::array<unsigned char, 0xC00> plain{};
    alignas(8) std::array<unsigned char, 0x280> edit{};
    alignas(8) std::array<unsigned char, 0x50> definition{};
    alignas(8) std::array<unsigned char, 0x238> focus_manager{};
    alignas(8) std::array<unsigned char, 0x140> prefill{};
    alignas(8) std::array<unsigned char, 0x140> epoch{};
    std::uint32_t text[64]{};
};
World* world = nullptr;
void* __fastcall fake_instant(void* root, const MsvcString* name, bool, bool) {
    if (root == world->editor.root.data() && name_equals(name, "rn_ip_leader_prefill")) return world->prefill.data();
    if (root == world->editor.root.data() && name_equals(name, "rn_ip_leader_epoch")) return world->epoch.data();
    if (root == world->editor.root.data() && name_equals(name, "rn_ip_leader_rev")) return world->editor.revision.data();
    if (root == world->launcher.root.data() && name_equals(name, "rn_ip_leader_rev")) return world->launcher.revision.data();
    return nullptr;
}
void* __fastcall fake_edit(void* root, const MsvcString* name, bool, bool) {
    return root == world->editor.root.data() && name_equals(name, "rn_ip_leader_edit") ? world->edit.data() : nullptr;
}
void __fastcall fake_set_text(void* tb, const MsvcString* text) {
    auto* edit = static_cast<unsigned char*>(tb) - 0x80;
    const char* bytes = text->capacity <= 15 ? reinterpret_cast<const char*>(text->data.small) : text->data.pointer;
    for (std::size_t i = 0; i < text->size && i < 64; ++i) world->text[i] = static_cast<unsigned char>(bytes[i]);
    put(edit, 0xB0, static_cast<void*>(world->text));
    put(edit, 0xC0, static_cast<std::uint64_t>(text->size));
    put(edit, 0xC8, std::uint64_t{64});
}
void __fastcall fake_refresh_edit(void*) {}
void __fastcall fake_tick(void*, char) {}
void __fastcall fake_refresh(void* gui, int) { put(gui == world->gui.data() ? world->view.data() : world->view.data(), 0x44, static_cast<unsigned char>(1)); }
void __fastcall fake_focus(void*, void*) {}
void make_own(Own& o, const char* name, World& w) {
    const auto base = image_base;
    put(o.model.data(), 0, base + 0x29DB558);
    put(o.model.data(), 8, borrowed_string(name, std::strlen(name)));
    put(o.model.data(), 0x28, std::uint32_t{0x3A70});
    put(o.model.data(), 0x2C, std::uint32_t{0x3AB2});
    auto* win = o.window.data();
    put(win, 0xE8, static_cast<void*>(o.model.data()));
    put(win, 0xF8, static_cast<void*>(o.root.data()));
    put(win, 0x110, base + 0x294E568);
    put(win, 0x120, static_cast<void*>(win));
    put(win, 0x130, static_cast<void*>(o.model.data()));
    put(win, 0x138, static_cast<void*>(o.root.data()));
    put(win, 0xE0, static_cast<void*>(w.idler.data()));
    put(win, 0xF0, static_cast<void*>(w.manager.data()));
    put(win, 0x100, static_cast<void*>(w.politics_root.data()));
    put(win, 0xB79, static_cast<unsigned char>(1));
    put(o.root.data(), 0, base + 0x2B45278);
    put(o.root.data(), 0xA5, static_cast<unsigned char>(8));
    put(o.revision.data(), 0, base + 0x2B468C8);
    put(o.revision.data(), 0x110, borrowed_string("3", 1));
}
}  // namespace

int main() {
    void* memory = VirtualAlloc(nullptr, 0x3454000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    image_base = reinterpret_cast<std::uintptr_t>(memory);
    log_file = CreateFileW(L"bench_editor.log", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    auto* w = new World;
    world = w;
    auto* img = reinterpret_cast<unsigned char*>(image_base);
    put(img, 0x294E4CC, std::uint32_t{0x3AB2});
    put(img, 0x2AECB3C, std::uint32_t{0x3A70});
    put(img, 0x29D9648 + 0x28, image_base + 0x1237520);
    put(img, 0x29D96F0 + 0x38, image_base + 0x1582820);
    put(img, 0x29D96F0 + 0x48, image_base + 0x15863F0);
    put(img, 0x2968FF0 + 0xB8, image_base + 0xDCCF30);
    put(w->idler.data(), 0, image_base + 0x2968FF0);
    put(w->idler.data(), 0x6B8, static_cast<void*>(w->gui.data()));
    put(w->gui.data(), 0x180, static_cast<void*>(w->idler.data()));
    put(w->gui.data(), 0xC8 + 8 * 12, static_cast<void*>(w->view.data()));
    put(w->view.data(), 0, image_base + 0x29D9648);
    put(w->view.data(), 0x30, image_base + 0x29D96F0);
    put(w->view.data(), 0x38, static_cast<void*>(w->gui.data()));
    put(w->view.data(), 0x570, static_cast<void*>(w->idler.data()));
    put(w->view.data(), 0x578, static_cast<void*>(w->politics_root.data()));
    put(w->manager.data(), 0x18, static_cast<void*>(w->idler.data()));
    put(w->politics_root.data(), 0xA5, static_cast<unsigned char>(8));
    make_own(w->editor, "rn_ip_leader_gui", *w);
    make_own(w->launcher, "rn_ip_leader_launcher_gui", *w);
    make_own(w->foreign, "vanilla_politics_gui", *w);
    auto* edit = w->edit.data();
    put(edit, 0, image_base + 0x2B4A940);
    put(edit, 0x80, image_base + 0x2B4ABD0);
    put(edit, 0x60, static_cast<void*>(w->definition.data()));
    put(w->definition.data(), 0, image_base + 0x2B54050);
    put(w->definition.data(), 0x28, borrowed_string("rn_ip_leader_edit", 17));
    put(edit, 0x75, static_cast<unsigned char>(8));
    put(edit, 0xB0, static_cast<void*>(w->text));
    put(edit, 0xC8, std::uint64_t{64});
    put(img, 0x3453230, static_cast<void*>(w->focus_manager.data()));
    put(w->prefill.data(), 0, image_base + 0x2B468C8);
    put(w->prefill.data(), 0x110, borrowed_string("Jan", 3));
    put(w->epoch.data(), 0, image_base + 0x2B468C8);
    put(w->epoch.data(), 0x110, borrowed_string("7", 1));
    find_instant = fake_instant;
    find_edit = fake_edit;
    set_text = fake_set_text;
    refresh_edit = fake_refresh_edit;
    original_tick = fake_tick;
    caption_request = fake_refresh;
    focus_switch = fake_focus;
    active.store(true);
    caption_armed.store(true);
    focus_armed.store(true);
    for (int i = 0; i < 10; ++i) tick_observed(w->editor.window.data(), 0);   // prefill + seed
    for (int i = 0; i < 10; ++i) tick_observed(w->launcher.window.data(), 0);
    const bool prefilled = lifecycle[0].prefilled && caption_service.snapshot().state(rn::caption::Field::leader).seeded;
    LARGE_INTEGER size_before{};
    GetFileSizeEx(log_file, &size_before);
    constexpr unsigned n = 200000;
    const double plain = measure(n, [&] { tick_observed(w->plain.data(), 0); });
    const double foreign = measure(n, [&] { tick_observed(w->foreign.window.data(), 0); });
    const double editor = measure(n / 10, [&] { tick_observed(w->editor.window.data(), 0); });
    const double launcher = measure(n / 10, [&] { tick_observed(w->launcher.window.data(), 0); });
    LARGE_INTEGER size_after{};
    GetFileSizeEx(log_file, &size_after);
    std::printf("BENCH %s tick_plain_ns=%.1f tick_foreign_sg_ns=%.1f tick_editor_open_ns=%.1f tick_launcher_ns=%.1f "
                "steady_state_log_bytes=%lld prefilled_and_seeded=%u\n",
                BENCH_LABEL, plain, foreign, editor, launcher, static_cast<long long>(size_after.QuadPart - size_before.QuadPart),
                prefilled ? 1U : 0U);
    std::fflush(stdout);
    std::_Exit(prefilled ? 0 : 1);
}

#else
// ================================ Country lookups ============================================
#if CAPIE_BENCH_LEGACY
#include "country.cpp"
#define BENCH_LABEL "legacy-country-status"
#else
#include "../../src/dll/capie_core.cpp"
using namespace capie::country;  // NOLINT(google-build-using-namespace): bench brevity
#define BENCH_LABEL "capie"
#endif
namespace {
const char original_text[] = "ORIGINAL";
const char* __cdecl fake_n(const char*, int) { return original_text; }
const char* __cdecl fake_z(const char*) { return original_text; }
bool __cdecl fake_exists(const char*) { return false; }
volatile const char* sink = nullptr;
volatile bool sink_bool = false;
}  // namespace

int main() {
    original_n = fake_n;
    original_z = fake_z;
    original_exists = fake_exists;
    active.store(true);
    const char foreign[] = "POL_fascism_party_long";
    const char own[] = "RN1|Rzeczpospolita Testowa|testowy|_ADJ";
    const char status[] = "RN_PLUGIN_STATUS_PL";
    constexpr unsigned n = 2000000;
    const double n_foreign = measure(n, [&] { sink = lookup_n(foreign, static_cast<int>(sizeof(foreign) - 1)); });
    const double z_foreign = measure(n, [&] { sink = lookup_z(foreign); });
    const double e_foreign = measure(n, [&] { sink_bool = exists(foreign); });
    const double n_own = measure(n / 10, [&] { sink = lookup_n(own, static_cast<int>(sizeof(own) - 1)); });
    const double z_own = measure(n / 10, [&] { sink = lookup_z(own); });
    const double z_status = measure(n, [&] { sink = lookup_z(status); });
    std::printf("BENCH %s lookup_n_foreign_ns=%.1f lookup_z_foreign_ns=%.1f exists_foreign_ns=%.1f lookup_n_own_ns=%.1f "
                "lookup_z_own_ns=%.1f lookup_z_status_ns=%.1f\n",
                BENCH_LABEL, n_foreign, z_foreign, e_foreign, n_own, z_own, z_status);
    return 0;
}
#endif
