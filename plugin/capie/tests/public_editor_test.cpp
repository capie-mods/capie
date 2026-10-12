/**
 * @file public_editor_test.cpp
 * @brief Integration tests of grammar editors (C1, C8, C3) on the real hook bodies, in a fake image.
 *
 * Includes the `capie_editor.dll` composition root and drives its Tick, Bind, Dispatch and
 * effect-constructor hook bodies with fake windows, widgets and carriers laid out as the
 * pinned build lays them out (`core/game_layout.hpp`). Game calls (FindEdit,
 * FindInstantText, SetText, edit refresh, Dispatch, the effect constructor) are fakes. No
 * game process, no hooks installed. Runs in both flavors.
 *
 * Covered: registration of two mods, prefill from carriers (and not over a focused field),
 * payload records and codes, spoofed `capie_*` records removed, extended alphabet and
 * field errors, zero-field editors, the three sentinels, dropped clicks of hidden windows,
 * lookalike names forwarded unchanged, Bind reset, registry limits, and Rename Anything's
 * legacy frames left without `capie_*` handling.
 */
#include "../src/dll/capie_editor.cpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// NOLINTBEGIN(google-build-using-namespace): test of the composition root's internals
using namespace capie::editor;
using namespace capie::engine;
// NOLINTEND(google-build-using-namespace)
namespace rva = capie::core::rva;
namespace off = capie::core::off;

unsigned checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
unsigned failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param value Condition.
 * @param label Printed on failure.
 */
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label.c_str());
    }
}

/**
 * @brief Writes a value into fake memory.
 * @param object Base.
 * @param offset Offset.
 * @param value Value.
 */
template <class T>
void put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, static_cast<const void*>(&value), sizeof(value));
}

/// @brief Fake edit box size.
constexpr std::size_t edit_bytes = 0x280;
/// @brief Fake edit definition size.
constexpr std::size_t definition_bytes = 0x50;
/// @brief Fake instantTextBox size.
constexpr std::size_t carrier_bytes = 0x140;
/// @brief Fake window size.
constexpr std::size_t window_bytes = 0xC00;
/// @brief Fake model size.
constexpr std::size_t model_bytes = 0x100;
/// @brief UTF-32 capacity of a fake edit box.
constexpr std::size_t edit_text_capacity = 64;
/// @brief Fake image size (covers the focus manager global).
constexpr std::size_t image_bytes = 0x3454000;
/// @brief Root flag byte "shown".
constexpr unsigned char shown = 8;
/// @brief HOI4 fixed-point scale.
constexpr std::int64_t fp = 100000;

/**
 * @brief Builds an MSVC string over stable storage.
 * @param storage String that outlives every use.
 * @return The string header.
 */
MsvcString native(const std::string& storage) {
    return borrowed_string(storage.c_str(), storage.size());
}

/// @brief One fake edit box with its definition and text.
struct FakeEdit {
    alignas(8) std::array<unsigned char, edit_bytes> edit{};              ///< CEditBox.
    alignas(8) std::array<unsigned char, definition_bytes> definition{};  ///< Definition.
    std::array<std::uint32_t, edit_text_capacity> text{};                 ///< UTF-32 text.
    std::string name;                                                     ///< Definition name storage.
    bool present{};                                                       ///< Found by FindEdit.
};

/// @brief One fake instantTextBox carrier.
struct FakeCarrier {
    alignas(8) std::array<unsigned char, carrier_bytes> widget{};  ///< Widget.
    std::string name;                                              ///< Widget name.
    std::string text;                                              ///< Text storage.
    bool present{};                                                ///< Found by FindInstantText.
};

/// @brief One fake scripted window of a grammar editor.
struct FakeWindow {
    alignas(8) std::array<unsigned char, window_bytes> window{};           ///< Window (context at +0x110).
    alignas(8) std::array<unsigned char, off::root_readable_span> root{};  ///< Root widget.
    alignas(8) std::array<unsigned char, model_bytes> model{};             ///< Model.
    std::string model_name;                                                ///< Model name storage.
    std::array<FakeEdit, grammar::max_fields + 1> edits{};                 ///< Fields 1..5.
    std::array<FakeCarrier, grammar::max_fields> prefills{};               ///< Prefill carriers 1..4.
    FakeCarrier epoch{};                                                   ///< Epoch carrier.
    /// @brief Context address. @return window + 0x110.
    void* context() { return window.data() + off::window_context; }
};

