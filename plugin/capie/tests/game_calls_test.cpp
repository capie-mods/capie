/**
 * @file game_calls_test.cpp
 * @brief Tests of the game-call fault latch in engine/game_calls.hpp (scan findings H1 and H3).
 *
 * Every wrapper is driven with fake game functions. One fake dereferences null, the way a stale
 * object faults inside the game. The test checks that the wrapper returns failure, that the latch
 * is set, and that every wrapper afterwards refuses WITHOUT calling its fake. A fault in a CAPIE
 * read (the vtable check of a lookup on a no-access page) must not set the latch. The latch shared
 * between the two DLLs (`share_game_call_latch`) is checked with a stand-in for the core's latch.
 * A C++ exception inside a game call (scan N6) must latch and reach the crash reporter (a stand-in
 * for `UnhandledExceptionFilter`), any other exception must pass on unlatched.
 * The first fault and the first refusal of a module are logged once each (`log_tests`, a
 * temporary file as the log). Owned memory and a temporary file only; no game process.
 */
#include "../src/engine/game_calls.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int checks = 0;
int failures = 0;

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Description printed on failure.
 */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label));
    }
}

using namespace capie::engine;  // NOLINT(google-build-using-namespace): test brevity

/// @brief Calls of each fake since the last `reset`.
struct Calls {
    int find{};      ///< FindEdit.
    int set{};       ///< SetText.
    int refresh{};   ///< Edit refresh.
    int caption{};   ///< RequestViewRefresh.
    int focus{};     ///< FocusSwitch.
    int valid{};     ///< PortraitEntryValid.
    int copy{};      ///< String copy.
    int update{};    ///< UpdateNames.
    int colour{};    ///< RefreshCountryColor.
} calls;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake call counters

/// @brief Null, read through a volatile so the compiler keeps the faulting access.
int* volatile null_target = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// @brief Faults like a game function on a stale object (write through null).
void fault() { *null_target = 1; }

/// @brief Widget the fake lookup returns (vtable set in `main`).
alignas(8) std::uintptr_t widget[4]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief What the fake lookup returns.
void* lookup_result = widget;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void* __fastcall fake_find(void*, const NameView*, bool, bool) {
    ++calls.find;
    return lookup_result;
}
void __fastcall fake_set(void*, const MsvcString*) { ++calls.set; }
void __fastcall faulting_set(void*, const MsvcString*) {
    ++calls.set;
    fault();
}
void __fastcall fake_refresh(void*) { ++calls.refresh; }
void __fastcall fake_caption(void*, int) { ++calls.caption; }
void __fastcall fake_focus(void*, void*) { ++calls.focus; }
bool fake_valid(const void*) {
    ++calls.valid;
    return true;
}
bool faulting_valid(const void*) {
    ++calls.valid;
    fault();
    return true;
}
void* fake_copy(void* out, const void*) {
    ++calls.copy;
    return out;
}
void* faulting_copy(void* out, const void*) {
    ++calls.copy;
    fault();
    return out;
}
std::uint64_t fake_update(std::uintptr_t) {
    ++calls.update;
    return 1;
}
std::uint64_t faulting_update(std::uintptr_t) {
    ++calls.update;
    fault();
    return 1;
}
void fake_colour(void*, const std::int32_t*) { ++calls.colour; }
void faulting_colour(void*, const std::int32_t*) {
    ++calls.colour;
    fault();
}
/// @brief A game function that throws a C++ exception (an STL helper's fatal error).
void __fastcall throwing_set(void*, const MsvcString*) {
    ++calls.set;
    throw std::length_error("set");
}
/// @brief A game function that raises a non-C++, non-memory exception.
void __fastcall raising_refresh(void*) {
    ++calls.refresh;
    RaiseException(0xE0000001, 0, 0, nullptr);
}

