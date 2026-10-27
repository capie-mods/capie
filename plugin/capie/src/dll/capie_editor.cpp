/**
 * @file capie_editor.cpp
 * @brief Composition root of `capie_editor.dll`: hook bodies, initialization and exports.
 *
 * This DLL turns the mod's scripted inline editors into real text fields. It hooks the
 * scripted-window Tick, Bind and Dispatch functions and the effect constructor of the
 * pinned `hoi4.exe`, and wires the features in a fixed order:
 *   Tick (after the original): editor lifecycle and one-shot prefill -> grammar editors of
 *        other mods (`public_editor.hpp`) -> [debug: key tick, debug requests] -> caption
 *        refresh -> focus release;
 *   Bind (after the original): caption reset -> focus reset -> grammar editor reset ->
 *        lifecycle reset;
 *   Dispatch (before the original, own Apply only): [debug: pre-dispatch log] -> caption
 *        gate -> field capture -> transport build -> re-check -> one original dispatch
 *        (or a payload-free sentinel / a logged drop); a grammar Apply goes through the
 *        same steps in `public_dispatch` (no caption gate, `capie_*` records);
 *   effect constructor: substitutes the record vector for our own dispatch frame only.
 * The debug flavor additionally hooks the edit-box key handler and wrapper (key log) and
 * Execute (command observation), exactly as the accepted rev7 bridge did; the release
 * flavor does not install those three hooks because they only fed diagnostics.
 *
 * Contract of every hook body: call the original exactly once, never under a lock, pass
 * its result through unchanged; observers run after it (Tick, Bind) or before it without
 * changing its arguments (Dispatch, constructor).
 *
 * Initialization (`RNInitialize`, called once by the injector on a remote thread): refuses
 * co-loading with any predecessor bridge (40), checks the executable SHA-256 (10), the PE
 * headers (11) and `.text` (12), every editor signature (20), every caption signature
 * (20), caption slot pins and tokens (21), focus signatures, slot pins and the manager RIP
 * target (22); opens the log next to the DLL (13), pins the module (14), installs the hooks
 * (30/31/32) and only then arms caption, focus and the master switch. A failed attempt is
 * final for the process (33 on retry). The DLL is never unloaded.
 *
 * Exports: `RNInitialize`, and in the debug flavor `RNDebugRequest`.
 */
#include "../core/game_build.hpp"
#include "../core/hook_set.hpp"
#include "../core/init_codes.hpp"
#include "../core/log.hpp"
#include "../core/pe_image.hpp"
#include "../core/sha256.hpp"
#include "../core/signature_table.hpp"
#include "../features/caption/caption_adapter.hpp"
#include "../features/editor/debug_observer.hpp"
#include "../features/editor/diagnostics.hpp"
#include "../features/editor/keylog.hpp"
#include "../features/editor/lifecycle.hpp"
#include "../features/editor/prefill.hpp"
#include "../features/editor/public_editor.hpp"
#include "../features/editor/transport.hpp"
#include "../features/editor/window_view.hpp"
#include "../features/focus/focus_adapter.hpp"
#include "../features/host/host_pins.hpp"
#if CAPIE_CONSOLE_CANDIDATE
#include "../features/console/console_wiring.hpp"  // developer build only (build.ps1 -Console)
#endif

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <utility>