std::vector<std::unique_ptr<FakeWindow>>
    windows;                      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake world
unsigned set_text_calls = 0;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): counter
unsigned dispatch_originals = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): counter
unsigned ctor_calls = 0;          // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): counter
std::array<unsigned char, 0x238>
    focus_manager{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): fake manager
alignas(8) std::array<unsigned char, 0x80> fake_effect{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// @brief One record seen by the fake constructor.
struct Captured {
    std::string key;   ///< Name.
    std::int64_t raw;  ///< Fixed-point value.
};
std::vector<Captured> captured;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): last constructor input

/**
 * @brief Sets the text of a fake edit box.
 * @param edit Edit.
 * @param text UTF-32 text.
 */
void type_text(FakeEdit& edit, std::u32string_view text) {
    edit.text.fill(0);
    for (std::size_t i = 0; i < text.size() && i < edit_text_capacity; ++i) {
        edit.text[i] = static_cast<std::uint32_t>(text[i]);
    }
    put(edit.edit.data(), off::edit_text, static_cast<void*>(edit.text.data()));
    put(edit.edit.data(), off::edit_size, static_cast<std::uint64_t>(text.size()));
    put(edit.edit.data(), off::edit_capacity, static_cast<std::uint64_t>(edit_text_capacity));
}

/**
 * @brief Reads the text of a fake edit box.
 * @param edit Edit.
 * @return UTF-32 text.
 */
std::u32string edit_text(const FakeEdit& edit) {
    std::uint64_t size = 0;
    std::memcpy(&size, edit.edit.data() + off::edit_size, sizeof(size));
    std::u32string out;
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(static_cast<char32_t>(edit.text[i]));
    }
    return out;
}

/**
 * @brief Sets the text of a carrier.
 * @param carrier Carrier.
 * @param text UTF-8 text.
 */
void set_carrier(FakeCarrier& carrier, const std::string& text) {
    carrier.text = text;
    carrier.present = true;
    put(carrier.widget.data(), 0, image_base + rva::vt_instant_text);
    put(carrier.widget.data(), off::instant_text, native(carrier.text));
}

/**
 * @brief Shows or hides a window (visible trigger and root flags).
 * @param w Window.
 * @param visible Visibility.
 */
void show(FakeWindow& w, bool visible) {
    put(w.root.data(), off::root_flags, static_cast<unsigned char>(visible ? shown : 0));
    put(w.window.data(), off::window_desired, static_cast<unsigned char>(visible ? 1 : 0));
}

/**
 * @brief Creates a fake grammar window.
 * @param model_name Scripted GUI name (need not be valid grammar).
 * @param field_mask Which of the fields 1..5 exist (bit n-1).
 * @param epoch Epoch carrier text ("" = no carrier).
 * @return The window (owned by `windows`).
 */
