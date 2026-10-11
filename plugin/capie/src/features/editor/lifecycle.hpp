/**
 * @file lifecycle.hpp
 * @brief Per-model editor lifecycle: one opening, one prefill, readiness for Apply.
 *
 * Each editor model has a `Lifecycle`: whether its window is visible, whether this
 * opening already attempted (and completed) its one-shot prefill, the opening epoch read
 * from the script and a generation counter that changes whenever an opening ends or the
 * window is rebound. Apply is accepted only while the lifecycle is visible, attempted,
 * prefilled and still in the same epoch and generation that the Apply read at its start.
 *
 * Transitions (all under `state_lock`, which is never held across a game call):
 *   - visible -> hidden (panel closed): new generation, prefill re-armed, draft dropped;
 *     the next show prefills again (RN3, PID3044);
 *   - invalid context/epoch: new generation, prefill NOT re-armed, so a draft is never
 *     overwritten in the same epoch; a new Open or Bind is required;
 *   - Bind: full reset to "not seen";
 *   - new epoch while visible: new generation and a fresh prefill.
 *
 * `Pending` holds a copied Enter observation (debug flavor only; never used to Apply).
 * Persistent state holds scalars and copied text only, never engine pointers.
 */
#pragma once
#include "diagnostics.hpp"
#include "schema.hpp"

#include <cstdint>

namespace capie::editor {

/// @brief Copied Enter observation awaiting the next Tick (diagnostic only).
struct Pending {
    bool valid{};               ///< Slot holds an observation.
    std::uint64_t epoch{};      ///< Epoch at observation.
    std::uint64_t generation{}; ///< Generation at observation.
    ULONGLONG created{};        ///< `GetTickCount64()` at observation.
    DWORD thread{};             ///< Observing thread.
    InputSnapshot text{};       ///< Copied text.
};

/// @brief Lifecycle of one editor model.
struct Lifecycle {
    bool visible{};              ///< Window seen visible in the current generation.
    bool attempted{};            ///< One-shot prefill attempted in this opening.
    bool prefilled{};            ///< Prefill completed and verified in this opening.
    std::uint64_t epoch{};       ///< Opening epoch read from the script.
    std::uint64_t generation{};  ///< Changes at every end of an opening or rebind.
    unsigned last_status{};      ///< Last status code logged by `status_once`.
};

/// @brief Status codes logged once per change by `status_once`.
enum class Status : unsigned {
    none = 0,             ///< Nothing logged yet.
    hidden = 1,           ///< Own root hidden.
    guard_rejected = 2,   ///< Own root failed the context/root guard.
    epoch_rejected = 3,   ///< Epoch carrier unreadable.
    visible_valid = 4,    ///< Visible with a valid epoch.
    bind_rejected = 5,    ///< Bind saw an invalid own root.
};

/// @brief Lifecycle per model.
inline Lifecycle lifecycle[model_count]{};
/// @brief Enter observation per field (debug flavor).
inline Pending pending[field_count]{};
/// @brief Guards `lifecycle` and `pending`. Never held across a game call.
inline SRWLOCK state_lock = SRWLOCK_INIT;

/**
 * @brief Clears the Enter observations of a model.
 * @param model Model index.
 * @pre `state_lock` held exclusively.
 */
inline void clear_pending(std::size_t model) noexcept {
    for (std::size_t i = models[model].first; i < models[model].first + models[model].count; ++i) {
        pending[i] = {};
    }
}

/**
 * @brief Ends the current opening because the window is hidden or invalid.
 * @param model Model index.
 * @param hidden `true` for a valid hidden window (re-prefill on the next show); `false`
 *        for an invalid context or epoch (no re-prefill in the same epoch).
 */
inline void reset_model(std::size_t model, bool hidden) noexcept {
    AcquireSRWLockExclusive(&state_lock);
    auto& state = lifecycle[model];
    if (hidden) {
        // Panel close replaces the editor widget on reopen; re-prefill on next show so Apply
        // is not silently dropped (RN3 PID3044). The draft is intentionally dropped.
        if (state.visible) {
            ++state.generation;
            state.attempted = false;
            state.prefilled = false;
        }
        state.visible = false;
    } else {
        // An invalid root/epoch cannot authorize input. Same-epoch recovery must not silently
        // retry SetText over a draft: require a new Open or bind.
        state.visible = false;
        state.attempted = true;
        state.prefilled = false;
        ++state.generation;
    }
    clear_pending(model);
    ReleaseSRWLockExclusive(&state_lock);
}

/**
 * @brief Full reset after a Bind of an own window.
 * @param model Model index.
 * @return The new generation.
 */
inline std::uint64_t reset_bound_model(std::size_t model) noexcept {
    AcquireSRWLockExclusive(&state_lock);
    auto& state = lifecycle[model];
    state.visible = false;
    state.attempted = false;
    state.prefilled = false;
    state.epoch = 0;
    ++state.generation;
    state.last_status = 0;
    clear_pending(model);
    const auto generation = state.generation;
    ReleaseSRWLockExclusive(&state_lock);
    return generation;
}

/**
 * @brief Whether a model is ready for Apply in `epoch` (and optionally `generation`).
 * @param model Model index.
 * @param epoch Epoch read at the start of the Apply.
 * @param generation In: expected generation (when `compare_generation`); out: current generation otherwise.
 * @param compare_generation Require the generation to be unchanged.
 * @return `true` when visible, attempted, prefilled and in the same epoch (and generation).
 */
inline bool ready_model(std::size_t model, std::uint64_t epoch, std::uint64_t& generation,
                        bool compare_generation) noexcept {
    if (model >= model_count) {
        return false;
    }
    AcquireSRWLockShared(&state_lock);
    const auto& state = lifecycle[model];
    const bool ready = state.visible && state.attempted && state.prefilled && state.epoch == epoch &&
                       (!compare_generation || state.generation == generation);
    if (ready && !compare_generation) {
        generation = state.generation;
    }
    ReleaseSRWLockShared(&state_lock);
    return ready;
}

/**
 * @brief Logs a status event once per change of status for a model.
 * @param model Model index.
 * @param status Status code.
 * @param event Event name.
 */
inline void status_once(std::size_t model, Status status, const char* event) noexcept {
    bool changed = false;
    const auto code = static_cast<unsigned>(status);
    AcquireSRWLockExclusive(&state_lock);
    if (lifecycle[model].last_status != code) {
        lifecycle[model].last_status = code;
        changed = true;
    }
    ReleaseSRWLockExclusive(&state_lock);
    if (changed) {
        diagnostic(next_observation_id(), event, models[model].first, 0, 0);
    }
}

}  // namespace capie::editor
