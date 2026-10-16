/**
 * @file debug_observer.hpp
 * @brief Debug-flavor observers: remote snapshot/dump requests, pre-dispatch and Execute observation.
 *
 * `service_debug` runs after the original Tick and serves requests posted by
 * `capie_debug_control.exe` through `RNDebugRequest`: it logs the request once, takes a
 * coherent snapshot of each own editor model on its next visible Tick (text read twice,
 * window and lifecycle re-checked after the last native lookup) and dumps the ring.
 * `debug_own_action` logs a recognised own selector before dispatch; `debug_execute_command`
 * copies the transport headers of an own command before Execute (the command may be
 * destroyed by the original, so only copied scalars are used afterwards).
 *
 * None of this changes game state, dispatches, types or sets focus. The release flavor
 * compiles none of these paths into its hooks and does not export `RNDebugRequest`.
 */
#pragma once
#include "../../core/log.hpp"
#include "diagnostics.hpp"
#include "keylog.hpp"
#include "lifecycle.hpp"
#include "trampolines.hpp"
#include "window_view.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace capie::editor {

using core::logging::debug_control;
using core::logging::dump_debug_ring;

/// @brief Size of a debug line buffer.
inline constexpr std::size_t debug_line_capacity = 768;
/// @brief Size of a request/pre-dispatch line buffer.
inline constexpr std::size_t request_line_capacity = 512;
/// @brief Size of an Execute observation line buffer.
inline constexpr std::size_t execute_line_capacity = 1024;
/// @brief Sanity limit on the scope count of a command.
inline constexpr int max_scope_count = 4096;

/**
 * @brief Whether two lifecycle copies describe the same opening state.
 * @param a First copy.
 * @param b Second copy.
 * @return `true` when epoch, generation, visible, prefilled and attempted match.
 */
inline bool same_lifecycle(const Lifecycle& a, const Lifecycle& b) noexcept {
    return a.epoch == b.epoch && a.generation == b.generation && a.visible == b.visible && a.prefilled == b.prefilled &&
           a.attempted == b.attempted;
}

/**
 * @brief Copies the lifecycle of a model under the shared lock.
 * @param model Model index.
 * @return Copy.
 */
inline Lifecycle lifecycle_copy(std::size_t model) noexcept {
    core::platform::lock_shared(&state_lock);
    const Lifecycle copy = lifecycle[model];
    core::platform::unlock_shared(&state_lock);
    return copy;
}

/**
 * @brief Coherent snapshot of one model's fields for a debug request.
 * @param window Window.
 * @param view Its inspection.
 * @param result Its classification.
 * @param serial Request serial.
 */
