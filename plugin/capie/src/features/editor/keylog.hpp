/**
 * @file keylog.hpp
 * @brief Debug-flavor key and focus diagnostics (`RN_KEYLOG`, `RN_KEYTICK`). Not built into release hooks.
 *
 * Purpose: show, from copied scalars only, which key events reach our edit boxes and how
 * their focus/visibility routing bits change, so a runtime log can explain input bugs.
 *
 * `key_prelog` runs before the original key handler/wrapper and records the eight event
 * DWORDs plus routing state, but only for one of OUR five exact edit boxes (identity is
 * checked before any payload is copied; foreign widgets are never logged). Budgets per
 * source/field/category keep the file small: 32 meaningful keys, 2 empty polls and 2 read
 * failures, each followed by one cap line; later meaningful keys still go to the ring.
 * `observe_key_tick` logs field/model status transitions only (no heartbeat), capped at
 * 64 records per slot.
 *
 * The release flavor does not install the key and wrapper hooks at all, so none of this
 * runs there. In the debug flavor the behaviour and line format equal the accepted rev7.
 */
#pragma once
#include "../../core/log.hpp"
#include "diagnostics.hpp"
#include "lifecycle.hpp"
#include "window_view.hpp"

#include <atomic>
#include <cstdio>
#include <type_traits>

