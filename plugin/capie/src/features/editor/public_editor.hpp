/**
 * @file public_editor.hpp
 * @brief Text editors of other mods: registry, opening, prefill and Apply transport (C1, C8).
 *
 * A mod that names its scripted GUI after the grammar in `public_names.hpp` gets the same
 * service Rename Anything's editors get: CAPIE copies the typed text of up to four edit
 * boxes into the temporary records of the player's own Apply command, and fills the boxes
 * once per opening from optional prefill carriers. The records and codes are those of
 * `plugin/capie/CONTRACT.md` (API level 2, extended alphabet).
 *
 * Registry. The first time a hook sees a model name that parses, the editor gets a slot
 * (at most 32 per session, 8 per mod id, first come first served). A slot holds only names
 * CAPIE built from the parsed ids; it is written once under `public_registry_lock` and
 * published by raising `public_editor_count`, so readers never lock and never see a
 * half-written slot. Slots are never freed. No engine pointer is stored.
 *
 * Opening (post-Tick). Same lifecycle as the own editors (`lifecycle.hpp`, shared helpers):
 * the window must pass the context/root chain check of `window_view.hpp`, the epoch
 * carrier must hold a valid epoch, a new epoch starts a new opening. The first visible Tick
 * of an opening finds the fields (1..4, consecutive, each an edit box; a fifth field or a
 * gap makes the layout invalid), writes each field that has a prefill carrier and no
 * keyboard focus, reads it back, and then marks the opening ready. A failed or skipped
 * prefill is logged and does not block Apply, because Apply reads every field afresh.
 *
 * Apply (Dispatch, before the original). A click whose model parses and whose selector is
 * exactly the built `..._apply_click` is a grammar Apply. CAPIE checks the visible window,
 * the epoch, readiness and the field layout, snapshots the fields, builds the records on
 * a copy of the click's vector (every incoming `capie_*` record removed), checks the
 * window, epoch, generation and texts once more, and runs the original dispatch exactly
 * once; the effect constructor hook substitutes the vector for this frame only
 * (`transport.hpp`). When the window is live but no payload can be built, the click is
 * forwarded once with `capie_cap`, `capie_ver` and `capie_epoch = -code` (`PublicSentinel`).
 * When the window is not live, the click is dropped and logged.
 *
 * What it does not do: no permission check (that is the script's job), no focus set or
 * release, no select-all, no typing cap, no Escape handling, no caption refresh. Typed text
 * never reaches the log.
 */
#pragma once
#include "lifecycle.hpp"
#include "prefill.hpp"
#include "public_names.hpp"
#include "public_schema.hpp"
#include "transport.hpp"
#include "window_view.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

