/**
 * @file prefill.hpp
 * @brief One-shot prefill of an opened editor with the current game text (post-Tick observer).
 *
 * When one of our editor windows becomes visible with a new epoch, the bridge writes the
 * current name (taken from the hidden prefill carrier the script fills) into the edit
 * box once, so the player edits the real name instead of an empty field. Order per
 * opening:
 *   1. look up every field's edit box and carrier; all must be ours, visible, NOT focused
 *      (a focused draft is never overwritten) and decodable; re-read the epoch;
 *   2. per field: SetText, read the text back, re-inspect the window and epoch, check the
 *      lifecycle is still the same opening, look the edit up again (same widget), run the
 *      native edit refresh, re-inspect and read back once more;
 *   3. commit `prefilled` only if the opening is still the same; then, and only then,
 *      seed the caption baseline (the only seed source).
 * Any failure logs the step and stops; the attempt is not repeated in the same opening
 * (a hide/show re-arms it, see `lifecycle.hpp`).
 *
 * Game calls made here: FindEdit, FindInstantText, SetText, edit Refresh, all SEH-guarded,
 * none under a lock. Pointers are stack-local to one Tick.
 */
#pragma once
#include "../caption/caption_adapter.hpp"
#include "diagnostics.hpp"
#include "lifecycle.hpp"
#include "window_view.hpp"