inline void debug_snapshot(void* window, const View& view, ViewResult result, std::uint64_t serial) noexcept {
    const char* reason = "ok";
    std::uint64_t epoch = 0;
    Lifecycle opening{};
    InputSnapshot texts[max_fields_per_model]{};
    KeyStatus statuses[max_fields_per_model]{};
    const auto& model = models[view.model];
    const auto ok = [&reason]() noexcept { return std::strcmp(reason, "ok") == 0; };
    if (result != ViewResult::visible) {
        reason = result == ViewResult::hidden ? "own_root_hidden" : "own_root_invalid";
    } else if (!read_epoch(view.root, view.model, epoch)) {
        reason = "epoch_unreadable";
    } else {
        opening = lifecycle_copy(view.model);
        for (std::size_t j = 0; j < model.count; ++j) {
            const auto field = model.first + j;
            void* edit = engine::guarded_find(engine::find_edit, view.root, fields[field].edit);
            if (!engine::edit_named(edit, fields[field].edit)) {
                reason = "edit_identity";
                break;
            }
            if (!engine::visible_edit(edit)) {
                reason = "edit_hidden";
                break;
            }
            if (!engine::snapshot_edit(edit, fields[field].edit, texts[j])) {
                reason = "text_unreadable_or_oversize";
                break;
            }
            copy_key_status(edit, field, statuses[j]);
        }
        View fresh{};
        std::uint64_t fresh_epoch = 0;
        if (ok() &&
            (inspect_window(window, fresh) != ViewResult::visible || fresh.model != view.model ||
             fresh.root != view.root || !read_epoch(fresh.root, view.model, fresh_epoch) || fresh_epoch != epoch)) {
            reason = "view_or_epoch_changed";
        }
        const Lifecycle after = lifecycle_copy(view.model);
        if (ok() && !same_lifecycle(after, opening)) {
            reason = "lifecycle_changed";
        }
        if (ok()) {
            for (std::size_t j = 0; j < model.count; ++j) {
                const auto field = model.first + j;
                InputSnapshot check{};
                void* edit = engine::guarded_find(engine::find_edit, fresh.root, fields[field].edit);
                if (!engine::visible_edit(edit) || !engine::snapshot_edit(edit, fields[field].edit, check) ||
                    !engine::same_text(check, texts[j])) {
                    reason = "text_changed_during_snapshot";
                    break;
                }
            }
        }
        // Widget lookup is native and may be reentrant. Check the binding and lifecycle again
        // after the last lookup, even when text stayed equal.
        if (ok()) {
            View final_view{};
            std::uint64_t final_epoch = 0;
            if (inspect_window(window, final_view) != ViewResult::visible || final_view.model != view.model ||
                final_view.root != view.root || !read_epoch(final_view.root, view.model, final_epoch) ||
                final_epoch != epoch) {
                reason = "final_view_or_epoch_changed";
            }
            const Lifecycle final_life = lifecycle_copy(view.model);
            if (ok() && !same_lifecycle(final_life, opening)) {
                reason = "final_lifecycle_changed";
            }
        }
    }
    char line[debug_line_capacity]{};
    static_cast<void>(
        std::snprintf(line, sizeof(line),
                      "RN_DEBUG stage=snapshot request=%llu model=%s reason=%s epoch=%llu generation=%llu "
                      "life_visible=%u attempted=%u prefilled=%u; observation only, no Apply\r\n",
                      static_cast<unsigned long long>(serial), model.name, reason,
                      static_cast<unsigned long long>(epoch), static_cast<unsigned long long>(opening.generation),
                      opening.visible ? 1U : 0U, opening.attempted ? 1U : 0U, opening.prefilled ? 1U : 0U));
    write_log(line);
    if (!ok()) {
        return;
    }
    for (std::size_t j = 0; j < model.count; ++j) {
        const auto field = model.first + j;
        const auto& s = statuses[j];
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_DEBUG stage=field_status request=%llu field=%s status_mask=%u flags75=0x%02X flags271=0x%02X "
            "manager_present=%u widget_is_edit=%u receiver_is_edit=%u\r\n",
            static_cast<unsigned long long>(serial), fields[field].edit, s.mask, static_cast<unsigned>(s.flags75),
            static_cast<unsigned>(s.flags271), s.manager_present ? 1U : 0U, s.widget_is_edit ? 1U : 0U,
            s.receiver_is_edit ? 1U : 0U));
        write_log(line);
        diagnostic(next_observation_id(), "debug-request-field-snapshot", field, epoch, opening.generation, &texts[j],
                   core::platform::thread_id());
    }
}

/**
 * @brief Serves pending debug requests after the original Tick.
 * @param window Ticking window (any window; own models get snapshots).
 */
inline void service_debug(void* window) noexcept {
    View view{};
    const auto result = inspect_window(window, view);
    const auto work =
        debug_control.take(core::platform::monotonic_ms(), result == ViewResult::unrelated ? 0U : 1U << view.model);
    char line[request_line_capacity]{};
    if (work.changed || work.expired != 0) {
        static_cast<void>(
            std::snprintf(line, sizeof(line),
                          "RN_DEBUG stage=request request=%llu operation=%zu pending_models=0x%X expired_models=0x%X "
                          "coalescing=latest_request ttl_ms=%llu; post-original UI tick, queued is not completed\r\n",
                          static_cast<unsigned long long>(work.serial), static_cast<std::size_t>(work.operation),
                          work.pending, work.expired, static_cast<unsigned long long>(core::trace::snapshot_ttl_ms)));
        write_log(line);
    }
    if (work.snapshot) {
        debug_snapshot(window, view, result, work.serial);
    }
    if (work.dump) {
        dump_debug_ring(work.serial);
    }
}

