/**
 * @file editor_hotpath_test.cpp
 * @brief Tests of the stage-2 performance changes of the editor bridge (both flavors).
 *
 * 1. Pre-filter soundness: for every fabricated window (model vtable good/bad/unreadable,
 *    own, debug-only, foreign and malformed names, inline and heap strings, missing
 *    terminators, bad capacities, unreadable name bytes) `window_may_be_own == false`
 *    implies that the bridge, caption and focus inspections all return "unrelated".
 *    Completeness: every model name an observer accepts passes the pre-filter.
 * 2. Tick on foreign windows forwards the original exactly once and leaves no log line and
 *    no caption/focus state change.
 * 2b. Remembered verdicts (review P1/P2): the Tick pre-filter equals the fresh one for thousands
 *    of models (the table clears itself when full), a Bind replaces the verdict of a reused
 *    model address, a failed read is never remembered (and a failed read at Bind forgets
 *    everything), and an unreadable or guard-page window reads as "not ours" without a crash.
 * 2c. Scan N6: a C++ exception thrown by the original Tick or Bind passes through the hook body
 *    to the caller (a `noexcept` hook would end the process instead).
 * 3. Release flavor: `RN_INLINE` lines carry no typed text; debug flavor keeps it.
 * 4. Exports of the built DLL (path argument): `RNDebugRequest` only in the debug flavor.
 * No game process, no hooks installed.
 */
#include "compat/rn_editor_bridge.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Description printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

/**
 * @brief Writes a value into fake memory.
 * @tparam T Value type.
 * @param object Base.
 * @param offset Offset.
 * @param value Value.
 */
template <class T>
void put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, static_cast<const void*>(&value), sizeof(value));
}

unsigned tick_calls = 0;
void __fastcall fake_tick(void*, char) {
    ++tick_calls;
}
unsigned bind_calls = 0;
void __fastcall fake_bind(void*) {
    ++bind_calls;
}
/// @brief An original Tick that throws, the way a game helper reports a fatal error.
void __fastcall throwing_tick(void*, char) {
    throw std::runtime_error("tick");
}
/// @brief An original Bind that throws.
void __fastcall throwing_bind(void*) {
    throw std::runtime_error("bind");
}

/**
 * @brief Reads the owned log file.
 * @return Content.
 */
std::string logs() {
    LARGE_INTEGER size{};
    LARGE_INTEGER const zero{};
    GetFileSizeEx(log_file, &size);
    SetFilePointerEx(log_file, zero, nullptr, FILE_BEGIN);
    std::string value(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(log_file, value.data(), static_cast<DWORD>(value.size()), &read, nullptr);
    SetFilePointerEx(log_file, zero, nullptr, FILE_END);
    return value;
}

/// @brief One fabricated window with its model and name storage.
struct FakeWindow {
    alignas(8) std::array<unsigned char, 0xC00> window{};
    alignas(8) std::array<unsigned char, 0x100> model{};
    alignas(8) std::array<char, 256> heap{};
};

}  // namespace

/**
 * @brief Entry point.
 * @param argc Argument count.
 * @param argv Optional path of the built capie_editor.dll of this flavor.
 * @return 0 when every check passed.
 */