/// @brief What the stand-in for the game's crash reporter saw.
struct Reported {
    int reports{};               ///< Calls of the reporter.
    unsigned long code{};        ///< Exception code it saw.
    int ends{};                  ///< Calls of the process end.
    unsigned long end_code{};    ///< Code passed to the process end.
    LONG verdict{EXCEPTION_EXECUTE_HANDLER};  ///< What the reporter answers.
} reported;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake reporter state

/// @brief Stand-in for `UnhandledExceptionFilter` (the game's crash reporter behind it).
LONG WINAPI fake_report(EXCEPTION_POINTERS* info) {
    ++reported.reports;
    reported.code = info->ExceptionRecord->ExceptionCode;
    return reported.verdict;
}
/// @brief Stand-in for the process end (returns, so the test continues).
void fake_end(unsigned long code) {
    ++reported.ends;
    reported.end_code = code;
}

/// @brief Clears the latch, the counters and the shared latch; installs the well-behaved fakes.
void reset() {
    game_call_faulted.store(false);
    game_call_latch.store(&game_call_faulted);
    calls = Calls{};
    lookup_result = widget;
    find_edit = fake_find;
    find_instant = nullptr;
    set_text = fake_set;
    refresh_edit = fake_refresh;
    caption_request = fake_caption;
    focus_switch = fake_focus;
}