/**
 * @brief Logs a recognised own selector before dispatch (no effect on dispatch).
 * @param context Window context of the click.
 * @param selector Selector string.
 */
inline void debug_own_action(void* context, const RecordString* selector) noexcept {
    std::uintptr_t context_vt = 0;
    std::uintptr_t model_vt = 0;
    void* model = nullptr;
    if (!member(context, 0, context_vt) || context_vt != image_base + rva::vt_sg_context ||
        !member(context, off::context_model, model) || !member(model, 0, model_vt) ||
        model_vt != image_base + rva::vt_sg_model) {
        return;
    }
    const auto model_address = reinterpret_cast<std::uintptr_t>(model);
    if (model_address > core::uptr_max - off::model_name) {
        return;
    }
    for (const auto& action : debug_actions) {
        if (!name_equals(reinterpret_cast<void*>(model_address + off::model_name), action.model) ||
            !name_equals(selector, action.selector)) {
            continue;
        }
        void* window = nullptr;
        void* window_model = nullptr;
        void* root = nullptr;
        void* context_root = nullptr;
        std::uintptr_t root_vt = 0;
        unsigned char flags = 0;
        unsigned char desired = 0;
        const bool has_window = member(context, off::context_window, window);
        const auto window_address = reinterpret_cast<std::uintptr_t>(window);
        const bool valid =
            has_window && window_address != 0 && window_address <= core::uptr_max - off::window_context &&
            reinterpret_cast<std::uintptr_t>(context) == window_address + off::window_context &&
            member(window, off::window_model, window_model) && window_model == model &&
            member(context, off::context_root, context_root) && member(window, off::window_root, root) &&
            root == context_root && member(root, 0, root_vt) && root_vt == image_base + rva::vt_sg_root &&
            member(root, off::root_flags, flags) && member(window, off::window_desired, desired) &&
#if defined(__APPLE__)
            engine::macos::root_guard(
                core::ProcessMemory{}, reinterpret_cast<std::uintptr_t>(root),
                {image_base + rva::vt_sg_root, image_base + rva::vt_edit, image_base + rva::vt_instant_text});
#else
            readable(root, off::root_readable_span) && bounded_list(root, off::root_edit_list) &&
            bounded_list(root, off::root_instant_list) && bounded_list(root, off::root_third_list);
#endif
        char line[request_line_capacity]{};
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_DEBUG stage=pre_dispatch model=%s selector=%s action=%s reason=%s visible=%u "
            "desired=%u root_flags=0x%02X; recognized selector, execution and effect not yet observed\r\n",
            action.model, action.selector, action.action, valid ? "fresh_context" : "context_guard_failed",
            valid && desired != 0 && flag::shown_not_hidden(flags) ? 1U : 0U, static_cast<unsigned>(desired),
            static_cast<unsigned>(flags)));
        write_log(line);
        return;
    }
}

/// @brief Bits of the `header_mask` logged by `debug_execute_command`.
namespace header_bit {
inline constexpr unsigned epoch = 1;        ///< `rn_ip_cmd_epoch` present.
inline constexpr unsigned a_ok = 2;         ///< `rn_native_ok` present.
inline constexpr unsigned a_len = 4;        ///< `rn_native_len` present.
inline constexpr unsigned b_ok = 8;         ///< `rn_nativeb_ok` present.
inline constexpr unsigned b_len = 16;       ///< `rn_nativeb_len` present.
inline constexpr unsigned capability = 32;  ///< `rn_native_cap` present.
}  // namespace header_bit

/// @brief Copied result of an Execute pre-observation.
struct ExecuteObservation {
    bool own{};           ///< Command is one of our Apply commands.
    std::size_t model{};  ///< Model index.
    unsigned long id{};   ///< Observation id.
};