namespace capie::editor {

using core::logging::remember_log;

/// @brief Key record categories with independent budgets.
enum class KeyCategory : std::size_t { meaningful, empty_poll, event_read_failed };
/// @brief Number of categories.
constexpr std::size_t key_category_count = 3;
/// @brief File records per category before the cap line.
constexpr unsigned key_record_limits[key_category_count] = {32, 2, 2};
/// @brief File records per tick slot before the cap line.
constexpr unsigned tick_record_limit = 64;
/// @brief Category names in the log.
constexpr const char* key_category_names[key_category_count] = {"meaningful", "empty_poll", "event_read_failed"};
/// @brief Two sources: index 0 wrapper, 1 text-buffer handler.
constexpr std::size_t key_source_count = 2;
/// @brief Saturating budgets per source/field/category.
inline std::atomic<unsigned> key_record_counts[key_source_count][field_count][key_category_count]{};
/// @brief Sequence number of key log lines.
inline std::atomic<std::uint64_t> key_record_sequence{0};

/// @brief Bits of `KeyStatus::mask`: which reads succeeded.
namespace status_bit {
inline constexpr unsigned flags75 = 1;                                      ///< edit+75 read.
inline constexpr unsigned flags271 = 2;                                     ///< edit+271 read.
inline constexpr unsigned manager = 4;                                      ///< Manager global read.
inline constexpr unsigned widget = 8;                                       ///< manager+158 read.
inline constexpr unsigned receiver = 16;                                    ///< manager+230 read.
inline constexpr unsigned lifecycle = 32;                                   ///< Lifecycle copied.
inline constexpr unsigned manager_relations = manager | widget | receiver;  ///< All manager reads.
}  // namespace status_bit
/// @brief Bits of `KeyRecord::mask`.
namespace record_bit {
inline constexpr unsigned event_words = 1;   ///< Eight DWORDs copied.
inline constexpr unsigned wrapper_type = 2;  ///< Wrapper type DWORD copied.
}  // namespace record_bit
/// @brief Routing bits retained for transition detection (wrapper proof).
inline constexpr unsigned char routing_mask75 = core::flag::shown;
/// @brief Routing bits of edit+271 (active, disabled).
inline constexpr unsigned char routing_mask271 = core::flag::edit_active | core::flag::edit_disabled;

/// @brief Copied routing/focus/lifecycle status of one field.
struct KeyStatus {
    unsigned mask{};                                    ///< `status_bit` read mask.
    unsigned char flags75{};                            ///< edit+75.
    unsigned char flags271{};                           ///< edit+271.
    bool manager_present{};                             ///< Manager global non-null.
    bool widget_is_edit{};                              ///< Manager focus is this edit.
    bool receiver_is_edit{};                            ///< Manager receiver is this edit's receiver.
    bool visible{};                                     ///< Lifecycle visible.
    bool attempted{};                                   ///< Lifecycle attempted.
    bool prefilled{};                                   ///< Lifecycle prefilled.
    std::uint64_t epoch{};                              ///< Lifecycle epoch.
    std::uint64_t generation{};                         ///< Lifecycle generation.
    bool operator==(const KeyStatus&) const = default;  ///< Field-wise equality.
};
/// @brief One copied key event.
struct KeyRecord {
    std::uint32_t values[core::off::key_event_words]{};  ///< Event DWORDs.
    std::uint32_t wrapper_type{};                        ///< Wrapper type (wrapper source).
    unsigned mask{};                                     ///< `record_bit` mask.
    std::size_t field{field_count};                      ///< Own field index.
    bool wrapper{};                                      ///< Source is the wrapper.
    unsigned long thread{};                                      ///< Thread.
    KeyStatus status{};                                  ///< Status copy.
};
static_assert(std::is_trivially_copyable_v<KeyStatus> && std::is_trivially_copyable_v<KeyRecord>);
/// @brief Last logged status per field and per model.
struct TickRecord {
    bool seen{};         ///< A status was logged.
    unsigned view{};     ///< Last `ViewResult`.
    unsigned count{};    ///< Records logged.
    KeyStatus status{};  ///< Last routing status.
};
/// @brief Tick records: fields first, then models.
inline TickRecord tick_records[field_count + model_count]{};
/// @brief Guards `tick_records`. Never held across a game call.
inline core::platform::Lock logger_lock{};

/// @brief Result of taking a budget slot.
enum class LogSlot { record, cap, drop };
/// @brief Event word holding modifiers (zero for a plain Enter; 2272FA0).
inline constexpr std::size_t event_modifier_word = 2;
/// @brief Last event word, also checked by the native Enter predicate (2272FA0).
inline constexpr std::size_t event_final_word = 7;
/// @brief Size of a key log line buffer.
inline constexpr std::size_t key_line_capacity = 2048;
/// @brief Size of the event word rendering buffer.
inline constexpr std::size_t key_words_capacity = 384;
/// @brief Size of a key tick line buffer.
inline constexpr std::size_t tick_line_capacity = 1024;

/**
 * @brief Takes one slot of a saturating budget.
 * @param count Budget counter.
 * @param limit Records before the cap.
 * @return `record` below the limit, `cap` once at it, `drop` afterwards.
 */
inline LogSlot take_key_slot(std::atomic<unsigned>& count, unsigned limit) noexcept {
    auto current = count.load(std::memory_order_relaxed);
    while (current <= limit) {
        if (count.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
            return current < limit ? LogSlot::record : LogSlot::cap;
        }
    }
    return LogSlot::drop;  // Saturating: unlimited repeats cannot wrap the cap.
}

/**
 * @brief Copies a field's lifecycle into a status.
 * @param field Field index.
 * @param out Status.
 */
inline void copy_key_lifecycle(std::size_t field, KeyStatus& out) noexcept {
    core::platform::lock_shared(&state_lock);
    const auto& state = lifecycle[fields[field].model];
    out.visible = state.visible;
    out.attempted = state.attempted;
    out.prefilled = state.prefilled;
    out.epoch = state.epoch;
    out.generation = state.generation;
    out.mask |= status_bit::lifecycle;
    core::platform::unlock_shared(&state_lock);
}

/**
 * @brief Copies routing bits, manager relations and lifecycle of an edit.
 * @param edit Own edit box.
 * @param field Field index.
 * @param out Status.
 */
inline void copy_key_status(void* edit, std::size_t field, KeyStatus& out) noexcept {
    if (member(edit, off::edit_visibility_flags, out.flags75)) {
        out.mask |= status_bit::flags75;
    }
    if (member(edit, off::edit_flags, out.flags271)) {
        out.mask |= status_bit::flags271;
    }
    void* manager = nullptr;
    void* widget = nullptr;
    void* receiver = nullptr;
    if (member(reinterpret_cast<void*>(image_base), rva::manager_global, manager)) {
        out.mask |= status_bit::manager;
    }
    out.manager_present = manager != nullptr;
    if (member(manager, off::manager_focus, widget)) {
        out.mask |= status_bit::widget;
        out.widget_is_edit = widget == edit;
    }
    if (member(manager, off::manager_receiver, receiver)) {
        out.mask |= status_bit::receiver;
        out.receiver_is_edit =
            reinterpret_cast<std::uintptr_t>(receiver) == reinterpret_cast<std::uintptr_t>(edit) + off::edit_receiver;
    }
    copy_key_lifecycle(field, out);
}

/**
 * @brief Index of the own field this edit box is, if any.
 * @param edit Candidate widget.
 * @return Field index, or `field_count` when not ours.
 * @note Reads the three vtables once and compares the name with all five fields; same
 *       result as calling `edit_named` for each field in order.
 */
inline std::size_t own_field_of(void* edit) noexcept {
    const void* name = nullptr;
    if (!engine::edit_identity(edit, name)) {
        return field_count;
    }
    for (std::size_t i = 0; i < field_count; ++i) {
        if (name_equals(name, fields[i].edit)) {
            return i;
        }
    }
    return field_count;
}

/**
 * @brief Builds a key record for one of our edit boxes.
 * @param edit Candidate edit box.
 * @param event Key event.
 * @param wrapper Source is the wrapper.
 * @param out Record.
 * @return `false` for foreign widgets (nothing copied).
 */
inline bool make_key_record(void* edit, void* event, bool wrapper, KeyRecord& out) noexcept {
    out = {};
    // Own exact primary/template/TB type+name first. Never copy foreign payload.
    out.field = own_field_of(edit);
    if (out.field == field_count) {
        return false;
    }
    out.wrapper = wrapper;
    out.thread = core::platform::thread_id();
    if (engine::guarded_copy(out.values, event, sizeof(out.values))) {
        out.mask |= record_bit::event_words;
    }
    if (wrapper && member(event, off::key_wrapper_type, out.wrapper_type)) {
        out.mask |= record_bit::wrapper_type;
    }
    copy_key_status(edit, out.field, out.status);
    return true;  // Read/visibility/focus/readiness rejection is itself observable.
}

/**
 * @brief Category of a record.
 * @param value Record.
 * @return `event_read_failed` without a full copy, `empty_poll` for all-zero words, else `meaningful`.
 */
inline KeyCategory key_category(const KeyRecord& value) noexcept {
    // Failed/partial 8-DWORD copy cannot classify the contents as a key or poll.
    if ((value.mask & record_bit::event_words) == 0) {
        return KeyCategory::event_read_failed;
    }
    for (const auto word : value.values) {
        if (word != 0) {
            return KeyCategory::meaningful;
        }
    }
    return KeyCategory::empty_poll;  // A readable zero event can be a normal poll.
}

/**
 * @brief Keeps only the routing bits established by the wrapper proof.
 * @param value Status.
 * @return Status with routing bits only in the flag bytes.
 */
inline KeyStatus routing_status(KeyStatus value) noexcept {
    value.flags75 &= routing_mask75;
    value.flags271 &= routing_mask271;
    return value;
}

/**
 * @brief Writes (or rings) one key record according to its budget.
 * @param value Record.
 */
inline void emit_key_record(const KeyRecord& value) noexcept {
    const auto category = static_cast<std::size_t>(key_category(value));
    const auto limit = key_record_limits[category];
    const auto slot = take_key_slot(key_record_counts[value.wrapper ? 0 : 1][value.field][category], limit);
    if (slot == LogSlot::drop && category != static_cast<std::size_t>(KeyCategory::meaningful)) {
        return;
    }
    char line[key_line_capacity]{};
    const auto sequence = key_record_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const char* source = value.wrapper ? "wrapper" : "tb";
    if (slot == LogSlot::cap) {
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_KEYLOG seq=%llu source=%s field=%s category=%s category_cap=%u; later originals still forwarded\r\n",
            static_cast<unsigned long long>(sequence), source, fields[value.field].edit, key_category_names[category],
            limit));
        write_log(line);
        if (category != static_cast<std::size_t>(KeyCategory::meaningful)) {
            return;
        }
    }
    char words[key_words_capacity]{};
    std::size_t position = 0;
    for (std::size_t i = 0; i < core::off::key_event_words; ++i) {
        position += static_cast<std::size_t>(std::snprintf(words + position, sizeof(words) - position, "%s%u/0x%08X",
                                                           i != 0 ? "," : "", value.values[i], value.values[i]));
    }
    const auto& s = value.status;
    const bool event_read = (value.mask & record_bit::event_words) != 0;
    static_cast<void>(std::snprintf(
        line, sizeof(line),
        "RN_KEYLOG seq=%llu source=%s field=%s category=%s thread=%lu copy_mask=%u status_mask=%u event=[%s] "
        "wrapper_type=%u flags75=0x%02X flags271=0x%02X manager_present=%u widget_is_edit=%u receiver_is_edit=%u "
        "life_visible=%u attempted=%u prefilled=%u epoch=%llu generation=%llu "
        "event_read=%u wrapper_read=%u key0_enter=%u modifier2_zero=%u field7_zero=%u wrapper_type1=%u "
        "not_disabled02=%u visible08=%u active01=%u manager_read=%u; pre-original only\r\n",
        static_cast<unsigned long long>(sequence), source, fields[value.field].edit, key_category_names[category],
        value.thread, value.mask, s.mask, words, value.wrapper_type, static_cast<unsigned>(s.flags75),
        static_cast<unsigned>(s.flags271), s.manager_present ? 1U : 0U, s.widget_is_edit ? 1U : 0U,
        s.receiver_is_edit ? 1U : 0U, s.visible ? 1U : 0U, s.attempted ? 1U : 0U, s.prefilled ? 1U : 0U,
        static_cast<unsigned long long>(s.epoch), static_cast<unsigned long long>(s.generation), event_read ? 1U : 0U,
        (value.mask & record_bit::wrapper_type) != 0 ? 1U : 0U,
        event_read && value.values[0] == core::value::key_enter ? 1U : 0U,
        event_read && value.values[event_modifier_word] == 0 ? 1U : 0U,
        event_read && value.values[event_final_word] == 0 ? 1U : 0U,
        value.wrapper && (value.mask & record_bit::wrapper_type) != 0 &&
                value.wrapper_type == core::value::wrapper_type_key
            ? 1U
            : 0U,
        (s.mask & status_bit::flags271) != 0 && (s.flags271 & core::flag::edit_disabled) == 0 ? 1U : 0U,
        (s.mask & status_bit::flags75) != 0 && (s.flags75 & core::flag::shown) != 0 ? 1U : 0U,
        (s.mask & status_bit::flags271) != 0 && (s.flags271 & core::flag::edit_active) != 0 ? 1U : 0U,
        (s.mask & status_bit::manager_relations) == status_bit::manager_relations && s.manager_present ? 1U : 0U));
    if (slot == LogSlot::record) {
        write_log(line);
    } else {
        remember_log(line);  // Latest meaningful data survives the file quota.
    }
}