namespace capie::editor {
namespace {

using caption::caption_bind;
using caption::caption_dispatch_verdict;
using caption::caption_tick;
using core::InitCode;
using core::to_exit_code;
using focus::focus_bind;
using focus::focus_tick;

/// @brief Handle of this DLL (set in `DllMain`).
HMODULE own_module = nullptr;

/// @brief Bridge DLLs that must not be loaded together with this one (predecessors).
constexpr const wchar_t* refused_co_loads[] = {
    L"rename_anything_caption_refresh_bridge.dll",
    L"rename_anything_inline_editor_debug_bridge.dll",
    L"rename_anything_caption_refresh_candidate.dll",
    L"rename_anything_inline_editor_key_logger_v2.dll",
    L"rename_anything_inline_editor_key_logger.dll",
    L"rename_anything_inline_editor_bridge.dll",
    L"rename_anything_inline_editor_focus_bridge.dll",
    L"rename_anything_editor_observer.dll",
    L"rename_anything_editor_bridge.dll",
    L"rename_anything_inline_prefill.dll",
    L"rename_anything_inline_leader_bridge.dll",
};

/// @brief Longest path buffer used for module and log paths.
constexpr DWORD max_path_chars = 32768;

/**
 * @brief Hook body of Tick: original first, then the observers (pre-filtered per window).
 * @param window Scripted window.
 * @param force Original argument.
 */
void __fastcall tick_observed(void* window, char force) noexcept {
    original_tick(window, force);
    if (!active.load(std::memory_order_acquire)) {
        return;
    }
#if CAPIE_CONSOLE_CANDIDATE
    console::wiring::on_tick();  // registers the console commands once, on this (game) thread
#endif
    // Performance: one cheap identity read decides whether any observer can care about this
    // window; for every other window the observers would all return "unrelated".
    const bool own = window_may_be_own(window);
    if (own) {
        observe_tick(window);
        public_observe_tick(window);
        if constexpr (debug_diagnostics) {
            observe_key_tick(window);
        }
    }
    if constexpr (debug_diagnostics) {
        if (core::logging::debug_control.has_work()) {
            service_debug(window);
        }
    }
    if (own) {
        caption_tick(window);
        focus_tick(window);
    }
}

/**
 * @brief Hook body of Bind: original first, then caption/focus/lifecycle resets.
 * @param window Scripted window.
 */
void __fastcall bind_observed(void* window) noexcept {
    // Native construction/rebind and recursive widget binding finish first. Forward once;
    // do not access an old root or cache a window/widget pointer.
    original_bind(window);
    if (!active.load(std::memory_order_acquire)) {
        return;
    }
    caption_bind(window);
    focus_bind(window);
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

/**
 * @brief Whether a key event is a plain Enter (diagnostic copy of the native predicate).
 * @param event Key event.
 * @return `true` for Enter without modifiers.
 */
bool enter_event(void* event) noexcept {
    int values[core::off::key_event_words]{};
    // 2273320 active: event[0] != 0 && event[2] != -1; 2272FA0 additionally checks [7].
    return engine::guarded_copy(values, event, sizeof(values)) && std::cmp_equal(values[0], core::value::key_enter) &&
           values[event_modifier_word] == 0 && values[event_final_word] == 0;
}

/**
 * @brief Hook body of the text-buffer key handler (debug flavor): log, forward once, copy Enter.
 * @param buffer Text-buffer subobject (edit + 0x80).
 * @param event Key event.
 * @return Original RAX, unchanged.
 */
std::uint64_t __fastcall key_observed(void* buffer, void* event) noexcept {
    key_prelog_from_buffer(buffer, event);  // Before the unchanged Enter predicate.
    std::size_t field = field_count;
    std::uint64_t epoch = 0;
    std::uint64_t generation = 0;
    bool eligible = active.load(std::memory_order_acquire) && enter_event(event);
    const auto address = reinterpret_cast<std::uintptr_t>(buffer);
    void* edit = address >= off::edit_buffer ? reinterpret_cast<void*>(address - off::edit_buffer) : nullptr;
    if (eligible) {
        eligible = false;
        for (std::size_t i = 0; i < field_count; ++i) {
            if (engine::edit_named(edit, fields[i].edit) && engine::input_active(edit)) {
                field = i;
                eligible = true;
                break;
            }
        }
    }
    if (eligible) {
        AcquireSRWLockShared(&state_lock);
        const auto& state = lifecycle[fields[field].model];
        eligible = state.visible && state.prefilled;
        epoch = state.epoch;
        generation = state.generation;
        ReleaseSRWLockShared(&state_lock);
    }
    // Every key forwarded exactly once. No mutation inside native NotifyEnter.
    const auto result = original_key(buffer, event);
    if (eligible && active.load(std::memory_order_acquire)) {
        InputSnapshot text{};
        if (engine::edit_named(edit, fields[field].edit) && engine::snapshot_edit(edit, fields[field].edit, text)) {
            const auto id = next_observation_id();
            bool queued = false;
            AcquireSRWLockExclusive(&state_lock);
            const auto& state = lifecycle[fields[field].model];
            if (state.visible && state.prefilled && state.epoch == epoch && state.generation == generation) {
                pending[field] = {epoch, generation, GetTickCount64(), text, GetCurrentThreadId(), true};
                queued = true;
            }
            ReleaseSRWLockExclusive(&state_lock);
            if (queued) {
                diagnostic(id, "enter-copied-no-Apply", field, epoch, generation, &text, GetCurrentThreadId());
            }
        } else {
            diagnostic(next_observation_id(), "enter-post-read-rejected", field, epoch, generation);
        }
    }
    return result;
}

/**
 * @brief Hook body of the CEditBox key wrapper (debug flavor): opaque full RAX passthrough.
 * @param edit Edit box.
 * @param event Key event.
 * @return Original RAX, unchanged (no bool normalisation, no post-call dereference).
 */
__declspec(noinline) std::uint64_t __fastcall wrapper_observed(void* edit, void* event) noexcept {
    key_prelog(edit, event, true);
    return original_wrapper(edit, event);
}

/**
 * @brief Hook body of Execute (debug flavor): copy headers, forward once, log the return.
 * @param command Command (may be destroyed by the original).
 */
void __fastcall execute_observed(void* command) noexcept {
    ExecuteObservation copied{};
    if (active.load(std::memory_order_acquire)) {
        copied = debug_execute_command(command);
    }
    original_execute(command);  // Always once, including foreign/malformed commands.
    // The queue may destroy command. Only our copied scalars are used here.
    if (copied.own) {
        char line[short_line_capacity]{};
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_DEBUG stage=execute_return id=%lu model=%s effect_success=unverified; void native returned\r\n",
            copied.id, models[copied.model].name));
        write_log(line);
    }
}

/**
 * @brief Hook body of Dispatch: own Apply clicks go through the gates; everything else is forwarded.
 * @param context Window context.
 * @param opaque_selector Selector string.
 */
void __fastcall dispatch_observed(void* context, void* opaque_selector) noexcept {
    const auto* selector = static_cast<const MsvcString*>(opaque_selector);
    if constexpr (debug_diagnostics) {
        if (active.load(std::memory_order_acquire)) {
            debug_own_action(context, selector);
        }
    }
    const MsvcString* model_name = nullptr;
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
    const unsigned caption_code = caption_dispatch_verdict(context, model);
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

/**
 * @brief Verifies the caption fingerprints, slot pins and tokens.
 * @param text `.text` of the game image.
 * @return `ok`, `signature_mismatch` (20) or `caption_pin_mismatch` (21).
 */
InitCode caption_verify(const core::TextSection& text) noexcept {
    if (!core::verify_all(text, core::caption_signatures)) {
        return InitCode::signature_mismatch;
    }
    const auto image = reinterpret_cast<core::uptr>(text.base);
    const core::ProcessMemory memory{};
    for (const auto& pin : core::caption_slot_pins) {
        if (!core::verify_slot_pin(memory, image, pin)) {
            return InitCode::caption_pin_mismatch;
        }
    }
    for (const auto& token : core::caption_token_pins) {
        if (!core::verify_token_pin(memory, image, token)) {
            return InitCode::caption_pin_mismatch;
        }
    }
    return InitCode::ok;
}

/**
 * @brief Verifies the focus fingerprints, slot pins and manager RIP target.
 * @param text `.text` of the game image.
 * @return `ok` or `focus_pin_mismatch` (22).
 */
InitCode focus_verify(const core::TextSection& text) noexcept {
    if (!core::verify_all(text, core::focus_signatures)) {
        return InitCode::focus_pin_mismatch;
    }
    const auto image = reinterpret_cast<core::uptr>(text.base);
    const core::ProcessMemory memory{};
    for (const auto& pin : core::focus_slot_pins) {
        if (!core::verify_slot_pin(memory, image, pin)) {
            return InitCode::focus_pin_mismatch;
        }
    }
    return core::verify_rip_pin(memory, image, core::focus_manager_rip_pin) ? InitCode::ok
                                                                            : InitCode::focus_pin_mismatch;
}

/// @brief Indices into the full (debug) hook table, in creation order.
namespace hook {
constexpr std::size_t execute = 0;   ///< Execute (debug only).
constexpr std::size_t bind = 1;      ///< Bind.
constexpr std::size_t tick = 2;      ///< Tick.
constexpr std::size_t wrapper = 3;   ///< Key wrapper (debug only).
constexpr std::size_t key = 4;       ///< Text-buffer key handler (debug only).
constexpr std::size_t dispatch = 5;  ///< Dispatch.
constexpr std::size_t ctor = 6;      ///< Effect constructor.
constexpr std::size_t count = 7;     ///< Hooks in the debug flavor.
}  // namespace hook
/// @brief Indices into the release hook table (Bind, Tick, Dispatch, ctor).
namespace release_hook {
constexpr std::size_t bind = 0;      ///< Bind.
constexpr std::size_t tick = 1;      ///< Tick.
constexpr std::size_t dispatch = 2;  ///< Dispatch.
constexpr std::size_t ctor = 3;      ///< Effect constructor.
constexpr std::size_t count = 4;     ///< Hooks in the release flavor.
}  // namespace release_hook

/**
 * @brief Number of distinct model names in `debug_actions` (the "models=" count of the init line).
 * @return Distinct model names.
 */
constexpr std::size_t debug_model_count() noexcept {
    std::size_t distinct = 0;
    for (std::size_t i = 0; i < std::size(debug_actions); ++i) {
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j) {
            seen = seen || std::string_view(debug_actions[j].model) == std::string_view(debug_actions[i].model);
        }
        distinct += seen ? 0 : 1;
    }
    return distinct;
}

/// @brief Size of the initialization line buffer.
constexpr std::size_t init_line_capacity = 1536;

/**
 * @brief Writes the initialization line; every count comes from the tables it describes.
 * @note Debug flavor text is identical to rev7 apart from the CAPIE suffix.
 */
void write_init_line() noexcept {
    char line[init_line_capacity]{};
    int count = 0;
    if constexpr (debug_diagnostics) {
        count = std::snprintf(
            line, sizeof(line),
            "RN_DEBUG initialized format=1 ring=%zu ttl_ms=%llu models=%zu selectors=%zu fields=%zu; exact image + %zu "
            "bridge + %zu caption + %zu focus unique fingerprints; %zu shared hooks; caption refresh armed (%zu caption "
            "FP, %zu slot pins, %zu tokens, RequestViewRefresh(gui,%d) only, seed gate on leader/party Apply); focus release on "
            "politics hide (%zu FP, %zu slot pins, FocusSwitch(manager,null) once per transition); own copied history, "
            "scalar remote requests, fresh post-Tick snapshots, observed Execute; dispatch is not execution and Execute "
            "is not effect success; no auto-Apply, no synthetic input, focus only released (never set) on politics hide, "
            "Escape not handled; stale Apply -> epoch -1 sentinel; invalid field -> rn_native(b)_err; CAPIE %s "
            "capie_editor flavor=debug\r\n",
            core::trace::ring_capacity, static_cast<unsigned long long>(core::trace::snapshot_ttl_ms),
            debug_model_count(), std::size(debug_actions), field_count, core::editor_signature_count,
            core::caption_signature_count, core::focus_signature_count, hook::count, core::caption_signature_count,
            std::size(core::caption_slot_pins), std::size(core::caption_token_pins), core::value::politics_view_index,
            core::focus_signature_count, std::size(core::focus_slot_pins), core::capie_version);
    } else {
        count = std::snprintf(
            line, sizeof(line),
            "RN_LIFECYCLE initialized; exact image + %zu bridge + %zu caption + %zu focus unique fingerprints; %zu hooks "
            "(Tick, Bind, Dispatch, effect ctor); caption refresh armed (RequestViewRefresh(gui,%d) only, seed gate on leader/party "
            "Apply); focus release on politics hide (FocusSwitch(manager,null) once per transition); no auto-Apply, no "
            "synthetic input, Escape not handled; stale Apply -> epoch -code sentinel; invalid field -> rn_native(b)_err; "
            "log channels error+lifecycle; CAPIE %s capie_editor flavor=release\r\n",
            core::editor_signature_count, core::caption_signature_count, core::focus_signature_count,
            release_hook::count, core::value::politics_view_index, core::capie_version);
    }
    if (count > 0) {
        write_log(line, core::logging::stored_bytes(count, sizeof(line)));
    } else {
        write_log("RN_LIFECYCLE initialized (init line formatting failed)\r\n");
    }
}

/**
 * @brief Body of `RNInitialize` (see file comment for the order and codes).
 * @return Result code.
 */
InitCode initialize() {
    // Unique logger; predecessors cannot know this basename. Always use one variant in a fresh
    // process, never an inverse later co-load.
    if (GetModuleHandleW(core::editor_dll_name) != own_module) {
        return InitCode::co_load_refused;
    }
    for (const auto* name : refused_co_loads) {
        if (GetModuleHandleW(name) != nullptr) {
            return InitCode::co_load_refused;
        }
    }
    wchar_t executable[max_path_chars]{};
    if (GetModuleFileNameW(nullptr, executable, max_path_chars) == 0 ||
        core::sha256_file(executable) != core::game_exe_sha256) {
        return InitCode::unsupported_executable;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    core::TextSection text{};
    if (const auto located = core::locate_text(base, text); located != InitCode::ok) {
        return located;
    }
    if (!core::verify_all(text, core::editor_signatures)) {
        return InitCode::signature_mismatch;
    }
    if (const auto caption = caption_verify(text); caption != InitCode::ok) {
        return caption;
    }
    if (const auto focus = focus_verify(text); focus != InitCode::ok) {
        return focus;
    }
    wchar_t log_path[max_path_chars]{};
    if (GetModuleFileNameW(own_module, log_path, max_path_chars) == 0) {
        return InitCode::log_unavailable;
    }
    auto* slash = std::wcsrchr(log_path, L'\\');
    if (slash == nullptr || wcscpy_s(slash + 1, max_path_chars - static_cast<std::size_t>(slash + 1 - log_path),
                                     core::editor_log_name) != 0) {
        return InitCode::log_unavailable;
    }
    core::logging::log_file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (core::logging::log_file == INVALID_HANDLE_VALUE) {
        return InitCode::log_unavailable;
    }
    image_base = reinterpret_cast<std::uintptr_t>(base);
    engine::find_edit = reinterpret_cast<engine::FindWidget>(image_base + rva::find_edit);
    engine::find_instant = reinterpret_cast<engine::FindWidget>(image_base + rva::find_instant);
    engine::set_text = reinterpret_cast<engine::SetText>(image_base + rva::text_buffer_set);
    engine::refresh_edit = reinterpret_cast<engine::RefreshEdit>(image_base + rva::edit_refresh);
#if CAPIE_CONSOLE_CANDIDATE
    // Code bytes only; a failure turns the console feature off and is logged, the editor continues.
    static_cast<void>(console::wiring::prepare(text));
#endif
#if CAPIE_DEBUG_DIAGNOSTICS
    // Host probe: log only (no capie_host record); a failed pin turns it off, the editor continues.
    if (const char* failed = host::pins::first_failed_pin(text, core::ProcessMemory{}, image_base); failed == nullptr) {
        host_probe_ready.store(true, std::memory_order_release);
        write_log("CAPIE_HOST probe on (debug, log only, no record)\r\n");
    } else {
        char line[core::logging::suffix_capacity]{};
        const int count = std::snprintf(line, sizeof(line), "CAPIE_HOST probe off; pin %s\r\n", failed);
        write_log(line, core::logging::stored_bytes(count, sizeof(line)));
    }
#endif
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&own_module), &pinned) == 0) {
        return InitCode::module_pin_failed;
    }
    const core::HookSpec hooks[hook::count] = {
        {"editor.execute", rva::effect_execute, reinterpret_cast<void*>(&execute_observed),
         reinterpret_cast<void**>(&original_execute)},
        {"editor.bind", rva::window_bind, reinterpret_cast<void*>(&bind_observed),
         reinterpret_cast<void**>(&original_bind)},
        {"editor.tick", rva::window_tick, reinterpret_cast<void*>(&tick_observed),
         reinterpret_cast<void**>(&original_tick)},
        {"editor.wrapper", rva::edit_key_wrapper, reinterpret_cast<void*>(&wrapper_observed),
         reinterpret_cast<void**>(&original_wrapper)},
        {"editor.key", rva::edit_key_handler, reinterpret_cast<void*>(&key_observed),
         reinterpret_cast<void**>(&original_key)},
        {"editor.dispatch", rva::window_dispatch, reinterpret_cast<void*>(&dispatch_observed),
         reinterpret_cast<void**>(&original_dispatch)},
        {"editor.ctor", rva::effect_ctor, reinterpret_cast<void*>(&constructor_bridged),
         reinterpret_cast<void**>(&original_ctor)},
    };
    auto* image = base;
    InitCode installed = InitCode::ok;
    if constexpr (debug_diagnostics) {
        // rev7 order: create all seven, enable Execute, Bind, ctor, Dispatch, wrapper, key, Tick last.
        constexpr std::size_t enable_order[] = {hook::execute, hook::bind, hook::ctor, hook::dispatch,
                                                hook::wrapper, hook::key,  hook::tick};
        installed = core::install_in_order(image, hooks, std::size(hooks), enable_order, std::size(enable_order));
    } else {
        // Release: the three diagnostic-only hooks are not installed; Tick still goes live last.
        const core::HookSpec release_hooks[release_hook::count] = {hooks[hook::bind], hooks[hook::tick],
                                                                   hooks[hook::dispatch], hooks[hook::ctor]};
        constexpr std::size_t enable_order[] = {release_hook::bind, release_hook::ctor, release_hook::dispatch,
                                                release_hook::tick};
        installed = core::install_in_order(image, release_hooks, std::size(release_hooks), enable_order,
                                           std::size(enable_order));
    }
    if (installed != InitCode::ok) {
        if (installed == InitCode::hook_enable_failed) {
            write_log("RN_IEBR initialization failed; all actions disabled; restart required\r\n");
        }
        return installed;
    }
    engine::caption_request = reinterpret_cast<engine::CaptionRequest>(image_base + rva::request_view_refresh);
    caption::caption_armed.store(true, std::memory_order_release);
    engine::focus_switch = reinterpret_cast<engine::FocusSwitch>(image_base + rva::focus_switch);
    focus::focus_armed.store(true, std::memory_order_release);
    active.store(true, std::memory_order_release);
    write_init_line();
    return InitCode::ok;
}

}  // namespace
}  // namespace capie::editor