namespace capie::editor {

/// @brief One registered grammar editor: every name CAPIE compares, built once from the ids.
struct PublicEditor {
    grammar::EditorId id{};                           ///< Parsed mod and editor ids.
    grammar::Name model{};                            ///< Scripted GUI (model) name.
    grammar::Name apply{};                            ///< Apply selector.
    grammar::Name epoch{};                            ///< Epoch carrier.
    grammar::Name fields[grammar::max_fields + 1]{};  ///< Edit boxes 1..5 (index 0 = field 1; 5 only detects overflow).
    grammar::Name prefills[grammar::max_fields]{};    ///< Prefill carriers 1..4.
};

/// @brief Field layout of an opening.
enum class Layout : unsigned {
    unknown,  ///< Not discovered yet.
    valid,    ///< 0..4 consecutive edit boxes.
    invalid,  ///< Gap, fifth field or a non-edit widget with a field name.
};

/// @brief Registry slots; slots below `public_editor_count` are immutable.
inline PublicEditor public_editors[max_public_editors]{};
/// @brief Number of published slots (release store after a slot is complete).
inline std::atomic<std::size_t> public_editor_count{0};
/// @brief Serializes registrations (never held across a game call).
inline SRWLOCK public_registry_lock = SRWLOCK_INIT;
/// @brief Set once the "registry full" line was logged.
inline std::atomic<bool> public_full_logged{false};
/// @brief Lifecycle per slot; guarded by `state_lock`.
inline Lifecycle public_lifecycle[max_public_editors]{};
/// @brief Field layout of the current opening per slot; guarded by `state_lock`.
inline Layout public_layout[max_public_editors]{};
/// @brief Field count of the current opening per slot (valid layouts); guarded by `state_lock`.
inline std::size_t public_field_count[max_public_editors]{};

/// @brief Size of a public log line buffer.
inline constexpr std::size_t public_line_capacity = 320;
/// @brief Size of the detail text of a public log line.
inline constexpr std::size_t public_detail_capacity = 64;
/// @brief Name buffer for a model name read from the game (longest grammar name + slack).
inline constexpr std::size_t public_name_buffer = grammar::name_capacity;

/// @brief Outcome of `register_public`.
enum class Registration {
    found,    ///< Already registered.
    added,    ///< Registered now.
    full,     ///< Session or per-mod limit reached.
    invalid,  ///< Not a grammar model name.
};

/**
 * @brief Writes one `CAPIE_ED` line (lifecycle channel; never typed text).
 * @param event Event name.
 * @param model Model name (or "?").
 * @param detail Extra detail (may be empty).
 * @param value Numeric detail.
 */
inline void public_log(const char* event, const char* model, const char* detail, unsigned long long value) noexcept {
    char line[public_line_capacity]{};
    const int count = std::snprintf(line, sizeof(line), "CAPIE_ED event=%s model=%s detail=%s value=%llu\r\n", event,
                                    model, detail, value);
    if (count > 0) {
        write_log(line, core::logging::stored_bytes(count, sizeof(line)));
    }
}

/**
 * @brief Finds a published slot by model name (no lock).
 * @param model Model name.
 * @return Slot index, or `max_public_editors` when absent.
 */
inline std::size_t find_public(std::string_view model) noexcept {
    const auto count = public_editor_count.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < count; ++i) {
        if (public_editors[i].model.view() == model) {
            return i;
        }
    }
    return max_public_editors;
}

/**
 * @brief Fills a slot with every name built from `id`.
 * @param slot Destination.
 * @param id Parsed ids.
 */
inline void fill_public(PublicEditor& slot, const grammar::EditorId& id) noexcept {
    slot.id = id;
    slot.model = grammar::model_name(id);
    slot.apply = grammar::apply_name(id);
    slot.epoch = grammar::epoch_name(id);
    for (std::size_t n = 0; n <= grammar::max_fields; ++n) {
        slot.fields[n] = grammar::field_name(id, n + 1);
    }
    for (std::size_t n = 0; n < grammar::max_fields; ++n) {
        slot.prefills[n] = grammar::prefill_name(id, n + 1);
    }
}

/**
 * @brief Finds or registers a grammar editor.
 * @param model Model name read from the game (explicit size).
 * @param index Receives the slot (`max_public_editors` unless found/added).
 * @return Outcome; `full` is logged once per session.
 */
inline Registration register_public(std::string_view model, std::size_t& index) noexcept {
    index = find_public(model);
    if (index != max_public_editors) {
        return Registration::found;
    }
    const auto id = grammar::parse_model(model);
    if (!id) {
        return Registration::invalid;
    }
    Registration result = Registration::full;
    AcquireSRWLockExclusive(&public_registry_lock);
    const auto count = public_editor_count.load(std::memory_order_relaxed);
    std::size_t same_mod = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (public_editors[i].model.view() == model) {
            index = i;
            result = Registration::found;
            break;
        }
        same_mod += public_editors[i].id.mod_view() == id->mod_view() ? 1U : 0U;
    }
    if (result != Registration::found && count < max_public_editors && same_mod < max_editors_per_mod) {
        fill_public(public_editors[count], *id);
        index = count;
        public_editor_count.store(count + 1, std::memory_order_release);
        result = Registration::added;
    }
    ReleaseSRWLockExclusive(&public_registry_lock);
    if (result == Registration::added) {
        public_log("registered", public_editors[index].model.c_str(), "", index);
    } else if (result == Registration::full && !public_full_logged.exchange(true)) {
        char name[public_name_buffer]{};
        model.copy(name, model.size() < sizeof(name) - 1 ? model.size() : sizeof(name) - 1);
        public_log("registry-full", name, "limit 32 per session, 8 per mod", count);
    }
    return result;
}

