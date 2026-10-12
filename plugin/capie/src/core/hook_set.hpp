/**
 * @file hook_set.hpp
 * @brief Installs a fixed set of MinHook detours with all-or-nothing activation.
 *
 * Both DLLs install their hooks through this file. It offers two variants because the two
 * DLLs differ in one observable way:
 *   - `install_all_at_once` (capie_core.dll): create every hook, then one
 *     `MH_EnableHook(MH_ALL_HOOKS)`; MinHook freezes threads once.
 *   - `install_in_order` (capie_editor.dll): create every hook, then enable them one by
 *     one in a caller-given order, so the per-frame Tick hook goes live last.
 *
 * Failure handling is the same in both variants: if a hook
 * cannot be created, `MH_Uninitialize` and code 31; if activation fails, every hook is
 * disabled best-effort and code 32. Trampolines are never freed after a failed activation
 * (MinHook is not uninitialized), because a callback might already be running on another
 * thread and would return into freed memory. The process needs a restart after any
 * failure; there is no unload or retry.
 *
 * Hook bodies themselves live in the DLL composition roots. Contract for every body:
 * call the original exactly once, never under a lock, and pass its result through.
 */
#pragma once
#include "init_codes.hpp"
#include "memory.hpp"

#include <MinHook.h>

#include <cstddef>

namespace capie::core {

/// @brief One detour: target RVA, replacement function and the trampoline slot.
struct HookSpec {
    const char* id;   ///< Identifier, e.g. "editor.tick"; matches the signature id.
    uptr rva;         ///< Target RVA inside the game image.
    void* detour;     ///< Replacement function.
    void** original;  ///< Receives the trampoline (call it to run the original).
};

/**
 * @brief Creates all hooks; on failure uninitializes MinHook.
 * @param base Image base.
 * @param hooks Hook list.
 * @param count Number of hooks.
 * @return `ok`, `minhook_init_failed` (30) or `hook_create_failed` (31).
 */
inline InitCode create_hooks(unsigned char* base, const HookSpec* hooks, std::size_t count) noexcept {
    if (MH_Initialize() != MH_OK) {
        return InitCode::minhook_init_failed;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (MH_CreateHook(base + hooks[i].rva, hooks[i].detour, hooks[i].original) != MH_OK) {
            MH_Uninitialize();
            return InitCode::hook_create_failed;
        }
    }
    return InitCode::ok;
}

/**
 * @brief Country-DLL variant: create all, then enable all at once.
 * @param base Image base.
 * @param hooks Hook list.
 * @param count Number of hooks.
 * @return `ok`, 30, 31 or `hook_enable_failed` (32; all hooks disabled, trampolines kept).
 */
inline InitCode install_all_at_once(unsigned char* base, const HookSpec* hooks, std::size_t count) noexcept {
    if (const auto created = create_hooks(base, hooks, count); created != InitCode::ok) {
        return created;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_DisableHook(MH_ALL_HOOKS);
        return InitCode::hook_enable_failed;
    }
    return InitCode::ok;
}

/**
 * @brief Editor-DLL variant: create all, then enable one by one in `enable_order`.
 * @param base Image base.
 * @param hooks Hook list (creation order).
 * @param count Number of hooks.
 * @param enable_order Indices into `hooks`, in activation order (Tick last).
 * @param enable_count Length of `enable_order`; must equal `count`.
 * @return `ok`, 30, 31 or 32 (every hook disabled best-effort, trampolines kept).
 */
inline InitCode install_in_order(unsigned char* base, const HookSpec* hooks, std::size_t count,
                                 const std::size_t* enable_order, std::size_t enable_count) noexcept {
    if (const auto created = create_hooks(base, hooks, count); created != InitCode::ok) {
        return created;
    }
    bool enabled = enable_count == count;
    for (std::size_t i = 0; enabled && i < enable_count; ++i) {
        enabled = enable_order[i] < count && MH_EnableHook(base + hooks[enable_order[i]].rva) == MH_OK;
    }
    if (!enabled) {
        for (std::size_t i = 0; i < count; ++i) {
            MH_DisableHook(base + hooks[i].rva);
        }
        return InitCode::hook_enable_failed;
    }
    return InitCode::ok;
}

}  // namespace capie::core