FakeWindow& make_window(const std::string& model_name, unsigned field_mask, const std::string& epoch) {
    windows.push_back(std::make_unique<FakeWindow>());
    auto& w = *windows.back();
    w.model_name = model_name;
    put(w.model.data(), 0, image_base + rva::vt_sg_model);
    put(w.model.data(), off::model_name, native(w.model_name));
    auto* win = w.window.data();
    put(win, off::window_model, static_cast<void*>(w.model.data()));
    put(win, off::window_root, static_cast<void*>(w.root.data()));
    put(win, off::window_context, image_base + rva::vt_sg_context);
    put(win, off::window_context + off::context_window, static_cast<void*>(win));
    put(win, off::window_context + off::context_model, static_cast<void*>(w.model.data()));
    put(win, off::window_context + off::context_root, static_cast<void*>(w.root.data()));
    put(w.root.data(), 0, image_base + rva::vt_sg_root);
    const auto id = grammar::parse_model(model_name);
    for (std::size_t n = 0; n <= grammar::max_fields; ++n) {
        auto& e = w.edits[n];
        e.present = (field_mask & (1U << n)) != 0;
        e.name = id ? std::string(grammar::field_name(*id, n + 1).view()) : std::string("unused");
        put(e.edit.data(), 0, image_base + rva::vt_edit);
        put(e.edit.data(), off::edit_buffer, image_base + rva::vt_text_buffer);
        put(e.edit.data(), off::edit_definition, static_cast<void*>(e.definition.data()));
        put(e.definition.data(), 0, image_base + rva::vt_edit_definition);
        put(e.definition.data(), off::definition_name, native(e.name));
        put(e.edit.data(), off::edit_visibility_flags, shown);
        type_text(e, U"");
    }
    for (std::size_t n = 0; n < grammar::max_fields; ++n) {
        w.prefills[n].name = id ? std::string(grammar::prefill_name(*id, n + 1).view()) : std::string("unused");
    }
    w.epoch.name = id ? std::string(grammar::epoch_name(*id).view()) : std::string("unused");
    if (!epoch.empty()) {
        set_carrier(w.epoch, epoch);
    }
    show(w, true);
    return w;
}

/**
 * @brief Finds the fake window of a root.
 * @param root Root.
 * @return Window or null.
 */
FakeWindow* window_of(void* root) {
    for (auto& w : windows) {
        if (w->root.data() == root) {
            return w.get();
        }
    }
    return nullptr;
}

/// @brief Fake FindEdit.
void* __fastcall fake_find_edit(void* root, const MsvcString* name, bool /*a*/, bool /*b*/) {
    auto* w = window_of(root);
    if (w == nullptr) {
        return nullptr;
    }
    for (auto& e : w->edits) {
        if (e.present && name_equals(name, e.name.c_str())) {
            return e.edit.data();
        }
    }
    return nullptr;
}

/// @brief Fake FindInstantText.
void* __fastcall fake_find_instant(void* root, const MsvcString* name, bool /*a*/, bool /*b*/) {
    auto* w = window_of(root);
    if (w == nullptr) {
        return nullptr;
    }
    if (w->epoch.present && name_equals(name, w->epoch.name.c_str())) {
        return w->epoch.widget.data();
    }
    for (auto& c : w->prefills) {
        if (c.present && name_equals(name, c.name.c_str())) {
            return c.widget.data();
        }
    }
    return nullptr;
}

/// @brief Fake text-buffer SetText: decodes UTF-8 into the owning edit.
void __fastcall fake_set_text(void* buffer, const MsvcString* text) {
    ++set_text_calls;
    auto* edit = static_cast<unsigned char*>(buffer) - off::edit_buffer;
    for (auto& w : windows) {
        for (auto& e : w->edits) {
            if (e.edit.data() == edit) {
                const char* bytes = text->capacity <= sso_capacity ? reinterpret_cast<const char*>(text->data.small)
                                                                   : text->data.pointer;
                std::array<wchar_t, edit_text_capacity> wide{};
                const int count = MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(text->size), wide.data(),
                                                      static_cast<int>(wide.size()));
                type_text(e, std::u32string(wide.begin(), wide.begin() + count));
            }
        }
    }
}

/// @brief Fake edit refresh.
void __fastcall fake_refresh(void* /*edit*/) {}
/// @brief Fake Tick original.
void __fastcall fake_tick(void* /*window*/, char /*force*/) {}
/// @brief Fake Bind original.
void __fastcall fake_bind(void* /*window*/) {}

/// @brief Fake effect constructor: records the vector it receives.
void* __fastcall fake_ctor(void* self, const MsvcString* /*gui*/, const MsvcString* /*selector*/, const void* /*scope*/,
                           const TempVector* view) {
    ++ctor_calls;
    captured.clear();
    for (int i = 0; i < view->count; ++i) {
        char key[max_name_bytes + 1]{};
        static_cast<void>(record_key(view->data[i], key));
        captured.push_back({key, view->data[i].raw_value});
    }
    return self;
}