/**
 * @brief Reads the scripted-GUI model object and its name.
 * @param model Model object (from a window or a context).
 * @param name Receives the terminated name.
 * @param size Receives the name length.
 * @return `false` unless the object has the model vtable and a grammar-length name.
 */
inline bool model_name_bytes(void* model, char (&name)[public_name_buffer], std::size_t& size) noexcept {
    std::uintptr_t model_vt = 0;
    if (!member(model, 0, model_vt) || model_vt != image_base + rva::vt_sg_model) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(model);
    return address <= core::uptr_max - off::model_name &&
           engine::string_bytes(reinterpret_cast<void*>(address + off::model_name), name, sizeof(name) - 1, size) &&
           grammar::may_be_model(std::string_view(name, size));
}

/**
 * @brief Classifies a ticking or binding window as a grammar editor.
 * @param window Scripted window.
 * @param view Receives the slot (in `view.model`) and root.
 * @param registration Receives the registry outcome.
 * @return Chain result; `unrelated` when the model is not a registered or registrable grammar editor.
 */
inline ViewResult inspect_public(void* window, View& view, Registration& registration) noexcept {
    void* model = nullptr;
    char name[public_name_buffer]{};
    std::size_t size = 0;
    registration = Registration::invalid;
    if (!member(window, off::window_model, model) || !model_name_bytes(model, name, size)) {
        return ViewResult::unrelated;
    }
    std::size_t index = max_public_editors;
    registration = register_public(std::string_view(name, size), index);
    if (registration != Registration::found && registration != Registration::added) {
        return ViewResult::unrelated;
    }
    view.model = index;
    return inspect_chain(window, model, view);
}

/**
 * @brief Discovers the field layout of an opening.
 * @param root Root widget of a fresh visible view.
 * @param editor Registry slot.
 * @param count Receives the field count (valid layouts).
 * @return `valid` or `invalid`.
 */
inline Layout discover_fields(void* root, const PublicEditor& editor, std::size_t& count) noexcept {
    count = 0;
    bool gap = false;
    for (std::size_t n = 0; n <= grammar::max_fields; ++n) {
        const char* name = editor.fields[n].c_str();
        void* edit = guarded_find(find_edit, root, name);
        if (edit == nullptr) {
            gap = true;
            continue;
        }
        if (gap || n == grammar::max_fields || !edit_named(edit, name)) {
            return Layout::invalid;
        }
        ++count;
    }
    return Layout::valid;
}

/**
 * @brief Re-inspects a grammar window: same slot and root, visible, same epoch.
 * @param window Window.
 * @param view Original inspection.
 * @param epoch Opening epoch.
 * @param fresh Receives the fresh inspection.
 * @return `true` when unchanged.
 */
inline bool same_public_view(void* window, const View& view, std::uint64_t epoch, View& fresh) noexcept {
    Registration registration{};
    std::uint64_t fresh_epoch = 0;
    return inspect_public(window, fresh, registration) == ViewResult::visible && fresh.model == view.model &&
           fresh.root == view.root &&
           read_epoch_carrier(fresh.root, public_editors[view.model].epoch.c_str(), fresh_epoch) &&
           fresh_epoch == epoch;
}

/**
 * @brief Whether a grammar lifecycle still describes the same opening.
 * @param slot Registry slot.
 * @param epoch Opening epoch.
 * @param generation Opening generation.
 * @return `true` when visible, attempted and unchanged.
 */