namespace capie::editor {

using engine::edit_named;
using engine::find_edit;
using engine::guarded_find;
using engine::guarded_refresh;
using engine::guarded_set;
using engine::read_input_active;
using engine::same_text;
using engine::snapshot_edit;
using engine::visible_edit;

/**
 * @brief Whether the lifecycle still describes the same opening.
 * @param model Model index.
 * @param epoch Opening epoch.
 * @param generation Opening generation.
 * @return `true` when visible, attempted and unchanged.
 */
inline bool same_opening(std::size_t model, std::uint64_t epoch, std::uint64_t generation) noexcept {
    core::platform::lock_shared(&state_lock);
    const auto& current = lifecycle[model];
    const bool same =
        current.visible && current.attempted && current.epoch == epoch && current.generation == generation;
    core::platform::unlock_shared(&state_lock);
    return same;
}

/**
 * @brief Re-inspects the window: still visible, same model and root, same epoch.
 * @param window Window.
 * @param view Original inspection.
 * @param epoch Opening epoch.
 * @param fresh Receives the fresh inspection.
 * @return `true` when unchanged.
 */
inline bool same_view(void* window, const View& view, std::uint64_t epoch, View& fresh) noexcept {
    std::uint64_t fresh_epoch = 0;
    return inspect_window(window, fresh) == ViewResult::visible && fresh.model == view.model &&
           fresh.root == view.root && read_epoch(fresh.root, view.model, fresh_epoch) && fresh_epoch == epoch;
}

/**
 * @brief Runs the one-shot prefill of an opening (see file comment).
 * @param window Visible own editor window.
 * @param view Its inspection.
 * @param epoch Opening epoch.
 * @param generation Opening generation.
 * @param id Observation id for the log.
 */
inline void prefill_open(void* window, const View& view, std::uint64_t epoch, std::uint64_t generation,
                         unsigned long id) noexcept {
    // Prepare all fields before any SetText. A failed/partial attempt is not retried.
    void* edits[max_fields_per_model]{};
    Utf8Snapshot text[max_fields_per_model]{};
    const auto& model = models[view.model];
    for (std::size_t j = 0; j < model.count; ++j) {
        const auto field = model.first + j;
        edits[j] = guarded_find(find_edit, view.root, fields[field].edit);
        bool focused = false;
        if (!edit_named(edits[j], fields[field].edit)) {
            diagnostic(id, "prefill-edit-lookup-type-rejected", field, epoch, generation);
            return;
        }
        if (!visible_edit(edits[j])) {
            diagnostic(id, "prefill-edit-visibility-rejected", field, epoch, generation);
            return;
        }
        if (!read_input_active(edits[j], focused)) {
            diagnostic(id, "prefill-focus-state-unreadable", field, epoch, generation);
            return;
        }
        if (focused) {
            diagnostic(id, "prefill-focused-draft-preserved", field, epoch, generation);
            return;
        }
        if (!carrier(view.root, fields[field].carrier, text[j], false)) {
            diagnostic(id, "prefill-carrier-lookup-string-rejected", field, epoch, generation);
            return;
        }
        if (!engine::decode_utf8(text[j])) {
            diagnostic(id, "prefill-carrier-unicode-render-rejected", field, epoch, generation);
            return;
        }
    }
    std::uint64_t fresh_epoch = 0;
    if (!read_epoch(view.root, view.model, fresh_epoch) || fresh_epoch != epoch) {
        diagnostic(id, "prefill-epoch-changed", model.first, epoch, generation);
        return;
    }
    for (std::size_t j = 0; j < model.count; ++j) {
        const auto field = model.first + j;
        const auto native = engine::borrowed_string(text[j].bytes, text[j].size);
        InputSnapshot check{};
        bool focused = false;
        if (!edit_named(edits[j], fields[field].edit) || !visible_edit(edits[j]) ||
            !read_input_active(edits[j], focused) || focused || !guarded_set(edits[j], &native) ||
            !snapshot_edit(edits[j], fields[field].edit, check) || !same_text(check, text[j].decoded)) {
            diagnostic(id, "prefill-write-readback-failed-reopen-required", field, epoch, generation);
            return;
        }
        // Native state-name/text-input paths refresh the CEditBox after TB SetText.
        // Resolve it again after the setter/observers; never write dirty flags.
        View fresh_view{};
        if (!same_view(window, view, epoch, fresh_view)) {
            diagnostic(id, "prefill-render-view-changed-reopen-required", field, epoch, generation);
            return;
        }
        bool opening = same_opening(view.model, epoch, generation);
        void* fresh_edit = guarded_find(find_edit, fresh_view.root, fields[field].edit);
        if (!opening || fresh_edit != edits[j] || !edit_named(fresh_edit, fields[field].edit) ||
            !visible_edit(fresh_edit) || !read_input_active(fresh_edit, focused) || focused ||
            !snapshot_edit(fresh_edit, fields[field].edit, check) || !same_text(check, text[j].decoded) ||
            !guarded_refresh(fresh_edit)) {
            diagnostic(id, "prefill-render-refresh-failed-reopen-required", field, epoch, generation);
            return;
        }
        // The native refresh may notify observers; re-resolve before accepting it.
        View rendered_view{};
        if (!same_view(window, view, epoch, rendered_view)) {
            diagnostic(id, "prefill-render-post-view-changed-reopen-required", field, epoch, generation);
            return;
        }
        opening = same_opening(view.model, epoch, generation);
        void* rendered_edit = guarded_find(find_edit, rendered_view.root, fields[field].edit);
        if (!opening || rendered_edit != fresh_edit || !edit_named(rendered_edit, fields[field].edit) ||
            !visible_edit(rendered_edit) || !read_input_active(rendered_edit, focused) || focused ||
            !snapshot_edit(rendered_edit, fields[field].edit, check) || !same_text(check, text[j].decoded)) {
            diagnostic(id, "prefill-render-post-readback-failed-reopen-required", field, epoch, generation);
            return;
        }
        // A returned refresh is not visual acceptance; QA must inspect rendered text.
        diagnostic(id, "prefill-render-refresh-returned", field, epoch, generation);
        diagnostic(id, "prefill-readback-pass", field, epoch, generation, &check);
    }
    core::platform::lock(&state_lock);
    auto& state = lifecycle[view.model];
    const bool prefilled = state.visible && state.epoch == epoch && state.generation == generation;
    if (prefilled) {
        state.prefilled = true;
    }
    core::platform::unlock(&state_lock);
    // Caption seed: only after this opening's one-shot prefill fully succeeded (no lock held).
    if (prefilled) {
        caption::caption_prefill(window, view.model);
    }
}

/**
 * @brief Matches copied Enter observations to the current opening (debug flavor; never Applies).
 * @param view Inspection.
 * @param epoch Epoch.
 * @param generation Generation.
 * @param id Observation id.
 */
inline void consume_diagnostic(const View& view, std::uint64_t epoch, std::uint64_t generation,
                               unsigned long id) noexcept {
    for (std::size_t i = models[view.model].first; i < models[view.model].first + models[view.model].count; ++i) {
        Pending request{};
        core::platform::lock(&state_lock);
        request = pending[i];
        pending[i] = {};
        core::platform::unlock(&state_lock);
        if (!request.valid) {
            continue;
        }
        void* edit = guarded_find(find_edit, view.root, fields[i].edit);
        InputSnapshot current{};
        const auto age = core::platform::monotonic_ms() - request.created;
        const bool valid = request.epoch == epoch && request.generation == generation && age <= enter_max_age_ms &&
                           edit_named(edit, fields[i].edit) && engine::input_active(edit) &&
                           snapshot_edit(edit, fields[i].edit, current) && same_text(request.text, current);
        diagnostic(id, valid ? "deferred-enter-validated-diagnostic" : "deferred-enter-rejected", i, epoch, generation,
                   &request.text, request.thread);
        // Enter remains diagnostic only. It never calls dispatch or injects command variables.
    }
}

/**
 * @brief Records a visible window with a valid epoch (shared by own and grammar editors).
 * @param state Lifecycle.
 * @param epoch Epoch read from the carrier.
 * @param generation Receives the opening's generation.
 * @param attempt Set to `true` when this call claims the opening's one-shot prefill.
 * @return `true` when the epoch changed (a new opening started).
 * @pre `state_lock` held exclusively.
 */
inline bool see_visible(Lifecycle& state, std::uint64_t epoch, std::uint64_t& generation, bool& attempt) noexcept {
    const bool fresh = state.epoch != epoch;
    if (fresh) {
        state.attempted = false;
        state.prefilled = false;
        state.epoch = epoch;
        ++state.generation;
    }
    state.visible = true;
    generation = state.generation;
    if (!state.attempted) {
        state.attempted = true;
        attempt = true;
    }
    return fresh;
}

/**
 * @brief Post-original Tick observer of the editor lifecycle.
 * @param window Ticking window.
 */
inline void observe_tick(void* window) noexcept {
    View view{};
    const auto result = inspect_window(window, view);
    if (result == ViewResult::unrelated) {
        return;
    }
    if (result != ViewResult::visible) {
        status_once(view.model, result == ViewResult::hidden ? Status::hidden : Status::guard_rejected,
                    result == ViewResult::hidden ? "own-root-hidden" : "own-root-guard-rejected");
        reset_model(view.model, result == ViewResult::hidden);
        return;
    }
    std::uint64_t epoch = 0;
    if (!read_epoch(view.root, view.model, epoch)) {
        status_once(view.model, Status::epoch_rejected, "epoch-carrier-rejected");
        reset_model(view.model, false);
        return;
    }
    status_once(view.model, Status::visible_valid, "own-root-visible-epoch-valid");
    bool attempt = false;
    std::uint64_t generation = 0;
    core::platform::lock(&state_lock);
    if (see_visible(lifecycle[view.model], epoch, generation, attempt)) {
        clear_pending(view.model);
    }
    core::platform::unlock(&state_lock);
    const auto id = next_observation_id();
    if (attempt) {
        prefill_open(window, view, epoch, generation, id);
    }
    if constexpr (debug_diagnostics) {
        consume_diagnostic(view, epoch, generation, id);  // Enter observations exist only in the debug flavor
    }
}

}  // namespace capie::editor
