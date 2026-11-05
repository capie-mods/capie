/**
 * @file game_calls.hpp
 * @brief Typed pointers to the game functions CAPIE calls, and SEH-guarded wrappers around them.
 *
 * CAPIE calls ten game functions, each only through a wrapper in this file.
 *
 * `capie_editor.dll`, from the UI thread after the original Tick has returned:
 *   FindEdit / FindInstantText (widget lookup by name), SetText (prefill), the edit-box
 *   Refresh (prefill), RequestViewRefresh(gui, 12) (caption refresh) and
 *   FocusSwitch(manager, nullptr) (focus release).
 * `capie_core.dll`, inside its own hook bodies: PortraitEntryValid and the game's string
 *   copy (portrait and faction layers), the UpdateNames trampoline when a `capie_cn_rev`
 *   commit refreshes a changed country (the call the game itself makes is not wrapped), and
 *   RefreshCountryColor when the colour layer of a country changes (logic thread, in game only).
 *
 * Each pointer is null until initialization verified the function's signature and the
 * image hash; a wrapper with a null pointer returns failure. Each call runs under an SEH
 * filter for access violations, so a stale object makes the call fail instead of
 * crashing the game. No lock is held while a game function runs (callers guarantee it).
 *
 * A C++ exception thrown inside one of these calls (scan N6; in practice only the game's
 * fatal kinds: allocation failure, length error, a broken variant) cannot pass through
 * CAPIE's `noexcept` callers: `std::terminate` would end the process with a `__fastfail` that
 * skips the game's crash reporter. The filter therefore latches and hands the exception to
 * the process's unhandled-exception filter, which is the game's reporter (minidump.dmp and
 * exception.txt), exactly as if nothing had caught it, and then ends the process the way
 * Windows does after an unhandled exception. Under a debugger the reporter declines and the
 * exception continues to the debugger.
 *
 * Fault latch: the first fault inside a game call stops every later game call of BOTH DLLs.
 * Each DLL has its own copy of the inline variables here, so the core exports its latch
 * (`game_call_latch_export`) and the editor adopts it at initialization
 * (`share_game_call_latch`). The core always loads first (proxy and launcher); an editor
 * without a loaded core keeps its own latch, and then has no core calls to stop.
 *
 * Log (CAPIE 1.7). The first fault a module's filter sees is written once to that module's log
 * (`capie_core.log` or `capie_editor.log`): which call, the exception code and the thread. The
 * first call the latch refuses is written once as well, so a log that shows a refusal without a
 * fault line says the other DLL faulted. The fault line is written from the filter, before the
 * game's frames unwind: one `WriteFile` of a stack buffer formatted by hand, no CRT, no
 * allocation and no lock (an appended line of one write does not interleave).
 *
 * Why the latch is not narrowed (CAPIE 1.7 review). A fault in the colour or portrait layer also
 * stops the text editors. A narrower latch (one per layer) was considered and rejected: the
 * filter unwinds the game's frames without running their destructors, so a lock the game held
 * (the GUI's, the map's, the allocator's) or a half-updated object may be left behind, and that
 * state is shared by every game function, not owned by the layer that faulted. Which locks a
 * given game function takes is not pinned, so no call can be shown to be independent of
 * another. Keeping every game call off is the only safe answer; the player is told to restart.
 *
 * `image_base` is the base of the running `hoi4.exe`, set once at initialization (tests
 * set it to an owned fake image). It is a scalar, not a borrowed pointer.
 */
#pragma once
#include "../core/game_layout.hpp"
#include "../core/log.hpp"
#include "../core/memory.hpp"
#include "msvc_string.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace capie::engine {

using core::memory_exception;

/// @brief Base address of the running game image; zero until initialization.
inline std::uintptr_t image_base = 0;