inline bool same_public_opening(std::size_t slot, std::uint64_t epoch, std::uint64_t generation) noexcept {
    AcquireSRWLockShared(&state_lock);
    const auto& current = public_lifecycle[slot];
    const bool same =
        current.visible && current.attempted && current.epoch == epoch && current.generation == generation;
    ReleaseSRWLockShared(&state_lock);
    return same;
}

/// @brief Result of one field's prefill.
enum class PrefillStep {
    written,  ///< Written, refreshed and read back.
    skipped,  ///< No carrier, focused field or undecodable carrier: field left unchanged.
    failed,   ///< Write or verification failed: stop this opening's prefill.
};

/**
 * @brief Prefills one field of a grammar editor (see file comment).
 * @param window Window.
 * @param view Inspection of the opening.
 * @param epoch Opening epoch.
 * @param generation Opening generation.
 * @param field Field index 0..3.
 * @return Outcome.
 */
inline PrefillStep prefill_public_field(void* window, const View& view, std::uint64_t epoch, std::uint64_t generation,
                                        std::size_t field) noexcept {
    const auto& editor = public_editors[view.model];
    const char* name = editor.fields[field].c_str();
    Utf8Snapshot text{};
    if (!carrier(view.root, editor.prefills[field].c_str(), text, false) || !engine::decode_utf8(text)) {
        return PrefillStep::skipped;
    }
    void* edit = guarded_find(find_edit, view.root, name);
    bool focused = false;
    if (!edit_named(edit, name) || !visible_edit(edit) || !read_input_active(edit, focused)) {
        return PrefillStep::failed;
    }
    if (focused) {
        return PrefillStep::skipped;
    }
    const auto native = engine::borrowed_string(text.bytes, text.size);
    InputSnapshot check{};
    if (!guarded_set(edit, &native) || !snapshot_edit(edit, name, check) || !same_text(check, text.decoded)) {
        return PrefillStep::failed;
    }
    View fresh{};
    if (!same_public_view(window, view, epoch, fresh) || !same_public_opening(view.model, epoch, generation)) {
        return PrefillStep::failed;
    }
    void* again = guarded_find(find_edit, fresh.root, name);
    if (again != edit || !edit_named(again, name) || !read_input_active(again, focused) || focused ||
        !snapshot_edit(again, name, check) || !same_text(check, text.decoded) || !guarded_refresh(again)) {
        return PrefillStep::failed;
    }
    View rendered{};
    if (!same_public_view(window, view, epoch, rendered) || !same_public_opening(view.model, epoch, generation)) {
        return PrefillStep::failed;
    }
    void* last = guarded_find(find_edit, rendered.root, name);
    return last == edit && snapshot_edit(last, name, check) && same_text(check, text.decoded) ? PrefillStep::written
                                                                                              : PrefillStep::failed;
}

/**
 * @brief First visible Tick of an opening: field layout, prefill, readiness.
 * @param window Window.
 * @param view Inspection.
 * @param epoch Opening epoch.
 * @param generation Opening generation.
 */
inline void open_public(void* window, const View& view, std::uint64_t epoch, std::uint64_t generation) noexcept {
    const auto& editor = public_editors[view.model];
    std::size_t count = 0;
    const auto layout = discover_fields(view.root, editor, count);
    std::size_t written = 0;
    const char* outcome = "ready";
    if (layout == Layout::valid) {
        for (std::size_t field = 0; field < count; ++field) {
            const auto step = prefill_public_field(window, view, epoch, generation, field);
            if (step == PrefillStep::failed) {
                outcome = "prefill-failed-ready";  // Apply still reads every field afresh
                break;
            }
            written += step == PrefillStep::written ? 1U : 0U;
        }
    } else {
        outcome = "layout-invalid";
    }
    AcquireSRWLockExclusive(&state_lock);
    auto& state = public_lifecycle[view.model];
    const bool same = state.visible && state.epoch == epoch && state.generation == generation;
    if (same) {
        state.prefilled = true;
        public_layout[view.model] = layout;
        public_field_count[view.model] = count;
    }
    ReleaseSRWLockExclusive(&state_lock);
    char detail[public_detail_capacity]{};
    static_cast<void>(std::snprintf(detail, sizeof(detail), "fields=%zu,prefilled=%zu", count, written));
    public_log(same ? outcome : "opening-changed", editor.model.c_str(), detail, epoch);
}

