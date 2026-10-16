/** @file release_pipeline.hpp
 * @brief Shared release Bind reset and Dispatch orchestration; platform hooks provide ownership/admission.
 * No detour installation or startup is performed here. Windows and macOS use the same
 * existing lifecycle, caption, public-editor and transport decisions.
 */
#pragma once
#include "../focus/focus_adapter.hpp"
#include "public_editor.hpp"
namespace capie::editor {
/** @brief Performs release reset observers after the native Bind has returned.
 * @param window Freshly bound window, inspected again before state reset.
 */
inline void after_bind_release(void* window) noexcept {
    // Native construction/rebind and recursive widget binding finish first. Forward once;
    // do not access an old root or cache a window/widget pointer.
    if (!active.load(std::memory_order_acquire)) {
        return;
    }
    caption::caption_bind(window);
    focus::focus_bind(window);
    public_bind(window);
    View view{};
    const auto result = inspect_window(window, view);
    if (result == ViewResult::unrelated) {
        return;
    }
    if (result == ViewResult::invalid) {
        reset_model(view.model, false);
        status_once(view.model, Status::bind_rejected, "bind-own-root-guard-rejected");
        return;
    }
    // inspect_window verified exact own model + fresh context/window/root/lists. The newly bound
    // root is normally hidden; its next visible tick prefills.
    const auto generation = reset_bound_model(view.model);
    diagnostic(next_observation_id(), "own-model-bound-readiness-reset", models[view.model].first, 0, generation);
}
/** @brief Applies the shared release gates and dispatches once or emits the existing sentinel.
 * @param context Current GUI context. @param opaque_selector Native selector object.
 * @note Caller owns native-call admission and window lifetime; this does not broaden either.
 */
inline void dispatch_release(void* context, void* opaque_selector) noexcept {
    const auto* selector = static_cast<const RecordString*>(opaque_selector);
    const RecordString* model_name = nullptr;
    std::size_t model = model_count;
    if (!active.load(std::memory_order_acquire)) {
        original_dispatch(context, opaque_selector);
        return;
    }
    if (!own_commit(context, selector, model_name, model)) {
        // Grammar editors of other mods (public contract); anything else is forwarded unchanged.
        if (!public_dispatch(context, opaque_selector)) {
            original_dispatch(context, opaque_selector);
        }
        return;
    }
    const unsigned caption_code = caption::caption_dispatch_verdict(context, model);
    const bool caption_ok = caption_code == 0;
    if (!caption_ok) {
        bridge_rejected(next_observation_id(), "caption-seed-gate-apply-rejected");
    }
    Origin current{};
    current.context = context;
    current.model_name = model_name;
    current.selector = selector;
    current.model = model;
    current.id = next_observation_id();
    InputSnapshot snapshots[max_fields_per_model]{};
    bool stale = !caption_ok;
    if (!stale && original_ctor != nullptr && !capture_fields(current, snapshots)) {
        stale = true;
    }
    if (stale) {
        // RN6: never drop an own Apply silently while its own editor window is live.
        if (stale_eligible(context, model) &&
            dispatch_stale(current, opaque_selector,
                           caption_ok ? static_cast<unsigned>(StaleCode::stale) : caption_code)) {
            return;
        }
        bridge_rejected(current.id, "own-action-dropped-before-dispatch");
        return;
    }
    if (original_ctor == nullptr || !prepare_transport(current, snapshots) || !still_current(current, snapshots)) {
        bridge_rejected(current.id, "own-action-dropped-before-dispatch");
        return;
    }
    {
        const OriginFrame frame(&current);
        original_dispatch(context, opaque_selector);
    }
    if (current.ctor_calls != 1) {
        bridge_rejected(current.id, "unexpected-ctor-count-no-retry");
    }
}
}  // namespace capie::editor