/**
 * @brief Name argument of the widget lookups: `{const char*, int32}` passed by pointer (16 bytes).
 *
 * 1.19.4.0 ("reduced string allocations in GUI system") looks children up in per-type name hash
 * maps and reads the name as a pointer and an int32 length (map lookup helper: `mov r9,[rdx]`,
 * `mov eax,[rdx+8]`), no longer as an MSVC `std::string`.
 */
struct NameView {
    const char* data;         ///< Name bytes (not necessarily NUL-terminated for the game).
    std::int32_t size;        ///< Length in bytes.
    std::int32_t padding{0};  ///< Keeps the 16-byte shape the game copies with `movups`.
};
static_assert(sizeof(NameView) == 16);

/// @brief `widget* (root*, const NameView* name, bool log_a, bool log_b)`.
using FindWidget = void*(__fastcall*)(void*, const NameView*, bool, bool);
/// @brief `void (textbuffer*, const string*)`.
using SetText = void(__fastcall*)(void*, const MsvcString*);
/// @brief `void (CEditBox*)`.
using RefreshEdit = void(__fastcall*)(void*);
/// @brief `void (gui*, int view_index)` (RequestViewRefresh, `rva::request_view_refresh`).
using CaptionRequest = void(__fastcall*)(void*, int);
/// @brief `void (manager*, widget*)` (FocusSwitch, `rva::focus_switch`).
using FocusSwitch = void(__fastcall*)(void*, void*);

/// @brief `bool PortraitEntryValid(const entry*)` (`rva::portrait_valid`).
using PortraitValid = bool (*)(const void*);
/// @brief `string* string_copy(string* out, const string* source)` (`rva::string_copy`, copy-construct).
using StringCopy = void* (*)(void*, const void*);
/// @brief `u64 UpdateNames(CCountry*)` (`rva::update_names`; CAPIE calls its trampoline).
using UpdateNames = std::uint64_t (*)(std::uintptr_t);
/// @brief `void RefreshCountryColor(map*, const tag*)` (`rva::refresh_country_color`).
using RefreshColour = void (*)(void*, const std::int32_t*);
/// @brief `entry* PathPortraitEntry(entry* out, const string* path, bool warn, bool flag)` (`rva::portrait_path_entry`).
using PathPortraitEntry = void* (*)(void*, const void*, bool, bool);
/// @brief `void ProvinceName(province*, string* out, bool* is_vp)` (`rva::province_name`; returns `out`).
using ProvinceName = void* (*)(std::uintptr_t, void*, bool*);
/// @brief `void operator delete(void*, size_t)` of the game's CRT (`rva::game_delete`).
using GameDelete = void (*)(void*, std::size_t);

/// @brief FindEdit (`rva::find_edit`, map root+13F0); set at initialization.
inline FindWidget find_edit = nullptr;
/// @brief FindInstantText (`rva::find_instant`, map root+1380); set at initialization.
inline FindWidget find_instant = nullptr;
/// @brief SetText thunk (`rva::text_buffer_set`); set at initialization.
inline SetText set_text = nullptr;
/// @brief CEditBox refresh (`rva::edit_refresh`); set at initialization.
inline RefreshEdit refresh_edit = nullptr;
/// @brief RequestViewRefresh (`rva::request_view_refresh`); set when the caption feature is armed.
inline CaptionRequest caption_request = nullptr;
/// @brief FocusSwitch (`rva::focus_switch`); set when the focus feature is armed.
inline FocusSwitch focus_switch = nullptr;

/**
 * @brief Set by the first access violation caught INSIDE a game call (not a CAPIE read).
 *
 * The SEH filter unwinds the game's own frames, so a lock the game held or a half-updated object
 * may be left behind. After that CAPIE never calls the game again until the next start: every
 * guarded call below returns failure, and every feature already treats a failure as "do nothing".
 * This is the latch of the module it is compiled into; `game_call_latch` says which one is used.
 */
inline std::atomic<bool> game_call_faulted{false};
static_assert(std::atomic<bool>::is_always_lock_free);