/**
 * @brief Post-original Tick observer of grammar editors.
 * @param window Ticking window.
 */
inline void public_observe_tick(void* window) noexcept {
    View view{};
    Registration registration{};
    const auto result = inspect_public(window, view, registration);
    if (result == ViewResult::unrelated) {
        return;
    }
    if (result != ViewResult::visible) {
        AcquireSRWLockExclusive(&state_lock);
        end_opening(public_lifecycle[view.model], result == ViewResult::hidden);
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    std::uint64_t epoch = 0;
    if (!read_epoch_carrier(view.root, public_editors[view.model].epoch.c_str(), epoch)) {
        AcquireSRWLockExclusive(&state_lock);
        end_opening(public_lifecycle[view.model], false);
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    bool attempt = false;
    std::uint64_t generation = 0;
    AcquireSRWLockExclusive(&state_lock);
    if (see_visible(public_lifecycle[view.model], epoch, generation, attempt)) {
        public_layout[view.model] = Layout::unknown;
        public_field_count[view.model] = 0;
    }
    ReleaseSRWLockExclusive(&state_lock);
    if (attempt) {
        open_public(window, view, epoch, generation);
    }
}

/**
 * @brief Post-original Bind observer of grammar editors: full reset of the slot's lifecycle.
 * @param window Bound window.
 */
inline void public_bind(void* window) noexcept {
    View view{};
    Registration registration{};
    const auto result = inspect_public(window, view, registration);
    if (result == ViewResult::unrelated) {
        return;
    }
    AcquireSRWLockExclusive(&state_lock);
    if (result == ViewResult::invalid) {
        end_opening(public_lifecycle[view.model], false);
    } else {
        static_cast<void>(rebind_opening(public_lifecycle[view.model]));
    }
    public_layout[view.model] = Layout::unknown;
    public_field_count[view.model] = 0;
    ReleaseSRWLockExclusive(&state_lock);
}

/// @brief A grammar Apply recognised at Dispatch.
struct PublicCommit {
    void* model{};                         ///< Model object of the click's context.
    const MsvcString* model_name{};        ///< Model name inside the model object.
    char name[public_name_buffer]{};       ///< Copied model name.
    std::size_t size{};                    ///< Its length.
    std::size_t slot{max_public_editors};  ///< Registry slot (when registered).
    Registration registration{};           ///< Registry outcome.
};

/**
 * @brief Recognises a grammar Apply: model parses and the selector is its exact Apply name.
 * @param context Window context of the click.
 * @param selector Selector.
 * @param commit Receives the model and slot.
 * @return `true` for a grammar Apply (registered or not).
 */
inline bool public_commit(void* context, const MsvcString* selector, PublicCommit& commit) noexcept {
    std::uintptr_t context_vt = 0;
    if (!member(context, 0, context_vt) || context_vt != image_base + rva::vt_sg_context ||
        !member(context, off::context_model, commit.model) ||
        !model_name_bytes(commit.model, commit.name, commit.size)) {
        return false;
    }
    const auto id = grammar::parse_model(std::string_view(commit.name, commit.size));
    if (!id || !name_equals(selector, grammar::apply_name(*id).c_str())) {
        return false;
    }
    commit.model_name =
        reinterpret_cast<const MsvcString*>(reinterpret_cast<std::uintptr_t>(commit.model) + off::model_name);
    commit.registration = register_public(std::string_view(commit.name, commit.size), commit.slot);
    return true;
}

/**
 * @brief The visible window of a grammar Apply's context.
 * @param context Window context.
 * @param model Model object of the context.
 * @param view Receives the root.
 * @return `true` when the context is embedded in a visible, consistent window of that model.
 */
inline bool public_live(void* context, void* model, View& view) noexcept {
    void* window = nullptr;
    if (!member(context, off::context_window, window)) {
        return false;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(window);
    return address != 0 && address <= core::uptr_max - off::window_context &&
           reinterpret_cast<std::uintptr_t>(context) == address + off::window_context &&
           inspect_chain(window, model, view) == ViewResult::visible;
}

/// @brief Field texts of one grammar Apply.
struct PublicSnapshot {
    InputSnapshot texts[grammar::max_fields]{};  ///< Field texts.
    std::size_t count{};                         ///< Field count.
    std::uint64_t epoch{};                       ///< Opening epoch.
    std::uint64_t generation{};                  ///< Opening generation.
};

/**
 * @brief Reads the fields of a grammar Apply after the readiness checks.
 * @param root Fresh root.
 * @param slot Registry slot.
 * @param snapshot Field count in, texts out.
 * @return `false` when a field is missing, hidden or unreadable.
 */
inline bool read_public_fields(void* root, std::size_t slot, PublicSnapshot& snapshot) noexcept {
    for (std::size_t field = 0; field < snapshot.count; ++field) {
        const char* name = public_editors[slot].fields[field].c_str();
        void* edit = guarded_find(find_edit, root, name);
        if (!edit_named(edit, name) || !visible_edit(edit) || !snapshot_edit(edit, name, snapshot.texts[field])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Checks readiness and snapshots a grammar Apply.
 * @param commit Recognised commit (registered).
 * @param view Fresh live view.
 * @param snapshot Receives epoch, generation, count and texts.
 * @return 0 when a payload can be built, otherwise the sentinel code.
 */
inline unsigned capture_public(const PublicCommit& commit, const View& view, PublicSnapshot& snapshot) noexcept {
    if (!read_epoch_carrier(view.root, public_editors[commit.slot].epoch.c_str(), snapshot.epoch) ||
        !opening_ready(public_lifecycle[commit.slot], snapshot.epoch, snapshot.generation, false)) {
        return static_cast<unsigned>(PublicSentinel::stale);
    }
    AcquireSRWLockShared(&state_lock);
    const auto layout = public_layout[commit.slot];
    const auto expected = public_field_count[commit.slot];
    ReleaseSRWLockShared(&state_lock);
    if (layout == Layout::invalid) {
        return static_cast<unsigned>(PublicSentinel::layout_invalid);
    }
    std::size_t count = 0;
    if (layout != Layout::valid || discover_fields(view.root, public_editors[commit.slot], count) != Layout::valid ||
        count != expected) {
        return static_cast<unsigned>(PublicSentinel::stale);
    }
    snapshot.count = count;
    return read_public_fields(view.root, commit.slot, snapshot) ? 0U : static_cast<unsigned>(PublicSentinel::stale);
}

/**
 * @brief Appends one record (helper over `transport_record`).
 * @param current Dispatch frame.
 * @param kept In/out record count.
 * @param key Key.
 * @param value Unscaled value.
 * @return `false` when the record cannot be built.
 */
inline bool append_record(Origin& current, std::size_t& kept, const char* key, std::uint64_t value) noexcept {
    if (kept >= max_original_records + max_frame_records) {
        return false;
    }
    return transport_record(current.records[kept++], key, value);
}

/**
 * @brief Appends `capie_cap` and `capie_ver`.
 * @param current Dispatch frame.
 * @param kept In/out record count.
 * @return `false` on failure.
 */
inline bool append_public_identity(Origin& current, std::size_t& kept) noexcept {
    return append_record(current, kept, public_key_cap, public_api_level) &&
           append_record(current, kept, public_key_ver, public_build_number);
}

/**
 * @brief Appends the 35 records of one field.
 * @param current Dispatch frame.
 * @param kept In/out record count.
 * @param number Field number 1..4.
 * @param text Field text.
 * @return `false` on failure.
 */
inline bool append_public_field(Origin& current, std::size_t& kept, std::size_t number,
                                const InputSnapshot& text) noexcept {
    char32_t input[max_codepoints]{};
    for (std::size_t i = 0; i < text.size; ++i) {
        input[i] = static_cast<char32_t>(text.points[i]);
    }
    const auto encoded = encode_field(std::u32string_view(input, text.size), Alphabet::extended);
    FieldCodes codes{};
    if (encoded.value) {
        codes = *encoded.value;
    }
    const std::uint64_t at = encoded.value ? 0 : encoded.error_index + 1;
    char key[record_key_buffer]{};
    static_cast<void>(std::snprintf(key, sizeof(key), public_field_header_format, number, public_suffix_err));
    if (!append_record(current, kept, key, static_cast<std::uint64_t>(encoded.error))) {
        return false;
    }
    static_cast<void>(std::snprintf(key, sizeof(key), public_field_header_format, number, public_suffix_at));
    if (!append_record(current, kept, key, at)) {
        return false;
    }
    static_cast<void>(std::snprintf(key, sizeof(key), public_field_header_format, number, public_suffix_len));
    if (!append_record(current, kept, key, codes.length)) {
        return false;
    }
    for (unsigned slot = 0; slot < code_slots; ++slot) {
        static_cast<void>(std::snprintf(key, sizeof(key), public_field_slot_format, number, slot));
        if (!append_record(current, kept, key, codes.codes[slot])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Copies the click's records (reserved ones removed) and installs the frame's view.
 * @param current Dispatch frame (`public_model` set).
 * @param kept Receives the kept count.
 * @param source Receives the original vector header.
 * @return `false` when the vector or a record is malformed.
 */
inline bool copy_public_originals(Origin& current, std::size_t& kept, TempVector& source) noexcept {
    const auto context_address = reinterpret_cast<std::uintptr_t>(current.context);
    if (context_address > core::uptr_max - off::context_extent) {
        return false;
    }
    std::size_t sanitized = 0;
    return copy_original_records(current, source, kept, sanitized) == CopyResult::ok;
}

/**
 * @brief Builds the payload records of a grammar Apply.
 * @param current Dispatch frame.
 * @param snapshot Captured fields.
 * @return `false` (logged by the caller) when a record cannot be built.
 */
inline bool prepare_public(Origin& current, const PublicSnapshot& snapshot) noexcept {
    if (snapshot.epoch == 0 ||
        snapshot.epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / fixed_point)) {
        return false;
    }
    TempVector source{};
    std::size_t kept = 0;
    if (!copy_public_originals(current, kept, source)) {
        return false;
    }
    const auto base = kept;
    if (!append_public_identity(current, kept) || !append_record(current, kept, public_key_epoch, snapshot.epoch) ||
        !append_record(current, kept, public_key_fields, snapshot.count)) {
        return false;
    }
    for (std::size_t field = 0; field < snapshot.count; ++field) {
        if (!append_public_field(current, kept, field + 1, snapshot.texts[field])) {
            return false;
        }
    }
    if (kept - base != public_header_records + (snapshot.count * public_records_per_field)) {
        return false;
    }
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    return true;
}

/**
 * @brief Forwards a grammar Apply once with `capie_cap`, `capie_ver` and `capie_epoch = -code`.
 * @param current Dispatch frame (`public_model` set).
 * @param opaque_selector Selector argument of Dispatch.
 * @param code Sentinel code.
 * @return `false` when the vector could not be copied (the caller then drops and logs).
 */
inline bool dispatch_public_sentinel(Origin& current, void* opaque_selector, PublicSentinel code) noexcept {
    TempVector source{};
    std::size_t kept = 0;
    if (original_ctor == nullptr || !copy_public_originals(current, kept, source) ||
        !append_public_identity(current, kept) || kept >= max_original_records + max_frame_records) {
        return false;
    }
    Record& epoch = current.records[kept++];
    if (!transport_record(epoch, public_key_epoch, static_cast<unsigned>(code))) {
        return false;
    }
    epoch.raw_value = -epoch.raw_value;
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept),
                    source.allocator};
    public_log("sentinel", current.public_model, "epoch=-code", static_cast<unsigned>(code));
    {
        const OriginFrame frame(&current);
        original_dispatch(current.context, opaque_selector);
    }
    if (current.ctor_calls != 1) {
        bridge_rejected(current.id, "public-sentinel-unexpected-ctor-count-no-retry");
    }
    return true;
}

/**
 * @brief Final check right before the original dispatch: same window, epoch, generation, texts.
 * @param current Dispatch frame.
 * @param commit Recognised commit.
 * @param snapshot Captured fields.
 * @return `true` when nothing changed.
 */
inline bool public_still_current(const Origin& current, const PublicCommit& commit,
                                 const PublicSnapshot& snapshot) noexcept {
    View view{};
    std::uint64_t epoch = 0;
    std::uint64_t generation = snapshot.generation;
    if (!public_live(current.context, commit.model, view) ||
        !read_epoch_carrier(view.root, public_editors[commit.slot].epoch.c_str(), epoch) || epoch != snapshot.epoch ||
        !opening_ready(public_lifecycle[commit.slot], epoch, generation, true)) {
        return false;
    }
    PublicSnapshot fresh{};
    fresh.count = snapshot.count;
    if (!read_public_fields(view.root, commit.slot, fresh)) {
        return false;
    }
    for (std::size_t field = 0; field < snapshot.count; ++field) {
        if (!same_text(fresh.texts[field], snapshot.texts[field])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Dispatch handling of a grammar Apply.
 * @param context Window context.
 * @param opaque_selector Selector argument of Dispatch.
 * @return `false` when the click is not a grammar Apply (the caller forwards it unchanged);
 *         `true` when it was dispatched once, forwarded as a sentinel or dropped (logged).
 */
inline bool public_dispatch(void* context, void* opaque_selector) noexcept {
    const auto* selector = static_cast<const MsvcString*>(opaque_selector);
    PublicCommit commit{};
    if (!public_commit(context, selector, commit)) {
        return false;
    }
    Origin current{};
    current.context = context;
    current.model_name = commit.model_name;
    current.selector = selector;
    current.id = next_observation_id();
    View view{};
    const bool live = public_live(context, commit.model, view);
    const bool registered = commit.registration == Registration::found || commit.registration == Registration::added;
    if (registered) {
        current.public_model = public_editors[commit.slot].model.c_str();
        current.public_apply = public_editors[commit.slot].apply.c_str();
    } else {
        // Unregistered (registry full): names point at storage that lives for this call only.
        current.public_model = commit.name;
        current.public_apply = nullptr;
    }
    if (!live) {
        bridge_rejected(current.id, "public-editor-not-live-dropped");
        return true;
    }
    PublicSnapshot snapshot{};
    auto code = static_cast<unsigned>(PublicSentinel::registry_full);
    if (registered) {
        code = capture_public(commit, view, snapshot);
    }
    if (code != 0) {
        grammar::Name apply{};
        if (!registered) {
            const auto id = grammar::parse_model(std::string_view(commit.name, commit.size));
            apply = id ? grammar::apply_name(*id) : grammar::Name{};
            current.public_apply = apply.c_str();
        }
        if (!dispatch_public_sentinel(current, opaque_selector, static_cast<PublicSentinel>(code))) {
            bridge_rejected(current.id, "public-sentinel-dropped");
        }
        return true;
    }
    if (original_ctor == nullptr || !prepare_public(current, snapshot) ||
        !public_still_current(current, commit, snapshot)) {
        bridge_rejected(current.id, "public-apply-dropped-before-dispatch");
        return true;
    }
    public_log("apply", current.public_model, "fields", snapshot.count);
    {
        const OriginFrame frame(&current);
        original_dispatch(context, opaque_selector);
    }
    if (current.ctor_calls != 1) {
        bridge_rejected(current.id, "public-unexpected-ctor-count-no-retry");
    }
    return true;
}

}  // namespace capie::editor