/// @brief Edit box stand-in (SetText receives edit + edit_buffer).
alignas(8) unsigned char edit[0x400]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief Root stand-in.
alignas(8) unsigned char root[0x10]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief Focus manager stand-in.
alignas(8) unsigned char manager[0x10]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief Portrait entry stand-in.
alignas(8) unsigned char entry[0x30]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief Tag field stand-in (RefreshCountryColor's second argument).
const std::int32_t colour_tag = 7;
/// @brief String storage stand-in.
alignas(8) unsigned char string_out[0x20]{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * @brief Whether every editor wrapper is refused without calling its fake.
 * @param label Prefix for the failure lines.
 */
void expect_editor_refused(const char* label) {
    const MsvcString text = borrowed_string("x", 1);
    const Calls before = calls;
    check(guarded_find(find_edit, root, "a") == nullptr, label);
    check(!guarded_set(edit, &text), label);
    check(!guarded_refresh(edit), label);
    check(!caption_call(root, capie::core::value::politics_view_index), label);
    check(!focus_call_null(manager), label);
    check(calls.find == before.find && calls.set == before.set && calls.refresh == before.refresh &&
              calls.caption == before.caption && calls.focus == before.focus,
          label);
}

/**
 * @brief Whether every core wrapper is refused without calling its fake.
 * @param label Prefix for the failure lines.
 */
void expect_core_refused(const char* label) {
    const MsvcString text = borrowed_string("x", 1);
    const Calls before = calls;
    bool valid = true;
    check(!guarded_portrait_valid(fake_valid, entry, valid) && !valid, label);
    check(!guarded_string_copy(fake_copy, string_out, &text), label);
    check(!guarded_update_names(fake_update, 0x1000), label);
    check(!guarded_refresh_colour(fake_colour, manager, &colour_tag), label);
    check(calls.valid == before.valid && calls.copy == before.copy && calls.update == before.update &&
              calls.colour == before.colour,
          label);
}

/// @brief H1: a fault inside SetText latches; every later editor call is refused unseen.
void editor_latch_tests() {
    reset();
    const MsvcString text = borrowed_string("x", 1);
    check(guarded_find(find_edit, root, "a") == widget && guarded_set(edit, &text) && guarded_refresh(edit) &&
              caption_call(root, capie::core::value::politics_view_index) && focus_call_null(manager),
          "unlatched: every wrapper calls the game");
    check(calls.find == 1 && calls.set == 1 && calls.refresh == 1 && calls.caption == 1 && calls.focus == 1,
          "unlatched: each fake called once");
    check(game_calls_allowed(), "no fault yet");
    set_text = faulting_set;
    check(!guarded_set(edit, &text) && calls.set == 2, "faulting SetText: wrapper returns false");
    check(game_call_faulted.load() && !game_calls_allowed(), "fault inside SetText sets the latch");
    set_text = fake_set;
    expect_editor_refused("latched: editor wrapper refused without a call");
    expect_core_refused("latched: core wrapper refused without a call");
}

/// @brief H1: an unreadable widget in the CAPIE-side vtable check is "not found", never a latch.
void capie_read_tests() {
    reset();
    void* page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    check(page != nullptr, "no-access page");
    lookup_result = page;
    check(guarded_find(find_edit, root, "a") == nullptr && calls.find == 1, "lookup result unreadable: not found");
    check(game_calls_allowed() && !game_call_faulted.load(), "a failed CAPIE read does not latch");
    lookup_result = widget;
    widget[0] = 0;
    check(guarded_find(find_edit, root, "a") == nullptr && game_calls_allowed(), "wrong vtable: not found, no latch");
    widget[0] = image_base + capie::core::rva::vt_edit;
    check(guarded_find(find_edit, root, "a") == widget, "right vtable: found");
    static_cast<void>(VirtualFree(page, 0, MEM_RELEASE));
}

/// @brief H3: each core wrapper calls the game, and a fault in each one latches.
void core_wrapper_tests() {
    const MsvcString text = borrowed_string("x", 1);
    reset();
    bool valid = false;
    check(guarded_portrait_valid(fake_valid, entry, valid) && valid && calls.valid == 1, "PortraitEntryValid called");
    check(guarded_string_copy(fake_copy, string_out, &text) && calls.copy == 1, "string copy called");
    check(guarded_update_names(fake_update, 0x1000) && calls.update == 1, "UpdateNames called");
    check(guarded_refresh_colour(fake_colour, manager, &colour_tag) && calls.colour == 1, "RefreshCountryColor called");
    check(!guarded_portrait_valid(nullptr, entry, valid) && !guarded_string_copy(nullptr, string_out, &text) &&
              !guarded_update_names(nullptr, 0x1000) && !guarded_refresh_colour(nullptr, manager, &colour_tag) &&
              game_calls_allowed(),
          "unresolved: refused, no latch");
    check(!guarded_refresh_colour(fake_colour, nullptr, &colour_tag) &&
              !guarded_refresh_colour(fake_colour, manager, nullptr) && calls.colour == 1 && game_calls_allowed(),
          "RefreshCountryColor without a map or tag: refused, no call, no latch");

    reset();
    valid = true;
    check(!guarded_portrait_valid(faulting_valid, entry, valid) && !valid, "faulting PortraitEntryValid: false");
    check(!game_calls_allowed(), "PortraitEntryValid fault latches");
    expect_core_refused("after PortraitEntryValid fault");
    expect_editor_refused("after PortraitEntryValid fault (editor)");

    reset();
    check(!guarded_string_copy(faulting_copy, string_out, &text) && !game_calls_allowed(), "string copy fault latches");
    expect_core_refused("after string copy fault");

    reset();
    check(!guarded_update_names(faulting_update, 0x1000) && !game_calls_allowed(), "UpdateNames fault latches");
    expect_core_refused("after UpdateNames fault");

    reset();
    check(!guarded_refresh_colour(faulting_colour, manager, &colour_tag) && calls.colour == 1 && !game_calls_allowed(),
          "RefreshCountryColor fault latches");
    expect_core_refused("after RefreshCountryColor fault");
    expect_editor_refused("after RefreshCountryColor fault (editor)");
}

/// @brief H3: with the latch shared, a fault in one DLL stops the other; sharing never clears.
void shared_latch_tests() {
    const MsvcString text = borrowed_string("x", 1);
    reset();
    std::atomic<bool> core_latch{false};  // stands in for capie_core.dll's latch
    share_game_call_latch(nullptr);
    check(game_call_latch.load() == &game_call_faulted, "null: own latch kept");
    share_game_call_latch(&core_latch);
    check(game_call_latch.load() == &core_latch && game_calls_allowed(), "core latch adopted");
    set_text = faulting_set;
    check(!guarded_set(edit, &text), "fault in the editor");
    check(core_latch.load(), "editor fault lands in the shared latch");
    expect_core_refused("shared latch: core refused after an editor fault");

    reset();
    std::atomic<bool> other{false};
    share_game_call_latch(&other);
    other.store(true);  // the core faulted
    expect_editor_refused("shared latch: editor refused after a core fault");

    reset();
    game_call_faulted.store(true);  // the editor faulted before it adopted the core's latch
    std::atomic<bool> late{false};
    share_game_call_latch(&late);
    check(late.load() && !game_calls_allowed(), "an earlier own fault is carried into the shared latch");
    reset();
}

/**
 * @brief Scan N6: a C++ exception inside a game call latches, goes to the game's crash reporter
 *        (the unhandled-exception filter) and then ends the process; other exceptions pass on.
 */
void cpp_exception_tests() {
    reset();
    unhandled_report = &fake_report;
    end_process = &fake_end;
    reported = Reported{};
    set_text = throwing_set;
    const MsvcString text = borrowed_string("x", 1);
    check(!guarded_set(edit, &text), "C++ exception: the wrapper does not return normally");
    check(!game_calls_allowed(), "C++ exception: the latch is set");
    check(reported.reports == 1 && reported.code == cpp_exception_code, "C++ exception: the reporter saw it");
    check(reported.ends == 1 && reported.end_code == cpp_exception_code, "C++ exception: the process is ended");
    expect_editor_refused("C++ exception: every later call refused");

    // Under a debugger the reporter declines: no process end here, the search goes on.
    reset();
    reported = Reported{};
    reported.verdict = EXCEPTION_CONTINUE_SEARCH;
    refresh_edit = raising_refresh;
    bool passed_on = false;
    __try {
        static_cast<void>(guarded_refresh(edit));
    } __except (GetExceptionCode() == 0xE0000001 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        passed_on = true;
    }
    check(passed_on && game_calls_allowed() && reported.reports == 0 && reported.ends == 0,
          "other exception: passed on, no latch, no report");

    // A memory fault keeps its old path: latched, no report.
    reset();
    reported = Reported{};
    set_text = faulting_set;
    check(!guarded_set(edit, &text) && !game_calls_allowed() && reported.reports == 0 && reported.ends == 0,
          "memory fault: latched, not reported");
    unhandled_report = &UnhandledExceptionFilter;
    end_process = &terminate_process;
    reset();
}

/// @brief The temporary log of `log_tests`.
struct TempLog {
    wchar_t path[MAX_PATH]{};  ///< File path.

    /// @brief Creates the file and makes it this module's log.
    TempLog() {
        wchar_t folder[MAX_PATH]{};
        static_cast<void>(GetTempPathW(MAX_PATH, folder));
        static_cast<void>(GetTempFileNameW(folder, L"cgc", 0, path));
        capie::core::logging::log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                     nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    /// @brief Closes and deletes the file; logging is off again.
    ~TempLog() {
        CloseHandle(capie::core::logging::log_file);
        capie::core::logging::log_file = INVALID_HANDLE_VALUE;
        DeleteFileW(path);
    }
    TempLog(const TempLog&) = delete;
    TempLog& operator=(const TempLog&) = delete;
    TempLog(TempLog&&) = delete;
    TempLog& operator=(TempLog&&) = delete;

    /**
     * @brief Everything written so far.
     * @return File contents.
     */
    [[nodiscard]] std::string text() const {
        std::string out;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        char buffer[4096];
        DWORD read = 0;
        while (file != INVALID_HANDLE_VALUE && ReadFile(file, buffer, sizeof(buffer), &read, nullptr) != 0 && read != 0) {
            out.append(buffer, read);
        }
        CloseHandle(file);
        return out;
    }
};

/**
 * @brief Occurrences of a text.
 * @param text Text.
 * @param part Searched text.
 * @return Count.
 */
int occurrences(const std::string& text, const std::string& part) {
    int count = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

/// @brief CAPIE 1.7: the first fault and the first refusal are logged once each, with call, code and thread.
void log_tests() {
    const MsvcString text = borrowed_string("x", 1);
    const TempLog log;
    check(log.text().empty(), "log: empty at the start");
    reset();
    fault_logged.store(false);
    refusal_logged.store(false);
    bool valid = false;
    check(!guarded_portrait_valid(nullptr, entry, valid) && log.text().empty(),
          "log: an unresolved call without a fault writes nothing");
    set_text = faulting_set;
    check(!guarded_set(edit, &text), "log: SetText faults");
    const std::string fault_line = "CAPIE_FAULT game call SetText faulted: exception=0xC0000005 thread=" +
                                   std::to_string(GetCurrentThreadId()) + "; ";
    check(occurrences(log.text(), fault_line) == 1, "log: the fault names the call, the code and the thread");
    check(occurrences(log.text(), "\r\n") == 1, "log: one line, CR LF");
    check(guarded_find(find_edit, root, "a") == nullptr && !guarded_set(edit, &text), "log: refused calls");
    check(occurrences(log.text(), "CAPIE_FAULT game call FindWidget refused: the fault latch is set (fault in this DLL") ==
              1 && occurrences(log.text(), "\r\n") == 2,
          "log: the first refusal only, with its call");
    game_call_faulted.store(false);
    check(!guarded_set(edit, &text) && occurrences(log.text(), "faulted:") == 1, "log: a second fault is not logged");

    // A refusal after a fault of the other DLL (shared latch) says so.
    reset();
    fault_logged.store(false);
    refusal_logged.store(false);
    std::atomic<bool> other{true};
    share_game_call_latch(&other);
    check(!focus_call_null(manager) && calls.focus == 0, "log: refused by the other DLL's fault");
    check(occurrences(log.text(), "game call FocusSwitch refused: the fault latch is set (fault in the other CAPIE DLL") ==
              1,
          "log: refusal after the other DLL's fault");

    // A C++ exception is logged with its code before the reporter runs.
    reset();
    fault_logged.store(false);
    unhandled_report = &fake_report;
    end_process = &fake_end;
    reported = Reported{};
    refresh_edit = nullptr;
    set_text = throwing_set;
    check(!guarded_set(edit, &text) && occurrences(log.text(), "SetText faulted: exception=0xE06D7363 thread=") == 1,
          "log: C++ exception logged with its code");
    unhandled_report = &UnhandledExceptionFilter;
    end_process = &terminate_process;

    // The formatter: exact hex and decimal, a cut line still ends with CR LF.
    FaultLine line{};
    line.hex(0);
    line.add(" ");
    line.hex(0xC000001DUL);
    line.add(" ");
    line.decimal(0);
    line.add(" ");
    line.decimal(4294967295UL);
    check(std::string(line.text, line.size) == "0x00000000 0xC000001D 0 4294967295", "log: number formatting");
    FaultLine cut{};
    for (int i = 0; i < 40; ++i) {
        cut.add("0123456789");
    }
    check(cut.size == fault_line_capacity - 2, "log: a long line is cut");
    reset();
    fault_logged.store(false);
    refusal_logged.store(false);
}

}  // namespace

/**
 * @brief Entry point.
 * @return 0 when every check passed.
 */
int main() {
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    image_base = 0x140000000;
    widget[0] = image_base + capie::core::rva::vt_edit;
    editor_latch_tests();
    capie_read_tests();
    core_wrapper_tests();
    shared_latch_tests();
    cpp_exception_tests();
    log_tests();
    static_cast<void>(std::printf("game calls tests: %d checks, %d failures\n", checks, failures));
    return failures != 0 ? 1 : 0;
}