/// @brief The latch in use: this module's own, or the core DLL's after `share_game_call_latch`.
inline std::atomic<std::atomic<bool>*> game_call_latch{&game_call_faulted};

/// @brief Export of `capie_core.dll` that returns its latch (`std::atomic<bool>*` as `void*`).
inline constexpr char game_call_latch_export[] = "RNGameCallLatch";

/**
 * @brief Makes this module use another module's latch (editor initialization, before any hook).
 *
 * A fault this module already latched is carried over, so sharing never clears a latch.
 * @param shared The core DLL's latch; null keeps the own latch.
 */
inline void share_game_call_latch(std::atomic<bool>* shared) noexcept {
    if (shared == nullptr) {
        return;
    }
    if (game_call_faulted.load(std::memory_order_acquire)) {
        shared->store(true, std::memory_order_release);
    }
    game_call_latch.store(shared, std::memory_order_release);
}

/// @brief The game functions CAPIE calls, named in the fault and refusal log lines.
enum class GameCall : unsigned {
    find_widget,      ///< FindEdit or FindInstantText.
    set_text,         ///< SetText (prefill).
    refresh_edit,     ///< CEditBox refresh (prefill).
    caption_request,  ///< RequestViewRefresh (caption refresh).
    focus_switch,     ///< FocusSwitch (focus release).
    portrait_valid,   ///< PortraitEntryValid (portrait layer).
    string_copy,      ///< The game's string copy (portrait and faction layers).
    update_names,     ///< UpdateNames trampoline (name layer commit).
    refresh_colour,   ///< RefreshCountryColor (colour layer).
};

/**
 * @brief Log name of a game call.
 * @param call Call.
 * @return Static ASCII name.
 */
constexpr const char* game_call_name(GameCall call) noexcept {
    switch (call) {
        case GameCall::find_widget:
            return "FindWidget";
        case GameCall::set_text:
            return "SetText";
        case GameCall::refresh_edit:
            return "RefreshEdit";
        case GameCall::caption_request:
            return "RequestViewRefresh";
        case GameCall::focus_switch:
            return "FocusSwitch";
        case GameCall::portrait_valid:
            return "PortraitEntryValid";
        case GameCall::string_copy:
            return "StringCopy";
        case GameCall::update_names:
            return "UpdateNames";
        case GameCall::refresh_colour:
            return "RefreshCountryColor";
    }
    return "unknown";
}

/// @brief Set once this module wrote its fault line (one per session and module).
inline std::atomic<bool> fault_logged{false};
/// @brief Set once this module wrote its refusal line (one per session and module).
inline std::atomic<bool> refusal_logged{false};

/// @brief Size of a fault or refusal line.
inline constexpr std::size_t fault_line_capacity = 224;

/**
 * @brief A log line built without the CRT (usable inside an exception filter).
 *
 * Text that does not fit is cut; the line always ends with CR LF.
 */
struct FaultLine {
    char text[fault_line_capacity]{};  ///< Bytes.
    std::size_t size{};                ///< Bytes used.

    /**
     * @brief Appends NUL-terminated text.
     * @param part Text.
     */
    void add(const char* part) noexcept {
        for (; part != nullptr && *part != '\0' && size < fault_line_capacity - 2; ++part) {
            text[size++] = *part;
        }
    }

    /**
     * @brief Appends `0x` and eight upper-case hex digits.
     * @param value Value (the low 32 bits).
     */
    void hex(unsigned long value) noexcept {
        constexpr char digits[] = "0123456789ABCDEF";
        char buffer[11] = {'0', 'x'};
        for (unsigned i = 0; i < 8; ++i) {
            buffer[9 - i] = digits[(value >> (4U * i)) & 0xFU];
        }
        buffer[10] = '\0';
        add(buffer);
    }