/// @brief Fake Dispatch: builds the effect through the bridged constructor, as the game does.
void __fastcall fake_dispatch(void* context, void* opaque_selector) {
    ++dispatch_originals;
    const auto address = reinterpret_cast<std::uintptr_t>(context);
    void* model = nullptr;
    std::memcpy(static_cast<void*>(&model), reinterpret_cast<void*>(address + off::context_model), sizeof(model));
    const auto* gui = reinterpret_cast<const MsvcString*>(reinterpret_cast<std::uintptr_t>(model) + off::model_name);
    constructor_bridged(fake_effect.data(), gui, static_cast<const MsvcString*>(opaque_selector),
                        reinterpret_cast<const void*>(address + off::context_scope),
                        reinterpret_cast<const TempVector*>(address + off::context_records));
}

/**
 * @brief Number of captured records with a key.
 * @param key Key.
 * @return Count.
 */
std::size_t count_of(const char* key) {
    std::size_t n = 0;
    for (const auto& r : captured) {
        n += r.key == key ? 1U : 0U;
    }
    return n;
}

/**
 * @brief Unscaled value of a captured record.
 * @param key Key.
 * @return Value, or -999999 when absent.
 */
std::int64_t value_of(const std::string& key) {
    for (const auto& r : captured) {
        if (r.key == key) {
            return r.raw / fp;
        }
    }
    return -999999;  // NOLINT(readability-magic-numbers): "absent" marker
}

/**
 * @brief Number of captured `capie_*` records.
 * @return Count.
 */
std::size_t capie_records() {
    std::size_t n = 0;
    for (const auto& r : captured) {
        n += r.key.starts_with("capie_") ? 1U : 0U;
    }
    return n;
}

/**
 * @brief Builds a source record.
 * @param key Key (<= 15 bytes).
 * @param value Unscaled value.
 * @return Record.
 */
Record source_record(const char* key, std::int64_t value) {
    Record r{};
    r.key = borrowed_string(key, std::strlen(key));
    r.raw_value = value * fp;
    return r;
}

/**
 * @brief Clicks the Apply button of a window.
 * @param w Window.
 * @param selector Selector storage.
 */
void click(FakeWindow& w, const std::string& selector) {
    captured.clear();
    auto name = native(selector);
    dispatch_observed(w.context(), &name);
}

/**
 * @brief Apply selector of a grammar window.
 * @param w Window.
 * @return Selector text.
 */
std::string apply_of(const FakeWindow& w) {
    const auto id = grammar::parse_model(w.model_name);
    return id ? std::string(grammar::apply_name(*id).view()) : std::string();
}