/**
 * @brief Pre-original key observation (debug flavor).
 * @param edit Edit box (primary object).
 * @param event Key event.
 * @param wrapper Source is the wrapper.
 */
#if defined(__APPLE__)
[[gnu::noinline]]
#else
__declspec(noinline)
#endif
inline void key_prelog(void* edit, void* event, bool wrapper) noexcept {
    if (!active.load(std::memory_order_acquire)) {
        return;
    }
    KeyRecord record{};
    if (make_key_record(edit, event, wrapper, record)) {
        emit_key_record(record);
    }
}

/**
 * @brief Pre-original observation from the text-buffer handler (`this` = edit + 0x80).
 * @param buffer Text-buffer subobject.
 * @param event Key event.
 */
inline void key_prelog_from_buffer(void* buffer, void* event) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(buffer);
    if (address >= off::edit_buffer) {
        key_prelog(reinterpret_cast<void*>(address - off::edit_buffer), event, false);
    }
}

/**
 * @brief Logs a field/model status transition (bounded per slot).
 * @param slot Tick record index.
 * @param name Field or model name.
 * @param view View result.
 * @param status Status.
 * @param model `true` for a model slot.
 */
inline void emit_tick_change(std::size_t slot, const char* name, ViewResult view, const KeyStatus& status,
                             bool model) noexcept {
    bool changed = false;
    bool cap = false;
    bool file_record = false;
    const auto semantic = routing_status(status);
    core::platform::lock(&logger_lock);
    auto& previous = tick_records[slot];
    if (!previous.seen || previous.view != static_cast<unsigned>(view) || !(previous.status == semantic)) {
        previous.seen = true;
        previous.view = static_cast<unsigned>(view);
        previous.status = semantic;
        changed = true;
        if (previous.count <= tick_record_limit) {
            cap = previous.count == tick_record_limit;
            ++previous.count;
            file_record = true;
        }
    }
    core::platform::unlock(&logger_lock);
    if (!changed) {
        return;
    }
    char line[tick_line_capacity]{};
    if (cap) {
        static_cast<void>(std::snprintf(line, sizeof(line), "RN_KEYTICK kind=%s name=%s cap=%u; no heartbeat\r\n",
                                        model ? "model" : "field", name, tick_record_limit));
    } else {
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_KEYTICK kind=%s name=%s view=%u status_mask=%u flags75=0x%02X flags271=0x%02X routing75=0x%02X "
            "routing271=0x%02X manager_present=%u widget_is_edit=%u receiver_is_edit=%u "
            "life_visible=%u attempted=%u prefilled=%u epoch=%llu generation=%llu; transition only\r\n",
            model ? "model" : "field", name, static_cast<unsigned>(view), status.mask,
            static_cast<unsigned>(status.flags75), static_cast<unsigned>(status.flags271),
            static_cast<unsigned>(semantic.flags75), static_cast<unsigned>(semantic.flags271),
            status.manager_present ? 1U : 0U, status.widget_is_edit ? 1U : 0U, status.receiver_is_edit ? 1U : 0U,
            status.visible ? 1U : 0U, status.attempted ? 1U : 0U, status.prefilled ? 1U : 0U,
            static_cast<unsigned long long>(status.epoch), static_cast<unsigned long long>(status.generation)));
    }
    if (file_record) {
        write_log(line);
    } else {
        remember_log(line);
    }
}

/**
 * @brief Post-original Tick observer of key routing state (debug flavor).
 * @param window Ticking window.
 */
inline void observe_key_tick(void* window) noexcept {
    if (!active.load(std::memory_order_acquire)) {
        return;
    }
    View view{};
    const auto result = inspect_window(window, view);
    if (result == ViewResult::unrelated) {
        return;
    }
    const auto& model = models[view.model];
    KeyStatus model_status{};
    copy_key_lifecycle(model.first, model_status);
    emit_tick_change(field_count + view.model, model.name, result, model_status, true);
    if (result == ViewResult::invalid) {
        return;  // No borrowed invalid-root dereference.
    }
    for (std::size_t field = model.first; field < model.first + model.count; ++field) {
        void* edit = engine::guarded_find(engine::find_edit, view.root, fields[field].edit);
        if (!engine::edit_named(edit, fields[field].edit)) {
            continue;
        }
        KeyStatus status{};
        copy_key_status(edit, field, status);
        emit_tick_change(field, fields[field].edit, result, status, false);
    }
}

}  // namespace capie::editor