    /**
     * @brief Appends a decimal number.
     * @param value Value.
     */
    void decimal(unsigned long value) noexcept {
        char buffer[16]{};
        std::size_t at = sizeof(buffer) - 1;
        do {
            buffer[--at] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && at != 0);
        add(buffer + at);
    }

    /// @brief Ends the line with CR LF and writes it with one `WriteFile` (no lock, no allocation).
    void write() noexcept {
        text[size++] = '\r';
        text[size++] = '\n';
        core::logging::write_bytes(text, size);
    }
};

/**
 * @brief Writes this module's fault line once: the call, the exception code and the thread.
 * @param call Faulting call.
 * @param code Exception code.
 * @note Runs inside the SEH filter: no CRT, no allocation, no lock.
 */
inline void note_game_call_fault(GameCall call, unsigned long code) noexcept {
    if (fault_logged.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    FaultLine line{};
    line.add("CAPIE_FAULT game call ");
    line.add(game_call_name(call));
    line.add(" faulted: exception=");
    line.hex(code);
    line.add(" thread=");
    line.decimal(GetCurrentThreadId());
    line.add("; CAPIE calls the game no more until it restarts (editors answer -4, CAPIE stopped)");
    line.write();
}

/**
 * @brief Writes this module's refusal line once, when the latch refuses a call.
 * @param call Refused call.
 */
inline void note_game_call_refused(GameCall call) noexcept {
    if (refusal_logged.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    FaultLine line{};
    line.add("CAPIE_FAULT game call ");
    line.add(game_call_name(call));
    line.add(" refused: the fault latch is set (");
    line.add(fault_logged.load(std::memory_order_acquire) ? "fault in this DLL, see above"
                                                          : "fault in the other CAPIE DLL, see its log");
    line.add("); later refusals are not logged");
    line.write();
}

/// @brief Exception code of an MSVC C++ `throw` (`'msc' | 0xE0000000`).
inline constexpr unsigned long cpp_exception_code = 0xE06D7363;

/// @brief `LONG UnhandledExceptionFilter(EXCEPTION_POINTERS*)`: runs the process's top-level filter.
using UnhandledReport = LONG(WINAPI*)(EXCEPTION_POINTERS*);
/// @brief Ends the process after an exception that was reported as unhandled.
using EndProcess = void (*)(unsigned long code);

/**
 * @brief Default `EndProcess`: what Windows does after an unhandled exception.
 * @param code Exception code, used as the exit code.
 */
inline void terminate_process(unsigned long code) noexcept {
    static_cast<void>(TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code)));
}

/// @brief Reporter of a C++ exception inside a game call (the game's crash reporter); tests replace it.
inline UnhandledReport unhandled_report = &UnhandledExceptionFilter;
/// @brief Called when the reporter asks for the process to end; tests replace it.
inline EndProcess end_process = &terminate_process;

/**
 * @brief SEH filter of every game call: latches memory faults and reports C++ exceptions.
 *
 * Memory fault: latch, then the wrapper returns failure. C++ exception: latch, then the
 * game's reporter sees it with the throw's own context (nothing is unwound yet); when the
 * reporter asks for the end of the process (`EXCEPTION_EXECUTE_HANDLER`), the process ends here.
 * Any other verdict (a debugger, or a test's `end_process` that returns) is passed on.
 * Every other exception continues its normal search. Either latching kind is logged once first.
 * @param info Exception information from `GetExceptionInformation()`.
 * @param call The game call the wrapper made (for the log line).
 * @return The SEH verdict.
 */