/// @brief Registration, prefill and payload of one editor, with spoofed records.
void payload_tests() {
    auto& faction = make_window("capie_v1_ed__mymod__faction", 0b1, "3");
    set_carrier(faction.prefills[0], "Allies");
    const auto calls = set_text_calls;
    tick_observed(faction.window.data(), 0);
    check(public_editor_count.load() == 1 && public_editors[0].model.view() == "capie_v1_ed__mymod__faction",
          "first grammar editor registered");
    check(set_text_calls == calls + 1 && edit_text(faction.edits[0]) == U"Allies", "prefill wrote the carrier text");
    check(public_lifecycle[0].prefilled && public_layout[0] == Layout::valid && public_field_count[0] == 1,
          "opening ready with one field");
    for (int i = 0; i < 5; ++i) {
        tick_observed(faction.window.data(), 0);
    }
    check(set_text_calls == calls + 1, "prefill runs once per opening");

    type_text(faction.edits[0], U"Entente Cordiale");
    std::array<Record, 5> spoof{source_record("user_key", 7), source_record("capie_cap", 99),
                                source_record("capie_f1_0", 5), source_record("capie_epoch", 42),
                                source_record("capie_x", 1)};
    put(faction.context(), off::context_records, TempVector{spoof.data(), 5, 5, nullptr});
    const auto before = dispatch_originals;
    click(faction, apply_of(faction));
    check(dispatch_originals == before + 1 && !captured.empty(), "Apply dispatched once");
    check(count_of("user_key") == 1 && value_of("user_key") == 7, "non-reserved original kept");
    check(count_of("capie_cap") == 1 && value_of("capie_cap") == 2, "capie_cap = 2 (spoof removed)");
    check(count_of("capie_ver") == 1 && value_of("capie_ver") == 10100, "capie_ver = 10100");
    check(count_of("capie_epoch") == 1 && value_of("capie_epoch") == 3, "capie_epoch from the carrier");
    check(count_of("capie_x") == 0, "every capie_ record from the source removed");
    check(value_of("capie_nf") == 1 && value_of("capie_f1_err") == 0 && value_of("capie_f1_at") == 0 &&
              value_of("capie_f1_len") == 16,
          "field header");
    check(value_of("capie_f1_0") == 31 && value_of("capie_f1_7") == 63 && value_of("capie_f1_15") == 5 &&
              value_of("capie_f1_16") == 0 && value_of("capie_f1_31") == 0 && count_of("capie_f1_0") == 1,
          "field codes (E, space, e, padding)");
    check(captured.size() == 1 + public_header_records + public_records_per_field && capie_records() == 39,
          "1 original + 4 header + 35 field records");

    // Extended alphabet and a rejected field.
    type_text(faction.edits[0], U"Žižkov");
    click(faction, apply_of(faction));
    check(value_of("capie_f1_err") == 0 && value_of("capie_f1_len") == 6 && value_of("capie_f1_0") > 84,
          "Latin Extended-A accepted");
    type_text(faction.edits[0], U"Москва");
    click(faction, apply_of(faction));
    check(value_of("capie_f1_err") == 6 && value_of("capie_f1_at") == 1 && value_of("capie_f1_len") == 0 &&
              value_of("capie_f1_0") == 0,
          "Cyrillic: err 6 at position 1, no codes");
    type_text(faction.edits[0], U"");
    click(faction, apply_of(faction));
    check(value_of("capie_f1_err") == 1 && value_of("capie_epoch") == 3, "empty field still carries the header");
    put(faction.context(), off::context_records, TempVector{});

    // Focused field: no prefill over a draft; the opening is still ready.
    set_carrier(faction.epoch, "4");
    type_text(faction.edits[0], U"draft");
    put(reinterpret_cast<void*>(image_base), rva::manager_global, static_cast<void*>(focus_manager.data()));
    put(focus_manager.data(), off::manager_receiver,
        static_cast<void*>(faction.edits[0].edit.data() + off::edit_receiver));
    put(faction.edits[0].edit.data(), off::edit_flags, static_cast<unsigned char>(1));
    const auto focused_calls = set_text_calls;
    tick_observed(faction.window.data(), 0);
    check(set_text_calls == focused_calls && edit_text(faction.edits[0]) == U"draft", "focused draft preserved");
    check(public_lifecycle[0].prefilled && public_lifecycle[0].epoch == 4, "opening ready after a skipped prefill");
    put(focus_manager.data(), off::manager_receiver, static_cast<void*>(nullptr));
    put(faction.edits[0].edit.data(), off::edit_flags, static_cast<unsigned char>(0));
    click(faction, apply_of(faction));
    check(value_of("capie_epoch") == 4 && value_of("capie_f1_len") == 5, "Apply of the new opening");
}

/// @brief A second mod, zero-field editors and legacy frames.
void second_mod_tests() {
    auto& motto = make_window("capie_v1_ed__othermod__motto", 0b11, "1");
    const auto calls = set_text_calls;
    tick_observed(motto.window.data(), 0);
    check(public_editor_count.load() == 2 && set_text_calls == calls, "second mod registered, no carriers no prefill");
    type_text(motto.edits[0], U"Liberté");
    type_text(motto.edits[1], U"Égalité, fraternité!");
    click(motto, apply_of(motto));
    check(value_of("capie_nf") == 2 && value_of("capie_f1_len") == 7 && value_of("capie_f2_len") == 20 &&
              value_of("capie_f2_err") == 0 && capie_records() == 4 + (2 * 35),
          "two fields, 74 records");

    auto& hello = make_window("capie_v1_ed__capie__hello", 0, "1");
    tick_observed(hello.window.data(), 0);
    click(hello, apply_of(hello));
    check(value_of("capie_nf") == 0 && capie_records() == 4 && value_of("capie_cap") == 2, "zero-field editor");

    // A legacy (Rename Anything) frame keeps capie_ records it did not reserve.
    std::array<Record, 2> legacy_source{source_record("capie_cap", 1), source_record("rn_native_ok", 1)};
    put(motto.context(), off::context_records, TempVector{legacy_source.data(), 2, 2, nullptr});
    Origin frame{};
    frame.context = motto.context();
    frame.model = model_index::leader;
    TempVector source{};
    std::size_t kept = 0;
    std::size_t sanitized = 0;
    check(copy_original_records(frame, source, kept, sanitized) == CopyResult::ok && kept == 1 && sanitized == 1 &&
              frame.records[0].raw_value == fp,
          "legacy frame: only legacy reserved keys removed");
    put(motto.context(), off::context_records, TempVector{});
}

