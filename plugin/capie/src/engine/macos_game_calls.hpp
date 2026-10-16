/** @file macos_game_calls.hpp
 * @brief SysV call boundary, inactive until a verified owning UI thread is admitted.
 * Evidence: docs/macos/editor-map.md. This is NOT Windows SEH fault recovery.
 * Production must establish object lifetimes and fault policy before admission; no runtime currently does so.
 */
#pragma once
#include "../core/platform.hpp"
#include "../core/platform_game_layout.hpp"
#include "platform_string.hpp"
#include <atomic>
namespace capie::engine {
inline std::uintptr_t image_base = 0;
inline std::atomic<unsigned long> native_call_thread{0};
using FindWidget = void* (*)(void*, const EngineString*, bool);
using SetText = void (*)(void*, const EngineString*);
using RefreshEdit = void (*)(void*);
using CaptionRequest = void (*)(void*, int);
using FocusSwitch = void (*)(void*, void*);
inline FindWidget find_edit = nullptr, find_instant = nullptr;
inline SetText set_text = nullptr;
inline RefreshEdit refresh_edit = nullptr;
inline CaptionRequest caption_request = nullptr;
inline FocusSwitch focus_switch = nullptr;
/// @brief Admission is separate from memory readability; tests admit only their owned fixture thread.
inline bool native_call_admitted(const void* object) noexcept {
    const auto expected = native_call_thread.load(std::memory_order_acquire);
    std::uintptr_t copied = 0;
    return expected != 0 && expected == core::platform::thread_id() &&
           core::guarded_copy(&copied, object, sizeof(copied));
}
inline void* guarded_find(FindWidget lookup, void* root, const char* name) noexcept {
    if (!lookup || (lookup != find_edit && lookup != find_instant) || !name || !native_call_admitted(root)) return nullptr;
    const auto size = strnlen(name, max_name_bytes + 1);
    if (size > max_name_bytes) return nullptr;
    core::uptr owner = 0, dummy = 0;
    if (!core::member(root, core::off::root_dummy_owner, owner) || owner == 0 ||
        !core::add_offset(owner, lookup == find_edit ? core::off::dummy_edit : core::off::dummy_instant, dummy)) return nullptr;
    const auto text = borrowed_string(name, size);
    void* result = lookup(root, &text, false);
    return reinterpret_cast<core::uptr>(result) == dummy ? nullptr : result;
}
inline bool guarded_set(void* edit, const EngineString* text) noexcept {
    core::uptr buffer = 0;
    if (!set_text || !text || !native_call_admitted(edit) ||
        !core::add_offset(reinterpret_cast<core::uptr>(edit), core::off::edit_buffer, buffer)) return false;
    set_text(reinterpret_cast<void*>(buffer), text);
    return true;
}
inline bool guarded_refresh(void* edit) noexcept {
    if (!refresh_edit || !native_call_admitted(edit)) return false;
    refresh_edit(edit);
    return true;
}
inline bool caption_call(void* gui, int index) noexcept {
    if (!caption_request || index != core::value::politics_view_index || !native_call_admitted(gui)) return false;
    caption_request(gui, index);
    return true;
}
inline bool focus_call_null(void* manager) noexcept {
    if (!focus_switch || !native_call_admitted(manager)) return false;
    focus_switch(manager, nullptr);
    return true;
}
} // namespace capie::engine