inline int game_call_exception(EXCEPTION_POINTERS* info, GameCall call) noexcept {
    const unsigned long code =
        info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
    if (memory_exception(code) == EXCEPTION_EXECUTE_HANDLER || code == cpp_exception_code) {
        note_game_call_fault(call, code);
    }
    if (memory_exception(code) == EXCEPTION_EXECUTE_HANDLER) {
        game_call_latch.load(std::memory_order_acquire)->store(true, std::memory_order_release);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (code != cpp_exception_code) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    game_call_latch.load(std::memory_order_acquire)->store(true, std::memory_order_release);
    const LONG verdict = unhandled_report(info);
    if (verdict == EXCEPTION_EXECUTE_HANDLER) {
        end_process(code);
        return EXCEPTION_EXECUTE_HANDLER;  // only reached when a test's end_process returns
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * @brief Whether CAPIE may still call the game.
 * @return `false` after a fault inside a game call of either DLL (once the latch is shared).
 */
[[nodiscard]] inline bool game_calls_allowed() noexcept {
    return !game_call_latch.load(std::memory_order_acquire)->load(std::memory_order_acquire);
}

/**
 * @brief After a refused wrapper call: logs the first refusal when the latch was the reason.
 * @param call The refused call.
 */
inline void refused_note(GameCall call) noexcept {
    if (!game_calls_allowed()) {
        note_game_call_refused(call);
    }
}

/**
 * @brief Vtable RVA a lookup's result must have.
 * @param lookup FindEdit or FindInstantText.
 * @return `vt_edit`, `vt_instant_text`, or 0 for any other lookup (no check).
 */
inline std::uintptr_t expected_widget_vtable(FindWidget lookup) noexcept {
    if (lookup != nullptr && lookup == find_edit) {
        return core::rva::vt_edit;
    }
    if (lookup != nullptr && lookup == find_instant) {
        return core::rva::vt_instant_text;
    }
    return 0;
}

/**
 * @brief Looks a child widget up by name under SEH.
 *
 * A missing name no longer yields null: 1.19.4.0 returns a shared null-object widget
 * (`CNullEditBox` for edits, from `[[root+98]+slot]`). The result is accepted only when its
 * vtable is the one of the type looked up, so the null object reads as "not found".
 * @param lookup FindEdit or FindInstantText.
 * @param root Root widget.
 * @param name Widget name.
 * @return Widget or null (also on fault or a null object). The result is used within the current call only.
 */
inline void* guarded_find(FindWidget lookup, void* root, const char* name) noexcept {
    if (lookup == nullptr || root == nullptr || name == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::find_widget);
        return nullptr;
    }
    const NameView view{name, static_cast<std::int32_t>(std::strlen(name))};
    const std::uintptr_t expected = expected_widget_vtable(lookup);
    void* widget = nullptr;
    __try {
        widget = lookup(root, &view, false, false);
    } __except (game_call_exception(GetExceptionInformation(), GameCall::find_widget)) {
        return nullptr;
    }
    // The type check is a CAPIE read: guarded, and never counted as a game-call fault.
    std::uintptr_t vtable = 0;
    if (widget != nullptr && expected != 0 &&
        (!core::ProcessMemory::read(reinterpret_cast<std::uintptr_t>(widget), &vtable, sizeof(vtable)) ||
         vtable != image_base + expected)) {
        return nullptr;
    }
    return widget;
}

/**
 * @brief Calls SetText on an edit box's text buffer under SEH.
 * @param edit CEditBox.
 * @param text New text (borrowed for the call).
 * @return `false` on fault.
 */
inline bool guarded_set(void* edit, const MsvcString* text) noexcept {
    if (set_text == nullptr || edit == nullptr || text == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::set_text);
        return false;
    }
    __try {
        set_text(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit) + core::off::edit_buffer), text);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::set_text)) {
        return false;
    }
}

/**
 * @brief Calls the native edit-box refresh under SEH.
 * @param edit CEditBox.
 * @return `false` when unresolved or on fault.
 */
inline bool guarded_refresh(void* edit) noexcept {
    if (refresh_edit == nullptr || edit == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::refresh_edit);
        return false;
    }
    __try {
        refresh_edit(edit);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::refresh_edit)) {
        return false;
    }
}