/// @brief Sentinels, drops, lookalikes and Bind.
void sentinel_tests() {
    // Apply before the first Tick of the window: registered at Dispatch, not ready -> -1.
    auto& late = make_window("capie_v1_ed__mymod__late", 0b1, "5");
    click(late, apply_of(late));
    check(value_of("capie_epoch") == -1 && value_of("capie_cap") == 2 && value_of("capie_ver") == 10100 &&
              count_of("capie_nf") == 0 && capie_records() == 3,
          "stale sentinel -1 with cap and ver only");

    // Field gap and a fifth field: layout invalid -> -3.
    auto& gap = make_window("capie_v1_ed__mymod__gap", 0b101, "1");
    tick_observed(gap.window.data(), 0);
    click(gap, apply_of(gap));
    check(value_of("capie_epoch") == -3 && capie_records() == 3, "gap -> layout sentinel -3");
    auto& five = make_window("capie_v1_ed__mymod__five", 0b11111, "1");
    tick_observed(five.window.data(), 0);
    click(five, apply_of(five));
    check(value_of("capie_epoch") == -3, "fifth field -> layout sentinel -3");

    // Missing epoch carrier: never ready -> -1.
    auto& noepoch = make_window("capie_v1_ed__mymod__noepoch", 0b1, "");
    tick_observed(noepoch.window.data(), 0);
    click(noepoch, apply_of(noepoch));
    check(value_of("capie_epoch") == -1, "missing epoch carrier -> -1");

    // Hidden window: dropped, the game's effect does not run.
    auto& hidden = make_window("capie_v1_ed__mymod__hidden", 0b1, "1");
    tick_observed(hidden.window.data(), 0);
    show(hidden, false);
    const auto before = dispatch_originals;
    click(hidden, apply_of(hidden));
    check(dispatch_originals == before && captured.empty(), "hidden editor: click dropped");

    // Lookalikes are not editors: forwarded unchanged, no records added.
    std::array<Record, 1> plain{source_record("user_key", 1)};
    for (const char* name :
         {"capie_v1_ed__MyMod__x", "capie_v1_ed__a__b__c", "capie_v1_ed__abcdefghijklm__x", "capie_v2_ed__a__b"}) {
        auto& fake = make_window(name, 0b1, "1");
        put(fake.context(), off::context_records, TempVector{plain.data(), 1, 1, nullptr});
        const auto count = public_editor_count.load();
        tick_observed(fake.window.data(), 0);
        const auto forwarded = dispatch_originals;
        click(fake, std::string(name) + "_apply_click");
        check(public_editor_count.load() == count && dispatch_originals == forwarded + 1 && captured.size() == 1 &&
                  capie_records() == 0,
              std::string("lookalike forwarded unchanged: ") + name);
    }
    // A valid editor's other buttons are not annotated.
    auto& faction = *windows.front();
    put(faction.context(), off::context_records, TempVector{plain.data(), 1, 1, nullptr});
    click(faction, "capie_v1_ed__mymod__faction_cancel_click");
    check(captured.size() == 1 && capie_records() == 0, "Cancel of a grammar editor forwarded unchanged");
    put(faction.context(), off::context_records, TempVector{});

    // Bind: full reset, next Apply is stale until the window ticks again.
    bind_observed(faction.window.data());
    click(faction, apply_of(faction));
    check(value_of("capie_epoch") == -1, "after Bind: stale until the next Tick");
    const auto calls = set_text_calls;
    set_carrier(faction.prefills[0], "Axis");
    tick_observed(faction.window.data(), 0);
    check(set_text_calls == calls + 1 && edit_text(faction.edits[0]) == U"Axis", "after Bind: new opening prefills");
    click(faction, apply_of(faction));
    check(value_of("capie_epoch") == 4 && value_of("capie_f1_len") == 4, "after Bind and Tick: payload");

    // The constructor substitutes nothing outside a dispatch frame.
    std::array<Record, 1> outside{source_record("capie_cap", 5)};
    TempVector vector{outside.data(), 1, 1, nullptr};
    auto gui = native(faction.model_name);
    const std::string apply = apply_of(faction);
    auto selector = native(apply);
    captured.clear();
    constructor_bridged(fake_effect.data(), &gui, &selector, nullptr, &vector);
    check(captured.size() == 1 && value_of("capie_cap") == 5, "no frame: constructor unchanged");
}

