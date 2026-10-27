/**
 * @file game_calls.hpp
 * @brief Typed pointers to the game functions CAPIE calls, and SEH-guarded wrappers around them.
 *
 * CAPIE calls exactly six game functions, all from the UI thread after the original Tick
 * has returned:
 *   FindEdit / FindInstantText (widget lookup by name), SetText (prefill), the edit-box
 *   Refresh (prefill), RequestViewRefresh(gui, 12) (caption refresh) and
 *   FocusSwitch(manager, nullptr) (focus release).
 * Each pointer is null until initialization verified the function's signature and the
 * image hash; a wrapper with a null pointer returns failure. Each call runs under an SEH
 * filter for access violations, so a stale object makes the call fail instead of
 * crashing the game. No lock is held while a game function runs (callers guarantee it).
 *
 * `image_base` is the base of the running `hoi4.exe`, set once at initialization (tests
 * set it to an owned fake image). It is a scalar, not a borrowed pointer.
 */
#pragma once
#include "../core/game_layout.hpp"
#include "../core/memory.hpp"
#include "msvc_string.hpp"

#include <atomic>
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
 * maps and reads the name as a pointer and an int32 length (`23319D0`: `mov r9,[rdx]`,
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

/// @brief FindEdit (23331C0, map root+13F0); set at initialization.
inline FindWidget find_edit = nullptr;
/// @brief FindInstantText (2334D40, map root+1380); set at initialization.
inline FindWidget find_instant = nullptr;
/// @brief SetText thunk (22F27C0); set at initialization.
inline SetText set_text = nullptr;
/// @brief CEditBox refresh (22F4D80); set at initialization.
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
 */
inline std::atomic<bool> game_call_faulted{false};

/**
 * @brief Records a fault inside a game call (see `game_call_faulted`).
 * @return `EXCEPTION_EXECUTE_HANDLER` when `code` is a memory fault, else `EXCEPTION_CONTINUE_SEARCH`.
 * @param code Exception code.
 */
inline int game_call_exception(unsigned long code) noexcept {
    const int verdict = memory_exception(code);
    if (verdict == EXCEPTION_EXECUTE_HANDLER) {
        game_call_faulted.store(true, std::memory_order_release);
    }
    return verdict;
}

/**
 * @brief Whether CAPIE may still call the game.
 * @return `false` after a fault inside a game call.
 */
[[nodiscard]] inline bool game_calls_allowed() noexcept {
    return !game_call_faulted.load(std::memory_order_acquire);
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
        return nullptr;
    }
    const NameView view{name, static_cast<std::int32_t>(std::strlen(name))};
    const std::uintptr_t expected = expected_widget_vtable(lookup);
    void* widget = nullptr;
    __try {
        widget = lookup(root, &view, false, false);
    } __except (game_call_exception(GetExceptionCode())) {
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
        return false;
    }
    __try {
        set_text(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit) + core::off::edit_buffer), text);
        return true;
    } __except (game_call_exception(GetExceptionCode())) {
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
        return false;
    }
    __try {
        refresh_edit(edit);
        return true;
    } __except (game_call_exception(GetExceptionCode())) {
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
        return false;
    }
    __try {
        caption_request(gui, index);
        return true;
    } __except (game_call_exception(GetExceptionCode())) {
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
        return false;
    }
    __try {
        focus_switch(manager, nullptr);
        return true;
    } __except (game_call_exception(GetExceptionCode())) {
        return false;
    }
}

}  // namespace capie::engine
