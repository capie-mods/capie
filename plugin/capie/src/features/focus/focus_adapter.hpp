/**
 * @file focus_adapter.hpp
 * @brief Connects the focus release to the editor bridge's Tick and Bind hooks.
 *
 * Supplies bounded memory, the fresh FindEdit lookup, the SEH-guarded
 * `225E320(manager, nullptr)` call and the editor log. `focus_armed` is set at the end of
 * a successful initialization, after the focus fingerprints, both CEditBox slot pins and
 * the manager RIP target were verified; until then `on_tick` returns immediately.
 */
#pragma once
#include "../../core/log.hpp"
#include "../../engine/game_calls.hpp"
#include "focus_release.hpp"

#include <atomic>

namespace capie::focus {

/// @brief Set after every focus pin verified.
inline std::atomic<bool> focus_armed{false};

/// @brief Environment adapter required by `Release`.
struct FocusEnv {
    core::ProcessMemory memory{};  ///< Memory policy.
    /// @brief Memory policy. @return Reference.
    [[nodiscard]] const core::ProcessMemory& mem() const noexcept { return memory; }
    /// @brief Image base. @return Base.
    [[nodiscard]] static uptr image() noexcept { return engine::image_base; }
    /**
     * @brief Fresh FindEdit under SEH.
     * @param root Root widget.
     * @param name Edit name.
     * @return Widget address or 0.
     */
    static uptr find_edit(uptr root, const char* name) noexcept {
        return reinterpret_cast<uptr>(engine::guarded_find(engine::find_edit, reinterpret_cast<void*>(root), name));
    }
    /**
     * @brief SEH-guarded `225E320(manager, nullptr)`.
     * @param manager Focus manager.
     * @return `false` on fault.
     */
    static bool call_focus_null(uptr manager) noexcept {
        return engine::focus_call_null(reinterpret_cast<void*>(manager));
    }
    /// @brief Operational switch. @return `focus_armed`.
    [[nodiscard]] static bool armed() noexcept { return focus_armed.load(std::memory_order_acquire); }
    /**
     * @brief Writes a focus/hide line (lifecycle channel).
     * @param line Text.
     */
    static void log(const char* line) noexcept { core::logging::write_log(line); }
};

/// @brief The adapter instance.
inline FocusEnv focus_env{};
/// @brief The focus release instance.
inline Release<FocusEnv> focus_release(focus_env);

/**
 * @brief Post-original Tick integration.
 * @param window Window.
 */
inline void focus_tick(void* window) noexcept {
    focus_release.on_tick(reinterpret_cast<uptr>(window));
}
/**
 * @brief Post-original Bind integration.
 * @param window Window.
 */
inline void focus_bind(void* window) noexcept {
    focus_release.on_bind(reinterpret_cast<uptr>(window));
}

}  // namespace capie::focus
