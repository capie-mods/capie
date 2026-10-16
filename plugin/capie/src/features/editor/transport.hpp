/**
 * @file transport.hpp
 * @brief Carries the typed text of an own Apply to the script as temp-variable records.
 *
 * HOI4 script cannot read an edit box, so on an own Apply click the bridge snapshots the
 * fields, converts each to the alphabet codes of `input_codes.hpp` and appends records to
 * a copy of the click's temp-variable vector:
 *   per field `rn_native(b)_ok`, `_len`, `_0.._31` (slot 31 becomes `_err` = InputError
 *   when the field is invalid), then `rn_ip_cmd_epoch` (opening epoch) and, for the
 *   country, `rn_native_cap = 1`.
 * Reserved keys already present in the original vector are removed first, so neither the
 * script nor the player can spoof them. The effect constructor deep-copies the vector; the
 * bridge substitutes its stack copy only inside that one constructor call, identified by
 * pointer equality with the dispatch frame and exact model/selector names (`origin` is
 * thread-local, so commands built on other threads, e.g. AI, are never touched).
 *
 * Gates before dispatch (all fresh, in this order): own visible editor window of the same
 * model, epoch and lifecycle readiness, own edit identity/visibility/text, a second
 * identical read after building the records, and for the country `capie_core.dll`
 * reporting its capability. Any failure drops the Apply with a logged guard, or, while
 * the own editor window is live, forwards a payload-free sentinel (`rn_ip_cmd_epoch =
 * -code`, see `StaleCode`) so the script can tell the player what to do (RN6/R5).
 *
 * Exactly one original dispatch per click; no retry; no lock across it.
 *
 * The same frame, record copy and constructor substitution also carry the Apply of a
 * grammar editor (`public_editor.hpp`): such a frame names its model and selector in
 * `public_model`/`public_apply`, and every incoming `capie_*` record is removed as well.
 */
#pragma once
#include "../../core/game_build.hpp"
#include "../caption/caption_adapter.hpp"
#include "../country/capability.hpp"
#include "diagnostics.hpp"
#include "lifecycle.hpp"
#include "public_schema.hpp"
#include "trampolines.hpp"
#include "window_view.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace capie::editor {

/// @brief Stack-bound state of one own dispatch; never stored in lifecycle/pending state.
struct Origin {
    void* context{};                                             ///< Window context of the click.
    const RecordString* model_name{};                            ///< Model name inside the model object.
    const RecordString* selector{};                              ///< Selector passed to Dispatch.
    const void* scope{};                                         ///< context + 3C0 (constructor scope argument).
    const TempVector* source{};                                  ///< context + 470 (original record vector).
    Record records[max_original_records + max_frame_records]{};  ///< Substituted records.
    TempVector view{};                                           ///< Vector handed to the constructor.
    unsigned ctor_calls{};                                       ///< Constructor calls seen in this frame.
    unsigned long id{};                                          ///< Observation id.
    std::uint64_t epoch{};                                       ///< Opening epoch.
    std::uint64_t generation{};                                  ///< Opening generation.
    std::size_t model{model_count};                              ///< Model index (own editors).
    const char* public_model{};  ///< Grammar editor: model name (registry storage, immutable), else null.
    const char* public_apply{};  ///< Grammar editor: Apply selector (registry storage, immutable), else null.
};

/// @brief The dispatch frame of the current thread, if any.
inline thread_local Origin* origin = nullptr;

/// @brief Scoped install of `origin` for the duration of one original dispatch.
struct OriginFrame {
    Origin* previous;  ///< Restored on scope exit.
    /**
     * @brief Installs `next`.
     * @param next Frame.
     */
    explicit OriginFrame(Origin* next) noexcept : previous(origin) { origin = next; }
    ~OriginFrame() { origin = previous; }
    OriginFrame(const OriginFrame&) = delete;
    OriginFrame& operator=(const OriginFrame&) = delete;
    OriginFrame(OriginFrame&&) = delete;
    OriginFrame& operator=(OriginFrame&&) = delete;
};

/// @brief Size of a transport log line buffer.
inline constexpr std::size_t transport_line_capacity = 512;

/**
 * @brief Recognises an own Apply click.
 * @param context Window context.
 * @param selector Selector.
 * @param model_name Receives the model name address.
 * @param index Receives the model index.
 * @return `true` for one of the three own Apply selectors on its own model.
 */