/**
 * @brief Requests a politics-view refresh: `RequestViewRefresh(gui, 12)` under SEH.
 * @param gui GUI container (freshly resolved by the caller).
 * @param index View index; anything other than 12 is refused.
 * @return `false` when unresolved, wrong index or fault.
 */
inline bool caption_call(void* gui, int index) noexcept {
    if (caption_request == nullptr || gui == nullptr || index != core::value::politics_view_index ||
        !game_calls_allowed()) {
        refused_note(GameCall::caption_request);
        return false;
    }
    __try {
        caption_request(gui, index);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::caption_request)) {
        return false;
    }
}

/**
 * @brief Releases keyboard focus: `FocusSwitch(manager, nullptr)` under SEH.
 * @param manager Focus manager (freshly read from the global).
 * @return `false` when unresolved or on fault.
 */
inline bool focus_call_null(void* manager) noexcept {
    if (focus_switch == nullptr || manager == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::focus_switch);
        return false;
    }
    __try {
        focus_switch(manager, nullptr);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::focus_switch)) {
        return false;
    }
}

/**
 * @brief Calls PortraitEntryValid under SEH (portrait hook of `capie_core.dll`).
 * @param valid The game's validity check (the layer's slot; null = unresolved).
 * @param entry Entry built by CAPIE.
 * @param result Receives the game's answer; `false` unless the call returned.
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_portrait_valid(PortraitValid valid, const void* entry, bool& result) noexcept {
    result = false;
    if (valid == nullptr || entry == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::portrait_valid);
        return false;
    }
    __try {
        result = valid(entry);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::portrait_valid)) {
        return false;
    }
}

/**
 * @brief Copy-constructs a game string with the game's own copy under SEH.
 *
 * On a fault `out` may hold a half-constructed string; the caller must then let the original
 * construct it (both callers fall through to their original, which writes `out` from scratch).
 * @param copy The game's string copy (null = unresolved).
 * @param out Uninitialised string storage.
 * @param source Source string (borrowed for the call).
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_string_copy(StringCopy copy, void* out, const void* source) noexcept {
    if (copy == nullptr || out == nullptr || source == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::string_copy);
        return false;
    }
    __try {
        copy(out, source);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::string_copy)) {
        return false;
    }
}

/**
 * @brief Calls the UpdateNames trampoline for a country CAPIE refreshes, under SEH.
 * @param update Trampoline (null = unresolved).
 * @param country Country object.
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_update_names(UpdateNames update, std::uintptr_t country) noexcept {
    if (update == nullptr || country == 0 || !game_calls_allowed()) {
        refused_note(GameCall::update_names);
        return false;
    }
    __try {
        static_cast<void>(update(country));
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::update_names)) {
        return false;
    }
}

/**
 * @brief Builds a path portrait entry with the game's builder under SEH (portrait layer format 2).
 *
 * The builder copies the path, makes a kind-1 entry and loads the texture at once through the
 * texture manager (`MakeIconEntry`, which takes the manager's own mutex). Logic thread only, as
 * `create_country_leader` does. On a fault `out` may be half-constructed; the caller then discards
 * it (never destroys it) and the layer is turned off by the latch.
 * @param build The game's builder (null = unresolved).
 * @param out Uninitialised entry storage (0x30 bytes).
 * @param path Path string (borrowed for the call; the builder copies it).
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_path_portrait_entry(PathPortraitEntry build, void* out, const MsvcString* path) noexcept {
    if (build == nullptr || out == nullptr || path == nullptr || !game_calls_allowed()) {
        return false;
    }
    __try {
        static_cast<void>(build(out, path, false, false));
        return true;
    } __except (game_call_exception(GetExceptionInformation())) {
        return false;
    }
}

/**
 * @brief Calls ProvinceName (the name the game shows for a province) under SEH.
 *
 * The result is constructed in `out` by the game; the caller copies the bytes and releases it with
 * `guarded_string_release`. On a fault `out` may hold a half-built string and is abandoned.
 * @param name The game's function (null = unresolved).
 * @param province Province object.
 * @param out Uninitialised string storage.
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_province_name(ProvinceName name, std::uintptr_t province, MsvcString* out) noexcept {
    if (name == nullptr || province == 0 || out == nullptr || !game_calls_allowed()) {
        return false;
    }
    bool is_vp = false;
    __try {
        static_cast<void>(name(province, out, &is_vp));
        return true;
    } __except (game_call_exception(GetExceptionInformation())) {
        return false;
    }
}

/// @brief Allocations of at least this many bytes carry the MSVC alignment header (`_Big_allocation_threshold`).
inline constexpr std::uint64_t big_allocation_threshold = 0x1000;
/// @brief Extra bytes of a big allocation (`_Non_user_size`: 8-byte back pointer plus 31 alignment bytes).
inline constexpr std::uint64_t big_allocation_extra = 0x27;
/// @brief Largest distance between the user pointer and the real block of a big allocation.
inline constexpr std::uint64_t big_allocation_slack = 0x1F;

/**
 * @brief Frees a game-owned MSVC string the way the game's inlined destructor does (`MakeIconEntry`).
 *
 * Inline strings own nothing. A heap string goes to the game's sized `operator delete`; a block of
 * 4 KiB or more is first walked back to its real start through the pointer stored before it. A
 * header that fails the game's own sanity check is leaked instead (the game would abort).
 * @param free_block The game's `operator delete` (null = leak).
 * @param value String to release; reset to empty afterwards.
 * @return `false` when the string was leaked (unresolved, latched, bad header or fault).
 */
