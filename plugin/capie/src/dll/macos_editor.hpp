/** @file macos_editor.hpp
 * @brief Explicit native release composition; no patching, startup constructor or automatic activation.
 * Configuration must be supplied by a verified bootstrap while hooks are quiescent.
 * Admission is lexical and restricted to the verified UI thread; it is not SEH or
 * a proof that readable objects remain alive. See docs/macos/editor-composition.md.
 */
#pragma once
#if !defined(__APPLE__)
#error "macos_editor.hpp requires the native macOS profile"
#endif
#include "../features/editor/release_pipeline.hpp"

#include <atomic>
namespace capie::editor::macos {
/// @brief Already verified originals/callees. Configure only before hooks can run.
struct Configuration {
    core::uptr image{};                              ///< Verified loaded image base.
    unsigned long ui_thread{};                       ///< Independently established owning UI thread.
    Tick tick{};                                     ///< Original native Tick trampoline.
    Bind bind{};                                     ///< Original native Bind trampoline.
    Dispatch dispatch{};                             ///< Original native Dispatch trampoline.
    Constructor constructor{};                       ///< Void five-argument native constructor trampoline.
    engine::FindWidget find_edit{}, find_instant{};  ///< Verified three-argument lookup functions.
    engine::SetText set_text{};                      ///< Verified text setter.
    engine::RefreshEdit refresh_edit{};              ///< Verified edit refresh.
    engine::CaptionRequest caption_request{};        ///< Verified caption request.
    engine::FocusSwitch focus_switch{};              ///< Verified focus release.
    CountryCapabilitiesQuery capabilities{};         ///< Verified core handshake, optional.
    bool caption_verified{};                         ///< All caption-specific pins passed.
    bool focus_verified{};                           ///< All focus-specific pins passed.
};
namespace detail {
inline Configuration config{};                    ///< Immutable while hook calls can run.
inline bool configured = false;                   ///< Bootstrap state; configuration requires quiescence.
inline std::atomic<bool> enabled{false};          ///< Explicit composition switch.
inline thread_local bool observing_tick = false;  ///< Prevent recursive Tick observer passes.

/// @brief Temporarily removes this thread's admission around a forwarded original.
class SuspendAdmission {
    unsigned long saved_{};

public:
    SuspendAdmission() noexcept {
        auto current = core::platform::thread_id();
        if (engine::native_call_thread.compare_exchange_strong(current, 0, std::memory_order_acq_rel))
            saved_ = current;
    }
    ~SuspendAdmission() {
        if (saved_) {
            unsigned long expected = 0;
            engine::native_call_thread.compare_exchange_strong(expected, saved_, std::memory_order_acq_rel);
        }
    }
    SuspendAdmission(const SuspendAdmission&) = delete;
    SuspendAdmission& operator=(const SuspendAdmission&) = delete;
};
/// @brief Grants a lexical scope to the owning thread; nested scopes do not revoke the outer grant.
class Admission {
    bool entered_{}, owned_{};
    unsigned long owner_{};

public:
    explicit Admission(bool allowed) noexcept {
        if (!allowed)
            return;
        owner_ = core::platform::thread_id();
        if (owner_ != config.ui_thread)
            return;
        unsigned long expected = 0;
        owned_ = engine::native_call_thread.compare_exchange_strong(expected, owner_, std::memory_order_acq_rel);
        entered_ = owned_ || expected == owner_;
    }
    ~Admission() {
        if (owned_) {
            auto expected = owner_;
            engine::native_call_thread.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        }
    }
    [[nodiscard]] bool entered() const noexcept { return entered_; }
    Admission(const Admission&) = delete;
    Admission& operator=(const Admission&) = delete;
};
/// @brief Calls the original Dispatch once, with plugin-initiated native calls suspended.
inline void forward_dispatch(void* context, void* selector) {
    const SuspendAdmission suspended;
    config.dispatch(context, selector);
}
/// @brief Native constructor has no return-value contract; preserve all five arguments.
inline void forward_constructor(void* self, const RecordString* gui, const RecordString* selector, const void* scope,
                                const TempVector* records) {
    const SuspendAdmission suspended;
    config.constructor(self, gui, selector, scope, records);
}
/// @brief Requires a fresh recognized visible GUI chain before Dispatch lookup admission.
inline bool dispatch_context(void* context) noexcept {
    void* window = nullptr;
    void* model = nullptr;
    if (!core::member(context, core::off::context_window, window) || !window_may_be_own(window) ||
        !core::member(window, core::off::window_model, model))
        return false;
    core::uptr expected = 0;
    if (!core::add_offset(reinterpret_cast<core::uptr>(window), core::off::window_context, expected) ||
        expected != reinterpret_cast<core::uptr>(context))
        return false;
    View view{};
    return inspect_chain(window, model, view) == ViewResult::visible;
}
}  // namespace detail
/** @brief Installs verified callbacks while inactive and quiescent; never enables native calls.
 * @param config Verified callback bundle. @return False for missing required bindings or active composition.
 */
inline bool configure(const Configuration& config) noexcept {
    if (detail::enabled.load() || !config.image || !config.ui_thread || !config.tick || !config.bind ||
        !config.dispatch || !config.constructor || !config.find_edit || !config.find_instant || !config.set_text ||
        !config.refresh_edit || !config.caption_request || !config.focus_switch ||
        engine::native_call_thread.load() != 0)
        return false;
    detail::config = config;
    engine::image_base = config.image;
    original_tick = config.tick;
    original_bind = config.bind;
    original_dispatch = detail::forward_dispatch;
    original_ctor = detail::forward_constructor;
    engine::find_edit = config.find_edit;
    engine::find_instant = config.find_instant;
    engine::set_text = config.set_text;
    engine::refresh_edit = config.refresh_edit;
    engine::caption_request = config.caption_request;
    engine::focus_switch = config.focus_switch;
    verified_country_capabilities.store(config.capabilities, std::memory_order_release);
    active.store(false);
    caption::caption_armed.store(false);
    focus::focus_armed.store(false);
    detail::configured = true;
    return true;
}
/** @brief Explicit activation after external UI lifetime/fault-policy verification.
 * @param lifetime_verified Bootstrap attestation, not a substitute for actual verification.
 * @return False until configured and the policy is explicitly verified.
 */
inline bool activate(bool lifetime_verified) noexcept {
    if (!lifetime_verified || !detail::configured || engine::native_call_thread.load() != 0)
        return false;
    caption::caption_armed.store(detail::config.caption_verified);
    focus::focus_armed.store(detail::config.focus_verified);
    active.store(true, std::memory_order_release);
    detail::enabled.store(true, std::memory_order_release);
    return true;
}
/// @brief Stops observer work; configuration remains for original-only forwarding. Call while quiescent.
inline void deactivate() noexcept {
    detail::enabled.store(false, std::memory_order_release);
    active.store(false, std::memory_order_release);
    caption::caption_armed.store(false);
    focus::focus_armed.store(false);
}
/** @brief Native Tick: original exactly once, then shared release observers on the owning UI thread.
 * @param window Current scripted window. @param force Original force flag.
 */
inline void tick(void* window, char force) noexcept {
    if (!detail::configured)
        return;
    {
        const detail::SuspendAdmission suspended;
        detail::config.tick(window, force);
    }
    if (!detail::enabled.load(std::memory_order_acquire) || core::platform::thread_id() != detail::config.ui_thread ||
        detail::observing_tick || !window_may_be_own(window))
        return;
    const detail::Admission admission(true);
    if (!admission.entered())
        return;
    struct TickScope {
        TickScope() { detail::observing_tick = true; }
        ~TickScope() { detail::observing_tick = false; }
    } scope;
    observe_tick(window);
    public_observe_tick(window);
    caption::caption_tick(window);
    focus::focus_tick(window);
}
/** @brief Native Bind: original exactly once, then shared reset without granting native calls.
 * @param window Freshly bound window.
 */
inline void bind(void* window) noexcept {
    if (!detail::configured)
        return;
    {
        const detail::SuspendAdmission suspended;
        detail::config.bind(window);
    }
    if (detail::enabled.load(std::memory_order_acquire) && core::platform::thread_id() == detail::config.ui_thread)
        after_bind_release(window);
}
/** @brief Native Dispatch: shared Apply pipeline with fresh GUI-chain admission and no persistent grant.
 * @param context Current context. @param selector Native selector object.
 */
inline void dispatch(void* context, void* selector) noexcept {
    if (!detail::configured)
        return;
    if (!detail::enabled.load(std::memory_order_acquire) || core::platform::thread_id() != detail::config.ui_thread) {
        detail::forward_dispatch(context, selector);
        return;
    }
    const detail::Admission admission(detail::dispatch_context(context));
    dispatch_release(context, selector);
}
/** @brief Native constructor hook body; shared transport owns all replacement gates.
 * @param self Destination command. @param gui Model. @param selector Selector.
 * @param scope Scope. @param records Original record vector.
 */
inline void construct(void* self, const RecordString* gui, const RecordString* selector, const void* scope,
                      const TempVector* records) noexcept {
    if (!detail::configured)
        return;
    constructor_bridged(self, gui, selector, scope, records);
}
}  // namespace capie::editor::macos
