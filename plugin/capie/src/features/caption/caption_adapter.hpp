/**
 * @file caption_adapter.hpp
 * @brief Connects the caption refresh core to the editor bridge (same hooks, no extra detour).
 *
 * The core (`caption_refresh.hpp`) is engine-agnostic; this adapter gives it bounded
 * process memory, the carrier lookup, the SEH-guarded `RequestViewRefresh` call and the editor log,
 * and exposes the four integration points the bridge calls:
 *   - `caption_tick(window)`     after the original Tick (observation + at most one request);
 *   - `caption_bind(window)`     after the original Bind (baseline reset);
 *   - `caption_prefill(window, model)` after a successful one-shot prefill (only seed source);
 *   - `caption_dispatch_verdict(context, model)` before an own leader/party Apply.
 * Everything is inert until `caption_armed` is set at the end of a successful
 * initialization. Only leader (model 0) and party (model 1) are gated; country is not.
 */
#pragma once
#include "../../core/log.hpp"
#include "../../engine/game_calls.hpp"
#include "../../engine/widgets.hpp"
#include "../editor/schema.hpp"
#include "caption_refresh.hpp"

#include <atomic>
#include <cstring>

namespace capie::caption {

/// @brief Set after every caption pin verified; read with acquire ordering.
inline std::atomic<bool> caption_armed{false};

/// @brief Bounded process memory for the core (alias kept for tests).
using CaptionMemory = core::ProcessMemory;

/**
 * @brief Reads a carrier's raw text (no decoding) for the core.
 * @param root Root widget.
 * @param name Carrier name.
 * @param text Receives the bytes.
 * @return `true` on success.
 */
inline bool caption_carrier_text(void* root, const char* name, engine::Utf8Snapshot& text) noexcept {
    return engine::carrier(root, name, text, false);
}

/// @brief Environment adapter required by `Service`.
struct CaptionEnv {
    CaptionMemory memory{};  ///< Memory policy.
    /// @brief Memory policy. @return Reference.
    [[nodiscard]] const CaptionMemory& mem() const noexcept { return memory; }
    /// @brief Image base. @return Base.
    [[nodiscard]] static uptr image() noexcept { return engine::image_base; }
    /**
     * @brief Fresh carrier lookup.
     * @param root Root widget.
     * @param name Carrier name.
     * @param out Destination.
     * @param cap Destination capacity.
     * @param size Receives the length.
     * @return `false` when missing, unreadable or longer than `cap`.
     */
    static bool carrier(uptr root, const char* name, char* out, std::size_t cap, std::size_t& size) noexcept {
        engine::Utf8Snapshot text{};
        if (!caption_carrier_text(reinterpret_cast<void*>(root), name, text) || text.size > cap) {
            return false;
        }
        std::memcpy(out, text.bytes, text.size);
        size = text.size;
        return true;
    }
    /**
     * @brief SEH-guarded `RequestViewRefresh(gui, index)`.
     * @param gui GUI container.
     * @param index View index.
     * @return `false` on fault.
     */
    static bool call_refresh(uptr gui, int index) noexcept {
        return engine::caption_call(reinterpret_cast<void*>(gui), index);
    }
    /// @brief Operational switch. @return `caption_armed`.
    [[nodiscard]] static bool refresh_armed() noexcept { return caption_armed.load(std::memory_order_acquire); }
    /**
     * @brief Writes a caption line (lifecycle channel).
     * @param line Text.
     */
    static void log(const char* line) noexcept { core::logging::write_log(line); }
};

static_assert(static_cast<unsigned>(Service<CaptionEnv>::Verdict::allowed) == 0 &&
                  static_cast<unsigned>(Service<CaptionEnv>::Verdict::stale) ==
                      static_cast<unsigned>(editor::StaleCode::stale) &&
                  static_cast<unsigned>(Service<CaptionEnv>::Verdict::no_counter) ==
                      static_cast<unsigned>(editor::StaleCode::no_counter) &&
                  static_cast<unsigned>(Service<CaptionEnv>::Verdict::cap_reached) ==
                      static_cast<unsigned>(editor::StaleCode::counter_limit),
              "a caption verdict is forwarded as the sentinel epoch -code: the numbers must match StaleCode");

/// @brief The adapter instance.
inline CaptionEnv caption_env{};
/// @brief The caption service instance.
inline Service<CaptionEnv> caption_service(caption_env);

/**
 * @brief Seeds the baseline after a successful prefill (leader/party only).
 * @param window Editor window.
 * @param model Editor model index.
 */
inline void caption_prefill(void* window, std::size_t model) noexcept {
    if (!caption_armed.load(std::memory_order_acquire) || model > editor::last_caption_model) {
        return;
    }
    caption_service.on_prefill(reinterpret_cast<uptr>(window));
}

/**
 * @brief Apply gate for an own Apply.
 * @param context Window context of the click.
 * @param model Editor model index.
 * @return 0 = allowed (also when disarmed or country); otherwise the sentinel code:
 *         1 stale (Cancel+reopen fixes it), 2 revision counter missing/unreadable,
 *         3 counter at the 999 limit (reopen does not fix 2 or 3).
 */
inline unsigned caption_dispatch_verdict(void* context, std::size_t model) noexcept {
    if (!caption_armed.load(std::memory_order_acquire) || model > editor::last_caption_model) {
        return 0;  // disarmed/country: unchanged
    }
    void* window = nullptr;
    if (!core::member(context, core::off::context_window, window)) {
        return 1;  // fail-closed
    }
    return static_cast<unsigned>(caption_service.on_dispatch_verdict(reinterpret_cast<uptr>(window)));
}

/**
 * @brief Boolean form of the gate.
 * @param context Window context.
 * @param model Model index.
 * @return `true` when allowed.
 */
inline bool caption_dispatch_allowed(void* context, std::size_t model) noexcept {
    return caption_dispatch_verdict(context, model) == 0;
}

/**
 * @brief Post-original Tick integration.
 * @param window Window.
 */
inline void caption_tick(void* window) noexcept {
    if (caption_armed.load(std::memory_order_acquire)) {
        caption_service.on_tick(reinterpret_cast<uptr>(window));
    }
}

/**
 * @brief Post-original Bind integration.
 * @param window Window.
 */
inline void caption_bind(void* window) noexcept {
    if (caption_armed.load(std::memory_order_acquire)) {
        caption_service.on_bind(reinterpret_cast<uptr>(window));
    }
}

}  // namespace capie::caption
