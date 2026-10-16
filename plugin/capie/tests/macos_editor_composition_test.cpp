/** @file macos_editor_composition_test.cpp @brief Offline release composition with scoped native-call admission. */
#include "../src/dll/macos_editor.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
namespace native = capie::editor::macos;
namespace ed = capie::editor;
namespace en = capie::engine;
unsigned checks = 0, failures = 0, ticks = 0, binds = 0, dispatches = 0, finds = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
void* expected_window = nullptr;
void* expected_context = nullptr;
void* expected_selector = reinterpret_cast<void*>(3);
void* edit_widget = nullptr;
void* epoch_widget = nullptr;
bool apply_mode = false;
bool nested_tick = false;
unsigned constructed = 0;
std::array<en::Record, 100> copied{};
void* model_name_address = nullptr;
std::array<unsigned char, 0x88> command{};
void tick(void* window, char force) {
    ++ticks;
    check(window == expected_window && force == 7, "tick exact arguments");
    check(en::native_call_thread.load() == 0, "original tick runs without admission");
}
void bind(void* window) {
    ++binds;
    check(window == expected_window, "bind exact argument");
    check(en::native_call_thread.load() == 0, "original bind has no admission");
}
void dispatch(void* context, void* selector) {
    ++dispatches;
    check(context == expected_context && selector == expected_selector, "dispatch exact arguments");
    check(en::native_call_thread.load() == 0, "original dispatch has no native admission");
    if (apply_mode) {
        auto address = reinterpret_cast<std::uintptr_t>(context);
        native::construct(command.data(), static_cast<const en::RecordString*>(model_name_address),
                          static_cast<const en::RecordString*>(selector),
                          reinterpret_cast<void*>(address + capie::core::off::context_scope),
                          reinterpret_cast<const en::TempVector*>(address + capie::core::off::context_records));
    }
}
void ctor(void* self, const en::RecordString*, const en::RecordString*, const void*, const en::TempVector* records) {
    ++constructed;
    check(en::native_call_thread.load() == 0, "constructor original admission suspended");
    std::memcpy(copied.data(), records->data, static_cast<std::size_t>(records->count) * sizeof(en::Record));
    en::TempVector view{copied.data(), records->count, records->count, records->allocator};
    std::memcpy(static_cast<unsigned char*>(self) + capie::core::off::effect_records, &view, sizeof(view));
}
void* find(void*, const en::EngineString* name, bool) {
    ++finds;
    check(en::native_call_thread.load() == capie::core::platform::thread_id(),
          "lookup admitted only inside verified scope");
    if (nested_tick) {
        nested_tick = false;
        native::tick(expected_window, 7);
        check(en::native_call_thread.load() == capie::core::platform::thread_id(),
              "nested original restores outer observer scope");
    }
    if (apply_mode) {
        if (en::name_equals(name, ed::fields[0].edit))
            return edit_widget;
        if (en::name_equals(name, ed::models[0].epoch))
            return epoch_widget;
    }
    return nullptr;
}
void set(void*, const en::EngineString*) {}
void refresh(void*) {}
void caption(void*, int) {}
void focus(void*, void*) {}
template <class T, std::size_t N>
void put(std::array<unsigned char, N>& object, std::size_t at, T value) {
    std::memcpy(object.data() + at, &value, sizeof(value));
}
int main() {
    native::Configuration config{};
    check(!native::configure(config), "incomplete configuration rejected");
    config.image = 0x100000000;
    config.ui_thread = capie::core::platform::thread_id();
    config.tick = tick;
    config.bind = bind;
    config.dispatch = dispatch;
    config.constructor = ctor;
    config.find_edit = find;
    config.find_instant = find;
    config.set_text = set;
    config.refresh_edit = refresh;
    config.caption_request = caption;
    config.focus_switch = focus;
    check(native::configure(config), "explicit original callbacks configured inactive");
    expected_window = nullptr;
    native::tick(nullptr, 7);
    native::bind(nullptr);
    native::dispatch(nullptr, reinterpret_cast<void*>(3));
    check(ticks == 1 && binds == 1 && dispatches == 1 && finds == 0, "inactive exact forwarding");
    check(!native::activate(false), "unverified post-tick lifetime refused");
    std::array<unsigned char, 0xc00> window{};
    std::array<unsigned char, 0x200> model{};
    std::array<unsigned char, 0xeb0> root{};
    std::array<unsigned char, 0x300> dummy_owner{};
    namespace off = capie::core::off;
    namespace rva = capie::core::rva;
    put(window, off::window_model, model.data());
    put(model, 0, config.image + rva::vt_sg_model);
    const auto name = en::borrowed_string(ed::models[0].name, std::strlen(ed::models[0].name));
    put(model, off::model_name, name);
    put(window, off::window_root, root.data());
    put(window, off::window_context, config.image + rva::vt_sg_context);
    put(window, off::window_context + off::context_window, window.data());
    put(window, off::window_context + off::context_model, model.data());
    put(window, off::window_context + off::context_root, root.data());
    put(root, 0, config.image + rva::vt_sg_root);
    put(root, off::root_dummy_owner, dummy_owner.data());
    root[off::root_flags] = capie::core::flag::shown;
    window[off::window_desired] = 1;
    check(native::activate(true), "fixture lifetime explicitly admitted");
    expected_window = window.data();
    nested_tick = true;
    check(!native::configure(config), "active composition cannot be rebound");
    native::tick(window.data(), 7);
    check(finds > 0 && en::native_call_thread.load() == 0, "observer scope closes native admission");
    check(!ed::lifecycle[0].prefilled, "missing epoch keeps lifecycle unready");
    const auto before = finds;
    std::thread foreign([&] { native::tick(window.data(), 7); });
    foreign.join();
    check(finds == before && en::native_call_thread.load() == 0, "foreign tick forwards without observers");
    std::array<unsigned char, 0x400> edit{};
    std::array<unsigned char, 0x100> definition{};
    std::array<unsigned char, 0x300> epoch{};
    put(edit, 0, config.image + rva::vt_edit);
    put(edit, off::edit_buffer, config.image + rva::vt_text_buffer);
    put(edit, off::edit_definition, definition.data());
    put(definition, 0, config.image + rva::vt_edit_definition);
    const auto edit_name = en::borrowed_string(ed::fields[0].edit, std::strlen(ed::fields[0].edit));
    put(definition, off::definition_name, edit_name);
    edit[off::edit_visibility_flags] = capie::core::flag::shown;
    edit[off::edit_text] = 2;
    const char32_t character = U'A';
    put(edit, off::edit_text + 4, character);
    put(epoch, 0, config.image + rva::vt_instant_text);
    put(epoch, off::instant_text, en::borrowed_string("1", 1));
    edit_widget = edit.data();
    epoch_widget = epoch.data();
    void* edit_ptr = edit.data();
    put(root, 0xe80, &edit_ptr);
    put<std::int32_t>(root, 0xe88, 1);
    put<std::int32_t>(root, 0xe8c, 1);
    put(root, 0xe98, &edit_name);
    put<std::int32_t>(root, 0xea0, 1);
    put<std::int32_t>(root, 0xea4, 1);
    en::Record original_record{};
    en::inline_record_key(original_record, "plain");
    en::TempVector source{&original_record, 1, 1, nullptr};
    put(window, off::window_context + off::context_records, source);
    ed::lifecycle[0].visible = true;
    ed::lifecycle[0].attempted = true;
    ed::lifecycle[0].prefilled = true;
    ed::lifecycle[0].epoch = 1;
    ed::lifecycle[0].generation = 5;
    auto selector = en::borrowed_string(ed::models[0].apply, std::strlen(ed::models[0].apply));
    expected_context = window.data() + off::window_context;
    expected_selector = &selector;
    model_name_address = model.data() + off::model_name;
    apply_mode = true;
    const auto dispatch_before = dispatches;
    native::dispatch(expected_context, &selector);
    check(dispatches == dispatch_before + 1 && constructed == 1 && en::native_call_thread.load() == 0,
          "complete own Apply forwards and constructs exactly once");
    bool payload = false;
    for (const auto& record : copied) {
        char key[129]{};
        if (en::record_key(record, key) && std::string_view(key) == "rn_native_ok" && record.raw_value == 100000)
            payload = true;
    }
    check(payload, "shared release Apply carries native payload");
    root[off::root_flags] = capie::core::flag::hidden_a;
    const auto denied_dispatches = dispatches, denied_finds = finds;
    native::dispatch(expected_context, &selector);
    check(dispatches == denied_dispatches && finds == denied_finds && en::native_call_thread.load() == 0,
          "hidden own window refuses Apply without lookup admission");
    root[off::root_flags] = capie::core::flag::shown;
    apply_mode = false;
    const auto after_apply = finds;
    native::deactivate();
    native::tick(window.data(), 7);
    check(finds == after_apply && !ed::active.load(), "deactivation preserves forwarding and closes observers");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