int main(int argc, char** argv) {
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    void* memory = VirtualAlloc(nullptr, 0x3502000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(memory != nullptr, "fake image");
    image_base = reinterpret_cast<std::uintptr_t>(memory);
    log_file = CreateFileW(L"editor_hotpath_test.log", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    // An unreadable page for "unreadable name bytes" cases.
    auto* noaccess = static_cast<char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));

    // ---- 1. Pre-filter soundness and completeness -------------------------------------------
    std::vector<std::string> names = {"rn_ip_leader_gui",
                                      "rn_ip_party_neutrality_gui",
                                      "rn_ip_country_gui",
                                      "rn_ip_leader_launcher_gui",
                                      "rn_ip_party_neutrality_launcher_gui",
                                      "rn_ip_country_launcher_gui",
                                      "rn_ilqa_guard_gui",
                                      "vanilla_gui",
                                      "",
                                      "rn_ip_leader_gu",
                                      "rn_ip_leader_guix",
                                      "RN_IP_LEADER_GUI",
                                      "rn_ip_party_neutrality_launcher_gu",
                                      std::string(128, 'a'),
                                      std::string(129, 'a')};
    const std::size_t accepted_names = 5;
    unsigned cases = 0;
    unsigned prefilter_false = 0;
    for (int vt_mode = 0; vt_mode < 3; ++vt_mode) {  // 0 good, 1 wrong, 2 unreadable model pointer
        for (std::size_t n = 0; n < names.size(); ++n) {
            for (int string_mode = 0; string_mode < 6; ++string_mode) {  // see below
                FakeWindow f{};
                const auto& name = names[n];
                put(f.window.data(), 0xE8,
                    vt_mode == 2 ? static_cast<void*>(noaccess) : static_cast<void*>(f.model.data()));
                put(f.model.data(), 0, vt_mode == 1 ? image_base + 0x1234 : image_base + capie::core::rva::vt_sg_model);
                MsvcString text{};
                text.size = name.size();
                if (string_mode == 0 && name.size() <= 15) {  // inline, terminated
                    std::memcpy(text.data.small, name.data(), name.size());
                    text.capacity = 15;
                } else if (string_mode == 1) {  // heap, terminated
                    std::memcpy(f.heap.data(), name.data(), (std::min)(name.size(), f.heap.size()));
                    text.data.pointer = f.heap.data();
                    text.capacity = (std::max<std::uint64_t>)(name.size(), 16);
                } else if (string_mode == 2) {  // heap, no terminator
                    std::memset(f.heap.data(), 'z', f.heap.size());
                    std::memcpy(f.heap.data(), name.data(), (std::min)(name.size(), f.heap.size()));
                    text.data.pointer = f.heap.data();
                    text.capacity = (std::max<std::uint64_t>)(name.size(), 16);
                } else if (string_mode == 3) {  // size > capacity
                    text.capacity = name.empty() ? 0 : name.size() - 1;
                    text.data.pointer = f.heap.data();
                } else if (string_mode == 4) {  // capacity > 1 MiB
                    text.capacity = 2U << 20;
                    text.data.pointer = f.heap.data();
                } else {  // unreadable heap bytes
                    text.data.pointer = noaccess;
                    text.capacity = (std::max<std::uint64_t>)(name.size(), 16);
                }
                put(f.model.data(), 8, text);
                void* window = f.window.data();
                const bool maybe = window_may_be_own(window);
                View view{};
                const auto bridge = inspect_window(window, view);
                capie::caption::OwnWindow caption_own{};
                const auto caption = capie::caption::inspect(capie::core::ProcessMemory{}, image_base,
                                                             reinterpret_cast<std::uintptr_t>(window), caption_own);
                capie::focus::Own focus_own{};
                const auto focus = capie::focus::inspect(capie::core::ProcessMemory{}, image_base,
                                                         reinterpret_cast<std::uintptr_t>(window), focus_own);
                ++cases;
                if (!maybe) {
                    ++prefilter_false;
                    check(bridge == ViewResult::unrelated && caption == capie::caption::WindowKind::unrelated &&
                              focus == capie::focus::Kind::unrelated,
                          "pre-filter false implies all unrelated: name=" + name +
                              " mode=" + std::to_string(string_mode) + " vt=" + std::to_string(vt_mode));
                }
                if (vt_mode == 0 && n < accepted_names &&
                    (string_mode == 1 || string_mode == 2 || (string_mode == 0 && name.size() <= 15))) {
                    check(maybe, "accepted model name passes the pre-filter: " + name);
                }
            }
        }
    }
    check(prefilter_false > cases / 2, "pre-filter rejects most foreign windows");

    // ---- 2. Foreign windows: original once, no log, no observer state -----------------------
    original_tick = fake_tick;
    active.store(true);
    caption_armed.store(true);
    focus_armed.store(true);
    FakeWindow foreign{};
    put(foreign.window.data(), 0xE8, static_cast<void*>(foreign.model.data()));
    put(foreign.model.data(), 0, image_base + capie::core::rva::vt_sg_model);
    put(foreign.model.data(), 8, borrowed_string("vanilla_gui", 11));
    const auto before = caption_service.snapshot().counters();
    const auto log_before = logs().size();
    for (int i = 0; i < 1000; ++i) {
        tick_observed(foreign.window.data(), 0);
    }
    FakeWindow plain{};  // not even a scripted GUI
    for (int i = 0; i < 1000; ++i) {
        tick_observed(plain.window.data(), 0);
    }
    const auto after = caption_service.snapshot().counters();
    check(tick_calls == 2000, "foreign ticks forward the original exactly once each");
    check(logs().size() == log_before, "foreign ticks write nothing");
    check(std::memcmp(&before, &after, sizeof(before)) == 0, "foreign ticks leave caption state unchanged");

    // ---- 2b. Remembered verdicts of the Tick pre-filter (review P1/P2) -----------------------
    {
        static constexpr char own_name[] = "rn_ip_country_gui";  // 17 bytes: heap string layout
        static constexpr char other_name[] = "vanilla_politics_gui";
        const auto name_model = [](unsigned char* model, const char* name) {
            put(model, 0, image_base + capie::core::rva::vt_sg_model);
            put(model, 8, borrowed_string(name, std::strlen(name)));
        };
        // Many models: the remembered verdict always equals the fresh one, also after the table
        // filled up and cleared itself.
        constexpr std::size_t model_total = 3000;
        const auto many = std::make_unique<FakeWindow[]>(model_total);
        bool same = true;
        unsigned own_seen = 0;
        model_verdicts.clear();
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < model_total; ++i) {
                auto& f = many[i];
                if (pass == 0) {
                    put(f.window.data(), 0xE8, static_cast<void*>(f.model.data()));
                    name_model(f.model.data(), i % 3 == 0 ? own_name : other_name);
                }
                const bool cached = tick_window_may_be_own(f.window.data());
                same = same && cached == window_may_be_own(f.window.data()) && cached == (i % 3 == 0);
                own_seen += cached ? 1U : 0U;
            }
        }
        check(same && own_seen == 2 * 1000, "remembered verdicts equal the fresh ones for 3000 models, twice");

        // A Bind replaces the verdict of a reused model address (both directions).
        original_bind = fake_bind;
        FakeWindow reused{};
        put(reused.window.data(), 0xE8, static_cast<void*>(reused.model.data()));
        name_model(reused.model.data(), other_name);
        check(!tick_window_may_be_own(reused.window.data()), "foreign model: not ours");
        name_model(reused.model.data(), own_name);  // the game freed the model; a new one took its address
        check(!tick_window_may_be_own(reused.window.data()), "without a Bind the remembered verdict holds");
        bind_observed(reused.window.data());
        check(bind_calls == 1 && tick_window_may_be_own(reused.window.data()),
              "Bind gives the reused address its new verdict");
        name_model(reused.model.data(), other_name);
        bind_observed(reused.window.data());
        check(!tick_window_may_be_own(reused.window.data()), "Bind also turns an own verdict into a foreign one");

        // A failed read is never remembered.
        auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        name_model(page, own_name);
        DWORD old_protect = 0;
        VirtualProtect(page, 4096, PAGE_NOACCESS, &old_protect);
        FakeWindow late{};
        put(late.window.data(), 0xE8, static_cast<void*>(page));
        check(!tick_window_may_be_own(late.window.data()), "unreadable model: not ours");
        VirtualProtect(page, 4096, PAGE_READWRITE, &old_protect);
        check(tick_window_may_be_own(late.window.data()), "the unreadable verdict was not remembered");

        // A failed read at Bind forgets every verdict (the old one for that address must not survive).
        check(!tick_window_may_be_own(reused.window.data()), "foreign verdict remembered");
        VirtualProtect(page, 4096, PAGE_NOACCESS, &old_protect);
        bind_observed(late.window.data());  // the model of the bound window is unreadable
        VirtualProtect(page, 4096, PAGE_READWRITE, &old_protect);
        name_model(reused.model.data(), own_name);  // reused address, no Bind of its own
        check(tick_window_may_be_own(reused.window.data()), "a failed read at Bind cleared the old verdicts");

        // The window itself unreadable or on a guard page: "not ours", no crash, original still once.
        auto* window_page =
            static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
        const auto ticks = tick_calls;
        check(!tick_window_may_be_own(window_page), "unreadable window: not ours");
        tick_observed(window_page, 0);
        check(tick_calls == ticks + 1, "unreadable window: original forwarded once, no crash");
        VirtualProtect(window_page, 4096, PAGE_READWRITE | PAGE_GUARD, &old_protect);
        check(!tick_window_may_be_own(window_page), "guard-page window: not ours, no crash");
        check(!tick_window_may_be_own(nullptr), "null window: not ours");
        VirtualFree(window_page, 0, MEM_RELEASE);
        VirtualFree(page, 0, MEM_RELEASE);
        model_verdicts.clear();
    }

    // ---- 2c. Scan N6: exceptions of the originals pass through the hook bodies ---------------
    {
        FakeWindow window{};
        bool caught = false;
        original_tick = throwing_tick;
        try {
            tick_observed(window.window.data(), 0);
        } catch (const std::runtime_error&) {
            caught = true;
        }
        check(caught, "an exception of the original Tick reaches the caller");
        caught = false;
        original_bind = throwing_bind;
        try {
            bind_observed(window.window.data());
        } catch (const std::runtime_error&) {
            caught = true;
        }
        check(caught, "an exception of the original Bind reaches the caller");
        original_tick = fake_tick;
        original_bind = fake_bind;
    }

    // ---- 3. Text in RN_INLINE lines only in the debug flavor ---------------------------------
    InputSnapshot sample{};
    sample.size = 3;
    sample.points[0] = 'J';
    sample.points[1] = 'a';
    sample.points[2] = 'n';
    diagnostic(1, "hotpath-test-event", 0, 7, 11, &sample, 0);
    const auto text_logged = logs().find("text=\"Jan\"") != std::string::npos;
    check(logs().find("event=hotpath-test-event") != std::string::npos, "event line written");
    check(text_logged == debug_diagnostics,
          debug_diagnostics ? "debug flavor logs the text" : "release flavor omits the text");

    // ---- 3b. Init line derived from the tables equals the stage-2 literal (4277e0f) -------------
    {
        constexpr const char* expected =
            debug_diagnostics
                ? "RN_DEBUG initialized format=1 ring=256 ttl_ms=10000 models=7 selectors=10 fields=5; exact image + 11 bridge + "
                  "25 caption + 8 focus unique fingerprints; 7 shared hooks; caption refresh armed (25 caption FP, 4 slot pins, 2 "
                  "tokens, RequestViewRefresh(gui,12) only, seed gate on leader/party Apply); focus release on politics hide (8 FP, 2 slot "
                  "pins, FocusSwitch(manager,null) once per transition); own copied history, scalar remote requests, fresh post-Tick "
                  "snapshots, observed Execute; dispatch is not execution and Execute is not effect success; no auto-Apply, no "
                  "synthetic input, focus only released (never set) on politics hide, Escape not handled; stale Apply -> epoch -1 "
                  "sentinel; invalid field -> rn_native(b)_err; CAPIE 1.5 capie_editor flavor=debug"
                : "RN_LIFECYCLE initialized; exact image + 11 bridge + 25 caption + 8 focus unique fingerprints; 4 hooks (Tick, "
                  "Bind, Dispatch, effect ctor); caption refresh armed (RequestViewRefresh(gui,12) only, seed gate on leader/party Apply); "
                  "focus release on politics hide (FocusSwitch(manager,null) once per transition); no auto-Apply, no synthetic input, "
                  "Escape not handled; stale Apply -> epoch -code sentinel; invalid field -> rn_native(b)_err; log channels "
                  "error+lifecycle; CAPIE 1.5 capie_editor flavor=release";
        const auto before_init = logs().size();
        write_init_line();
        const auto written = logs().substr(before_init);
        check(written.starts_with(expected), "init line derived from the tables equals the accepted literal");
    }

    // ---- 4. Exports of the built DLL ------------------------------------------------------------
    if (argc > 1) {
        std::wstring const path(argv[1], argv[1] + std::strlen(argv[1]));
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        check(module != nullptr, "built DLL maps");
        if (module != nullptr) {
            check(GetProcAddress(module, "RNInitialize") != nullptr, "RNInitialize exported");
            check((GetProcAddress(module, "RNDebugRequest") != nullptr) == debug_diagnostics,
                  debug_diagnostics ? "debug flavor exports RNDebugRequest" : "release flavor has no RNDebugRequest");
            FreeLibrary(module);
        }
    }
    CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
    static_cast<void>(
        std::printf("editor hot path (%s): %d checks, %d failures (%u windows, %u rejected by the pre-filter)\n",
                    debug_diagnostics ? "debug" : "release", checks, failures, cases, prefilter_false));
    std::_Exit(failures != 0 ? 1 : 0);
}