inline bool guarded_string_release(GameDelete free_block, MsvcString* value) noexcept {
    if (value == nullptr) {
        return false;
    }
    const std::uint64_t capacity = value->capacity;
    if (capacity <= sso_capacity) {
        *value = MsvcString{};
        value->capacity = sso_capacity;
        return true;
    }
    if (free_block == nullptr || !game_calls_allowed() || capacity > max_string_capacity) {
        return false;
    }
    auto block = reinterpret_cast<std::uintptr_t>(value->data.pointer);
    std::uint64_t bytes = capacity + 1;
    if (bytes >= big_allocation_threshold) {
        std::uintptr_t real = 0;
        if (block < sizeof(real) || !core::ProcessMemory::read(block - sizeof(real), &real, sizeof(real)) ||
            real > block - sizeof(real) || block - real - sizeof(real) > big_allocation_slack) {
            return false;
        }
        bytes += big_allocation_extra;
        block = real;
    }
    __try {
        free_block(reinterpret_cast<void*>(block), static_cast<std::size_t>(bytes));
    } __except (game_call_exception(GetExceptionInformation())) {
        return false;
    }
    *value = MsvcString{};
    value->capacity = sso_capacity;
    return true;
}

/**
 * @brief Calls RefreshCountryColor under SEH (colour layer of `capie_core.dll`, logic thread).
 *
 * The caller has checked that the game is in a campaign (the current idler's in-game slot is
 * the `CInGameIdler` one, compared by address) and that the map exists; this wrapper adds the
 * null checks and the shared latch.
 * @param refresh The game's refresh (null = unresolved).
 * @param map Map object, freshly read from its global.
 * @param tag The country's tag field (`country + off::country_tag`), as the game passes it.
 * @return `false` when unresolved, refused by the latch or on fault.
 */
inline bool guarded_refresh_colour(RefreshColour refresh, void* map, const std::int32_t* tag) noexcept {
    if (refresh == nullptr || map == nullptr || tag == nullptr || !game_calls_allowed()) {
        refused_note(GameCall::refresh_colour);
        return false;
    }
    __try {
        refresh(map, tag);
        return true;
    } __except (game_call_exception(GetExceptionInformation(), GameCall::refresh_colour)) {
        return false;
    }
}

}  // namespace capie::engine