#if CAPIE_DEBUG_DIAGNOSTICS
/**
 * @brief Debug flavor: queues a scalar request from `capie_debug_control.exe` (remote thread).
 * @param argument Opcode 1..4 (snapshot, dump, pause, resume) passed as the pointer value.
 * @return 0 queued, 41 inactive, 42 invalid opcode, 43 serial exhausted.
 * @note No engine reads, file I/O, focus, SetText or dispatch on this thread; completion
 *       appears only in the post-Tick log.
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
extern "C" __declspec(dllexport) DWORD WINAPI RNDebugRequest(void* argument) {
    using capie::core::InitCode;
    using capie::core::to_exit_code;
    namespace logging = capie::core::logging;
    namespace trace = capie::core::trace;
    if (!capie::editor::active.load(std::memory_order_acquire)) {
        return to_exit_code(InitCode::inactive);
    }
    const auto code = reinterpret_cast<std::uintptr_t>(argument);
    if (code < trace::first_operation || code > trace::last_operation) {
        return to_exit_code(InitCode::invalid_request);
    }
    const auto operation = static_cast<trace::Operation>(code);
    AcquireSRWLockExclusive(&logging::debug_request_lock);
    if (!logging::debug_control.request(operation, GetTickCount64())) {
        ReleaseSRWLockExclusive(&logging::debug_request_lock);
        return to_exit_code(InitCode::request_exhausted);
    }
    if (operation == trace::Operation::pause) {
        logging::debug_ring.arm(false);
    } else if (operation == trace::Operation::resume) {
        logging::debug_ring.arm(true);
    }
    ReleaseSRWLockExclusive(&logging::debug_request_lock);
    return to_exit_code(InitCode::ok);  // Queued. Completion appears only in the post-Tick log.
}
#endif

/**
 * @brief Initializes the editor bridge once (remote thread from the injector).
 * @param unused Ignored.
 * @return 0 on success; see `capie::core::InitCode` for refusals. A failed attempt is final (33).
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void* unused) {
    static_cast<void>(unused);
    using capie::core::InitCode;
    using capie::core::to_exit_code;
    unsigned expected = 0;
    if (!capie::editor::init_state.compare_exchange_strong(expected, 1)) {
        return capie::editor::active.load() ? to_exit_code(InitCode::ok) : to_exit_code(InitCode::retry_refused);
    }
    try {
        return to_exit_code(capie::editor::initialize());
    } catch (...) {
        return to_exit_code(InitCode::unexpected_exception);
    }
}

/**
 * @brief Records the module handle; does nothing else (no work in the loader lock).
 * @param instance This DLL.
 * @param reason Notification.
 * @return TRUE.
 */
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage): loader entry point, external by definition
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        capie::editor::own_module = instance;
    }
    return TRUE;
}
