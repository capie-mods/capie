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
#if defined(__APPLE__)
#include "macos_game_calls.hpp"
#else
#include "../core/game_layout.hpp"
#include "../core/memory.hpp"
#include "msvc_string.hpp"

#include <cstdint>
#include <cstring>

namespace capie::engine {

using core::memory_exception;

/// @brief Base address of the running game image; zero until initialization.
inline std::uintptr_t image_base = 0;

/// @brief `widget* (root*, const string* name, bool, bool)`.
using FindWidget = void*(__fastcall*)(void*, const MsvcString*, bool, bool);
/// @brief `void (textbuffer*, const string*)`.
using SetText = void(__fastcall*)(void*, const MsvcString*);
/// @brief `void (CEditBox*)`.
using RefreshEdit = void(__fastcall*)(void*);
/// @brief `void (gui*, int view_index)` (B6E210).
using CaptionRequest = void(__fastcall*)(void*, int);
/// @brief `void (manager*, widget*)` (225E320).
using FocusSwitch = void(__fastcall*)(void*, void*);

/// @brief FindEdit (22BAD40); set at initialization.
inline FindWidget find_edit = nullptr;
/// @brief FindInstantText (22BC600); set at initialization.
inline FindWidget find_instant = nullptr;
/// @brief SetText thunk (22F27C0); set at initialization.
inline SetText set_text = nullptr;
/// @brief CEditBox refresh (22F4D80); set at initialization.
inline RefreshEdit refresh_edit = nullptr;
/// @brief RequestViewRefresh (B6E210); set when the caption feature is armed.
inline CaptionRequest caption_request = nullptr;
/// @brief FocusSwitch (225E320); set when the focus feature is armed.
inline FocusSwitch focus_switch = nullptr;

/**
 * @brief Looks a child widget up by name under SEH.
 * @param lookup FindEdit or FindInstantText.
 * @param root Root widget.
 * @param name Widget name.
 * @return Widget or null (also on fault). The result is used within the current call only.
 */
inline void* guarded_find(FindWidget lookup, void* root, const char* name) noexcept {
    const auto text = borrowed_string(name, std::strlen(name));
    __try {
        return lookup(root, &text, false, false);
    } __except (memory_exception(GetExceptionCode())) {
        return nullptr;
    }
}

/**
 * @brief Calls SetText on an edit box's text buffer under SEH.
 * @param edit CEditBox.
 * @param text New text (borrowed for the call).
 * @return `false` on fault.
 */
inline bool guarded_set(void* edit, const MsvcString* text) noexcept {
    __try {
        set_text(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit) + core::off::edit_buffer), text);
        return true;
    } __except (memory_exception(GetExceptionCode())) {
        return false;
    }
}

/**
 * @brief Calls the native edit-box refresh under SEH.
 * @param edit CEditBox.
 * @return `false` when unresolved or on fault.
 */
inline bool guarded_refresh(void* edit) noexcept {
    if (refresh_edit == nullptr) {
        return false;
    }
    __try {
        refresh_edit(edit);
        return true;
    } __except (memory_exception(GetExceptionCode())) {
        return false;
    }
}

/**
 * @brief Requests a politics-view refresh: `B6E210(gui, 12)` under SEH.
 * @param gui GUI container (freshly resolved by the caller).
 * @param index View index; anything other than 12 is refused.
 * @return `false` when unresolved, wrong index or fault.
 */
inline bool caption_call(void* gui, int index) noexcept {
    if (caption_request == nullptr || index != core::value::politics_view_index) {
        return false;
    }
    __try {
        caption_request(gui, index);
        return true;
    } __except (memory_exception(GetExceptionCode())) {
        return false;
    }
}

/**
 * @brief Releases keyboard focus: `225E320(manager, nullptr)` under SEH.
 * @param manager Focus manager (freshly read from the global).
 * @return `false` when unresolved or on fault.
 */
inline bool focus_call_null(void* manager) noexcept {
    if (focus_switch == nullptr) {
        return false;
    }
    __try {
        focus_switch(manager, nullptr);
        return true;
    } __except (memory_exception(GetExceptionCode())) {
        return false;
    }
}

}  // namespace capie::engine

#endif