inline bool own_commit(void* context, const RecordString* selector, const RecordString*& model_name,
                       std::size_t& index) noexcept {
    std::uintptr_t context_vt = 0;
    std::uintptr_t model_vt = 0;
    void* model = nullptr;
    if (!member(context, 0, context_vt) || context_vt != image_base + rva::vt_sg_context ||
        !member(context, off::context_model, model) || !member(model, 0, model_vt) ||
        model_vt != image_base + rva::vt_sg_model) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(model);
    if (address > core::uptr_max - off::model_name) {
        return false;
    }
    model_name = reinterpret_cast<const RecordString*>(address + off::model_name);
    for (std::size_t i = 0; i < model_count; ++i) {
        if (name_equals(model_name, models[i].name) && name_equals(selector, models[i].apply)) {
            index = i;
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether the context belongs to a visible own window of `model`.
 * @param context Window context.
 * @param model Model index.
 * @param view Receives the inspection.
 * @return `true` when the context is embedded in a visible window of that model.
 */
inline bool context_view(void* context, std::size_t model, View& view) noexcept {
    void* window = nullptr;
    if (!member(context, off::context_window, window)) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    return address != 0 && address <= core::uptr_max - off::window_context &&
           reinterpret_cast<std::uintptr_t>(context) == address + off::window_context &&
           inspect_window(window, view) == ViewResult::visible && view.model == model;
}

/**
 * @brief `context_view` with the drop reasons logged.
 * @param context Window context.
 * @param model Model index.
 * @param view Receives the inspection.
 * @param id Observation id.
 * @return `true` when valid.
 */
inline bool action_view(void* context, std::size_t model, View& view, unsigned long id) noexcept {
    void* window = nullptr;
    if (!member(context, off::context_window, window)) {
        bridge_rejected(id, "context-window");
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    if (address == 0 || address > core::uptr_max - off::window_context ||
        reinterpret_cast<std::uintptr_t>(context) != address + off::window_context ||
        inspect_window(window, view) != ViewResult::visible || view.model != model) {
        bridge_rejected(id, "fresh-own-action-view");
        return false;
    }
    return true;
}

/**
 * @brief Snapshots the fields of an own Apply.
 * @param current Dispatch frame (epoch and generation are filled).
 * @param snapshots Receives the field texts.
 * @return `false` (logged) when the window, epoch, readiness or a field fails.
 */
inline bool capture_fields(Origin& current, InputSnapshot (&snapshots)[max_fields_per_model]) noexcept {
    View view{};
    if (!action_view(current.context, current.model, view, current.id)) {
        return false;
    }
    if (!read_epoch(view.root, current.model, current.epoch) ||
        !ready_model(current.model, current.epoch, current.generation, false)) {
        bridge_rejected(current.id, "opening-epoch-prefill-state");
        return false;
    }
    const auto& model = models[current.model];
    for (std::size_t j = 0; j < model.count; ++j) {
        const auto field = model.first + j;
        void* edit = engine::guarded_find(engine::find_edit, view.root, fields[field].edit);
        if (!engine::edit_named(edit, fields[field].edit) || !engine::visible_edit(edit) ||
            !engine::snapshot_edit(edit, fields[field].edit, snapshots[j])) {
            bridge_rejected(current.id, "own-edit-identity-visibility-text");
            return false;
        }
        // A button click may release focus. Both fields belong to the same fresh View/epoch.
        diagnostic(current.id, "button-field-snapshot", field, current.epoch, current.generation, &snapshots[j],
                   core::platform::thread_id());
    }
    return true;
}

/**
 * @brief Queries the capability export of `capie_core.dll` afresh.
 * @return The export's value, 0 when the module or export is missing or the call faults.
 */
#if defined(__APPLE__)
/// @brief Capability query installed only after native core verification; never resolved by symbol guessing.
using CountryCapabilitiesQuery = std::uint32_t (*)() noexcept;
/// @brief Null by default; no native country payload is authorized until explicitly bound.
inline std::atomic<CountryCapabilitiesQuery> verified_country_capabilities{nullptr};
inline std::uint32_t country_capabilities() noexcept {
    const auto query = verified_country_capabilities.load(std::memory_order_acquire);
    return query != nullptr ? query() : 0;
}
#else
inline DWORD country_capabilities() noexcept {
    using Query = DWORD(WINAPI*)();
    auto* const module = GetModuleHandleW(core::core_dll_name);
    if (module == nullptr) {
        return 0;
    }
    const auto query = reinterpret_cast<Query>(GetProcAddress(module, country::capabilities_export));
    if (query == nullptr) {
        return 0;
    }
    __try {
        return query();
    } __except (core::memory_exception(GetExceptionCode())) {
        return 0;
    }
}
#endif

/**
 * @brief Whether a country Apply may run: `capie_core.dll` is initialized with the name layers.
 * @return `true` while the name layers are active (v2, v3 or a v4 value).
 */
inline bool country_ready() noexcept {
    return country::names_active(country_capabilities());
}

/**
 * @brief Whether the flag layer is active (API level 6, record `capie_flag`).
 * @return `true` for `country_capabilities_v3` or a v4 value with the flag bit.
 */
inline bool flag_ready() noexcept {
    return country::flag_active(country_capabilities());
}

/**
 * @brief Whether the leader portrait layer is active (API level 7, record `capie_portrait`).
 * @return `true` for a v4 value.
 */
inline bool portrait_ready() noexcept {
    return country::portrait_active(country_capabilities());
}

/**
 * @brief Re-checks everything right before the original dispatch.
 * @param current Dispatch frame.
 * @param snapshots Texts captured earlier.
 * @return `false` (logged) when anything changed.
 */
inline bool still_current(const Origin& current, const InputSnapshot (&snapshots)[max_fields_per_model]) noexcept {
    View view{};
    std::uint64_t epoch = 0;
    std::uint64_t generation = current.generation;
    if (!action_view(current.context, current.model, view, current.id) ||
        !read_epoch(view.root, current.model, epoch) || epoch != current.epoch ||
        !ready_model(current.model, epoch, generation, true)) {
        bridge_rejected(current.id, "pre-dispatch-view-epoch-generation-changed");
        return false;
    }
    const auto& model = models[current.model];
    for (std::size_t j = 0; j < model.count; ++j) {
        const auto field = model.first + j;
        void* edit = engine::guarded_find(engine::find_edit, view.root, fields[field].edit);
        InputSnapshot fresh{};
        if (!engine::edit_named(edit, fields[field].edit) || !engine::visible_edit(edit) ||
            !engine::snapshot_edit(edit, fields[field].edit, fresh) || !engine::same_text(snapshots[j], fresh)) {
            bridge_rejected(current.id, "pre-dispatch-edit-text-changed");
            return false;
        }
    }
    // Re-read after the text snapshot as well; lifecycle locks never cross native dispatch.
    std::uint64_t after = 0;
    if (!read_epoch(view.root, current.model, after) || after != epoch ||
        !ready_model(current.model, epoch, generation, true)) {
        bridge_rejected(current.id, "pre-dispatch-epoch-generation-changed");
        return false;
    }
    if (current.model == country_model && !country_ready()) {
        bridge_rejected(current.id, "country-hooks-not-active-at-dispatch");
        return false;
    }
    return true;
}

/**
 * @brief Whether a record key belongs to the public contract (removed from grammar Apply vectors).
 * @param key Key.
 * @return `true` for every key starting with `capie_`.
 */
inline bool public_reserved_key(const char* key) noexcept {
    return std::string_view(key).starts_with(public_reserved_prefix);
}

/**
 * @brief Whether a record key is reserved for the bridge (removed from incoming vectors).
 * @param key Key.
 * @return `true` for headers, error slots, epoch, capability and `rn_native(b)_0..31`.
 */
inline bool reserved_key(const char* key) noexcept {
    if (std::strcmp(key, key_a_ok) == 0 || std::strcmp(key, key_a_len) == 0 || std::strcmp(key, key_b_ok) == 0 ||
        std::strcmp(key, key_b_len) == 0 || std::strcmp(key, key_capability) == 0 ||
        std::strcmp(key, command_epoch_key) == 0 || std::strcmp(key, key_a_err) == 0 ||
        std::strcmp(key, key_b_err) == 0) {
        return true;
    }
    for (unsigned i = 0; i < code_slots; ++i) {
        char expected[record_key_buffer]{};
        static_cast<void>(std::snprintf(expected, sizeof(expected), "%s_%u", field_a_prefix, i));
        if (std::strcmp(key, expected) == 0) {
            return true;
        }
        static_cast<void>(std::snprintf(expected, sizeof(expected), "%s_%u", field_b_prefix, i));
        if (std::strcmp(key, expected) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Builds one SSO record `key = value` (raw = value * fixed_point).
 * @param record Destination.
 * @param key Key (<= 15 bytes).
 * @param value Unscaled value.
 * @return `false` for a long key or a value that would overflow when scaled.
 */
inline bool transport_record(Record& record, const char* key, std::uint64_t value) noexcept {
    const auto size = std::strlen(key);
    if (size > max_record_key ||
        value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / fixed_point)) {
        return false;
    }
    record = {};
    if (!engine::inline_record_key(record, std::string_view(key, size))) {
        return false;
    }
    record.raw_value = static_cast<std::int64_t>(value) * fixed_point;
    return true;
}

/// @brief Result of `copy_original_records`.
enum class CopyResult { ok, bad_vector, bad_record };

/**
 * @brief Copies the context's record vector without reserved keys into `current.records`.
 * @param current Dispatch frame (scope/source set here).
 * @param source Receives the copied vector header.
 * @param kept Receives the number of kept records.
 * @param sanitized Receives the number of removed reserved records.
 * @return `CopyResult::ok`, or which part was malformed.
 */
inline CopyResult copy_original_records(Origin& current, TempVector& source, std::size_t& kept,
                                        std::size_t& sanitized) noexcept {
    const auto context_address = reinterpret_cast<std::uintptr_t>(current.context);
    current.scope = reinterpret_cast<const void*>(context_address + off::context_scope);
    current.source = reinterpret_cast<const TempVector*>(context_address + off::context_records);
    if (!engine::guarded_copy(&source, current.source, sizeof(source)) || source.count < 0 ||
        std::cmp_greater(source.count, max_original_records) || source.capacity < source.count ||
        source.capacity > engine::max_vector_capacity ||
        !core::readable(source.data, static_cast<std::size_t>(source.count) * sizeof(Record))) {
        return CopyResult::bad_vector;
    }
    kept = 0;
    sanitized = 0;
    for (int i = 0; i < source.count; ++i) {
        Record record{};
        char key[engine::max_name_bytes + 1]{};
        if (!engine::guarded_copy(&record, source.data + i, sizeof(record)) || !engine::record_key(record, key)) {
            return CopyResult::bad_record;
        }
        if (current.public_model != nullptr && public_reserved_key(key)) {
            ++sanitized;
            continue;
        }
        if (reserved_key(key)) {
            ++sanitized;
            continue;
        }
        current.records[kept++] = record;  // Borrowed native key bytes; no plugin free/dtor.
    }
    return CopyResult::ok;
}

/**
 * @brief Builds the substituted record vector of an own Apply.
 * @param current Dispatch frame.
 * @param snapshots Field texts.
 * @return `false` (usually logged) when the frame, vector or a record is invalid.
 */
inline bool prepare_transport(Origin& current, const InputSnapshot (&snapshots)[max_fields_per_model]) noexcept {
    if (current.model >= model_count) {
        bridge_rejected(current.id, "unknown-own-model");
        return false;
    }
    const auto context_address = reinterpret_cast<std::uintptr_t>(current.context);
    if (context_address > core::uptr_max - off::context_extent || current.epoch == 0 ||
        current.epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / fixed_point)) {
        bridge_rejected(current.id, "scope-or-fixed-point-epoch");
        return false;
    }
    if (current.model == country_model && !country_ready()) {
        bridge_rejected(current.id, "country-hooks-not-active");
        return false;
    }
    TempVector source{};
    std::size_t kept = 0;
    std::size_t sanitized = 0;
    if (const auto copied = copy_original_records(current, source, kept, sanitized); copied != CopyResult::ok) {
        bridge_rejected(current.id, copied == CopyResult::bad_vector ? "source-vector" : "source-record-or-key");
        return false;
    }
    const auto base_count = kept;
    const auto& model = models[current.model];
    for (std::size_t j = 0; j < model.count; ++j) {
        char32_t input[max_codepoints]{};
        for (std::size_t i = 0; i < snapshots[j].size; ++i) {
            input[i] = static_cast<char32_t>(snapshots[j].points[i]);
        }
        const auto encoded = encode_field(std::u32string_view(input, snapshots[j].size));
        FieldCodes field{};
        if (encoded.value) {
            field = *encoded.value;
        }
        const char* prefix = j == 0 ? field_a_prefix : field_b_prefix;
        char key[record_key_buffer]{};
        static_cast<void>(std::snprintf(key, sizeof(key), "%s_ok", prefix));
        if (!transport_record(current.records[kept++], key, encoded.value ? 1U : 0U)) {
            return false;
        }
        static_cast<void>(std::snprintf(key, sizeof(key), "%s_len", prefix));
        if (!transport_record(current.records[kept++], key, field.length)) {
            return false;
        }
        for (unsigned i = 0; i < code_slots; ++i) {
            // RN6: an invalid field has no codes; its last slot carries the InputError instead
            // (rn_native_err / rn_nativeb_err: 1 empty, 2 too long, 3..6 character). Budget unchanged.
            const bool error_slot = i == error_slot_index && !encoded.value;
            if (error_slot) {
                static_cast<void>(std::snprintf(key, sizeof(key), "%s_err", prefix));
            } else {
                static_cast<void>(std::snprintf(key, sizeof(key), "%s_%u", prefix, i));
            }
            const std::uint64_t value = error_slot ? static_cast<std::uint64_t>(encoded.error) : field.codes[i];
            if (!transport_record(current.records[kept++], key, value)) {
                return false;
            }
        }
        char line[transport_line_capacity]{};
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_IEBR id=%lu field=%zu prepared ok=%u len=%zu input_error=%u error_index=%zu; diagnostic transport\r\n",
            current.id, j + 1, encoded.value ? 1U : 0U, field.length, static_cast<unsigned>(encoded.error),
            encoded.error_index));
        write_log(line);
    }
    if (!transport_record(current.records[kept++], command_epoch_key, current.epoch)) {
        return false;
    }
    if (current.model == country_model && !transport_record(current.records[kept++], key_capability, 1)) {
        return false;
    }
    const auto transport_records = (model.count * records_per_field) + 1 + (current.model == country_model ? 1U : 0U);
    if (transport_records > max_transport_records || kept != base_count + transport_records) {
        return false;
    }
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    char line[transport_line_capacity]{};
    static_cast<void>(std::snprintf(
        line, sizeof(line),
        "RN_IEBR id=%lu model=%s "
        "epoch=%llu epoch_raw=%lld generation=%llu original=%d kept=%zu sanitized=%zu appended=%zu total=%d; "
        "diagnostic transport\r\n",
        current.id, model.name, static_cast<unsigned long long>(current.epoch),
        static_cast<long long>(current.epoch * fixed_point), static_cast<unsigned long long>(current.generation),
        source.count, base_count, sanitized, transport_records, current.view.count));
    write_log(line);
    return true;
}

/**
 * @brief Whether the constructor's names are exactly those of the dispatch frame.
 * @param current Dispatch frame.
 * @param gui Model name argument.
 * @param selector Selector argument.
 * @return `true` for the frame's own model and Apply selector (own or grammar editor).
 */
inline bool frame_names(const Origin& current, const RecordString* gui, const RecordString* selector) noexcept {
    if (current.public_model != nullptr) {
        return current.public_apply != nullptr && name_equals(gui, current.public_model) &&
               name_equals(selector, current.public_apply);
    }
    return current.model < model_count && name_equals(gui, models[current.model].name) &&
           name_equals(selector, models[current.model].apply);
}

/**
 * @brief Hook body of the effect constructor: substitutes the record view for our own dispatch only.
 * @param self Effect being constructed.
 * @param gui Model name argument.
 * @param selector Selector argument.
 * @param scope Scope argument.
 * @param source Record vector argument.
 * @return The original constructor's result, unchanged.
 */
#if defined(__APPLE__)
inline void constructor_bridged(void* self, const RecordString* gui, const RecordString* selector,
#else
inline void* __fastcall constructor_bridged(void* self, const RecordString* gui, const RecordString* selector,
#endif
                                const void* scope, const TempVector* source) noexcept {
    Origin* current = origin;
    if (!active.load(std::memory_order_acquire) || current == nullptr || gui != current->model_name ||
        selector != current->selector || scope != current->scope || source != current->source ||
        !frame_names(*current, gui, selector)) {
        return original_ctor(self, gui, selector, scope, source);
    }
    ++current->ctor_calls;
    // The native constructor deep-copies the borrowed POD view. Exactly one call, no
    // cancellation/retry or engine-pointer lifetime extension after it starts.
#if defined(__APPLE__)
    original_ctor(self, gui, selector, scope, &current->view);
    void* result = self;  // Native constructor is void; inspect the actual destination, never RAX.
#else
    void* result = original_ctor(self, gui, selector, scope, &current->view);
#endif
    TempVector copied{};
    const bool independent = member(result, off::effect_records, copied) && copied.count == current->view.count &&
                             copied.data != current->view.data &&
                             core::readable(copied.data, static_cast<std::size_t>(copied.count) * sizeof(Record));
    char line[short_line_capacity]{};
    static_cast<void>(
        std::snprintf(line, sizeof(line),
#if defined(__APPLE__)
                      "RN_IEBR id=%lu ctor_calls=%u inspected_self=%u independent_record_storage=%u records=%d\r\n",
#else
                      "RN_IEBR id=%lu ctor_calls=%u result_is_this=%u independent_record_storage=%u records=%d\r\n",
#endif
                      current->id, current->ctor_calls, result == self ? 1U : 0U, independent ? 1U : 0U, copied.count));
    write_log(line);
#if !defined(__APPLE__)
    return result;  // Preserve the native result even when post-ctor diagnostics fail.
#endif
}

/**
 * @brief Whether a payload-free sentinel may be forwarded (own editor window live).
 * @param context Window context.
 * @param model Model index.
 * @return `true` when the context belongs to a visible own window of that model.
 */
inline bool stale_eligible(void* context, std::size_t model) noexcept {
    View view{};
    return context_view(context, model, view);
}

/**
 * @brief Log name of a sentinel code.
 * @param code 1, 2 or 3.
 * @return "no-counter", "counter-limit" or "stale".
 */
inline const char* stale_reason(unsigned code) noexcept {
    if (code == static_cast<unsigned>(StaleCode::no_counter)) {
        return "no-counter";
    }
    if (code == static_cast<unsigned>(StaleCode::counter_limit)) {
        return "counter-limit";
    }
    return "stale";
}

/**
 * @brief Forwards an own Apply with only the original non-reserved records and `rn_ip_cmd_epoch = -code`.
 * @param current Dispatch frame.
 * @param opaque_selector Selector argument of Dispatch.
 * @param code Sentinel code (1 stale, 2 no counter, 3 counter limit; anything else becomes 1).
 * @return `false` when the vector could not be copied (the caller then drops and logs).
 */
inline bool dispatch_stale(Origin& current, void* opaque_selector, unsigned code) noexcept {
    const auto context_address = reinterpret_cast<std::uintptr_t>(current.context);
    if (code < static_cast<unsigned>(StaleCode::stale) || code > static_cast<unsigned>(StaleCode::counter_limit)) {
        code = static_cast<unsigned>(StaleCode::stale);
    }
    if (original_ctor == nullptr || current.model >= model_count ||
        context_address > core::uptr_max - off::context_extent) {
        return false;
    }
    TempVector source{};
    std::size_t kept = 0;
    std::size_t sanitized = 0;
    if (copy_original_records(current, source, kept, sanitized) != CopyResult::ok) {
        return false;
    }
    Record& epoch = current.records[kept++];
    epoch = {};
    if (!engine::inline_record_key(epoch, std::string_view(command_epoch_key, sizeof(command_epoch_key) - 1))) {
        return false;
    }
    epoch.raw_value = -static_cast<std::int64_t>(code) * fixed_point;
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    char line[short_line_capacity]{};
    static_cast<void>(std::snprintf(
        line, sizeof(line),
        "RN_IEBR id=%lu model=%s stale-sentinel epoch=-%u reason=%s kept=%zu sanitized=%zu; no payload\r\n", current.id,
        models[current.model].name, code, stale_reason(code), kept - 1, sanitized));
    write_log(line);
    {
        const OriginFrame frame(&current);
        original_dispatch(current.context, opaque_selector);
    }
    if (current.ctor_calls != 1) {
        bridge_rejected(current.id, "stale-sentinel-unexpected-ctor-count-no-retry");
    }
    return true;
}

}  // namespace capie::editor