/**
 * @brief Copies the transport headers of an own command before Execute.
 * @param command Command object.
 * @return Copied identification (no pointers).
 */
inline ExecuteObservation debug_execute_command(void* command) noexcept {
    ExecuteObservation observation{};
    std::uintptr_t vt = 0;
    if (!member(command, 0, vt) || vt != image_base + rva::vt_execute_effect) {
        return observation;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(command);
    if (address > core::uptr_max - off::command_model) {
        return observation;
    }
    for (std::size_t model = 0; model < model_count; ++model) {
        if (!name_equals(reinterpret_cast<void*>(address + off::command_model), models[model].name) ||
            !name_equals(reinterpret_cast<void*>(address + off::command_selector), models[model].apply)) {
            continue;
        }
        observation = {true, model, next_observation_id()};
        TempVector vector{};
        int scopes = 0;
        int scope_capacity = 0;
        const bool scope_read = member(command, off::command_scope_count, scopes) &&
                                member(command, off::command_scope_capacity, scope_capacity) && scopes >= 0 &&
                                scopes <= max_scope_count && scope_capacity >= scopes &&
                                scope_capacity <= engine::max_vector_capacity;
        const bool vector_read = member(command, off::command_records, vector) && vector.count >= 0 &&
                                 std::cmp_less_equal(vector.count, max_original_records + max_transport_records) &&
                                 vector.capacity >= vector.count && vector.capacity <= engine::max_vector_capacity &&
                                 core::readable(vector.data, static_cast<std::size_t>(vector.count) * sizeof(Record));
        unsigned header_mask = 0;
        std::int64_t epoch = 0;
        std::int64_t a_ok = 0;
        std::int64_t a_len = 0;
        std::int64_t b_ok = 0;
        std::int64_t b_len = 0;
        std::int64_t capability = 0;
        bool records_valid = vector_read;
        if (vector_read) {
            for (int i = 0; i < vector.count; ++i) {
                Record record{};
                char key[engine::max_name_bytes + 1]{};
                if (!engine::guarded_copy(&record, vector.data + i, sizeof(record)) ||
                    !engine::record_key(record, key)) {
                    records_valid = false;
                    break;
                }
                if (std::strcmp(key, command_epoch_key) == 0) {
                    header_mask |= header_bit::epoch;
                    epoch = record.raw_value;
                } else if (std::strcmp(key, key_a_ok) == 0) {
                    header_mask |= header_bit::a_ok;
                    a_ok = record.raw_value;
                } else if (std::strcmp(key, key_a_len) == 0) {
                    header_mask |= header_bit::a_len;
                    a_len = record.raw_value;
                } else if (std::strcmp(key, key_b_ok) == 0) {
                    header_mask |= header_bit::b_ok;
                    b_ok = record.raw_value;
                } else if (std::strcmp(key, key_b_len) == 0) {
                    header_mask |= header_bit::b_len;
                    b_len = record.raw_value;
                } else if (std::strcmp(key, key_capability) == 0) {
                    header_mask |= header_bit::capability;
                    capability = record.raw_value;
                }
            }
        }
        char line[execute_line_capacity]{};
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_DEBUG stage=pre_execute id=%lu model=%s selector=%s reason=%s records=%d scopes_read=%u "
            "scopes=%d header_mask=%u epoch_raw=%lld a_ok_raw=%lld a_len_raw=%lld b_ok_raw=%lld b_len_raw=%lld "
            "capability_raw=%lld fixed_point=%lld; queue reached Execute, target authority and effect success "
            "unverified\r\n",
            observation.id, models[model].name, models[model].apply,
            records_valid ? "copied_headers" : "record_guard_failed", vector.count, scope_read ? 1U : 0U, scopes,
            header_mask, static_cast<long long>(epoch), static_cast<long long>(a_ok), static_cast<long long>(a_len),
            static_cast<long long>(b_ok), static_cast<long long>(b_len), static_cast<long long>(capability),
            static_cast<long long>(fixed_point)));
        write_log(line);
        break;
    }
    return observation;
}

}  // namespace capie::editor