/// @brief Registry limits: 8 per mod, 32 per session, then sentinel -2.
void registry_tests() {
    // mymod already has faction, late, gap, five, noepoch, hidden (6) -> two more fit.
    for (const char* name : {"capie_v1_ed__mymod__extra1", "capie_v1_ed__mymod__extra2"}) {
        auto& w = make_window(name, 0b1, "1");
        tick_observed(w.window.data(), 0);
    }
    const auto count = public_editor_count.load();
    auto& ninth = make_window("capie_v1_ed__mymod__ninth", 0b1, "1");
    tick_observed(ninth.window.data(), 0);
    check(public_editor_count.load() == count, "ninth editor of one mod refused");
    click(ninth, apply_of(ninth));
    check(value_of("capie_epoch") == -2 && capie_records() == 3, "per-mod limit -> sentinel -2");
    for (unsigned i = 0; public_editor_count.load() < max_public_editors && i < 2 * max_public_editors; ++i) {
        auto& w = make_window("capie_v1_ed__filler" + std::to_string(i / max_editors_per_mod) + "__e" +
                                  std::to_string(i % max_editors_per_mod),
                              0b1, "1");
        tick_observed(w.window.data(), 0);
    }
    auto& last = make_window("capie_v1_ed__newmod__x", 0b1, "1");
    tick_observed(last.window.data(), 0);
    check(public_editor_count.load() == max_public_editors && find_public(last.model_name) == max_public_editors,
          "33rd editor refused");
    click(last, apply_of(last));
    check(value_of("capie_epoch") == -2, "session limit -> sentinel -2");
    auto& first = *windows.front();
    click(first, apply_of(first));
    check(value_of("capie_epoch") == 4, "registered editors keep working when the registry is full");
    FakeWindow* other_major = nullptr;
    for (auto& w : windows) {
        other_major = w->model_name == "capie_v2_ed__a__b" ? w.get() : other_major;
    }
    check(other_major != nullptr && window_may_be_own(last.window.data()) &&
              !window_may_be_own(other_major->window.data()),
          "pre-filter: grammar prefix passes, capie_v2_ does not");
}

}  // namespace

int main() {
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    void* memory = VirtualAlloc(nullptr, image_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == nullptr) {
        std::printf("public editor tests: 1 checks, 1 failures\n");
        return 1;
    }
    image_base = reinterpret_cast<std::uintptr_t>(memory);
    capie::core::logging::log_file =
        CreateFileW(L"public_editor_test.log", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    put(memory, rva::manager_global, static_cast<void*>(focus_manager.data()));
    find_edit = fake_find_edit;
    find_instant = fake_find_instant;
    set_text = fake_set_text;
    refresh_edit = fake_refresh;
    original_tick = fake_tick;
    original_bind = fake_bind;
    original_dispatch = fake_dispatch;
    original_ctor = fake_ctor;
    active.store(true);
    payload_tests();
    second_mod_tests();
    sentinel_tests();
    registry_tests();
    std::printf("public editor tests: %u checks, %u failures\n", checks, failures);
    static_cast<void>(std::fflush(stdout));
    std::_Exit(failures == 0 ? 0 : 1);
}
