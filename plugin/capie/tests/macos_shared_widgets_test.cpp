/** @file macos_shared_widgets_test.cpp @brief Shared widget/window guards with owned native objects. */
#include "../src/engine/macos_owned_string.hpp"
#include "../src/features/editor/window_view.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace engine = capie::engine;
namespace editor = capie::editor;
namespace off = capie::core::off;
namespace rva = capie::core::rva;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
template <class T, std::size_t N>
void put(std::array<unsigned char, N>& object, std::size_t at, T value) {
    std::memcpy(object.data() + at, &value, sizeof(value));
}
void* found_widget = nullptr;
void* find(void*, const engine::macos::NativeString*, bool) {
    return found_widget;
}
int main() {
    engine::image_base = 0x100000000;
    engine::native_call_thread.store(capie::core::platform::thread_id());
    std::array<unsigned char, 0x400> edit{};
    std::array<unsigned char, 0x100> definition{};
    put(edit, 0, engine::image_base + rva::vt_edit);
    put(edit, off::edit_buffer, engine::image_base + rva::vt_text_buffer);
    put(edit, off::edit_definition, definition.data());
    put(definition, 0, engine::image_base + rva::vt_edit_definition);
    engine::macos::NativeString name{};
    engine::macos::construct_name("field", name, nullptr);
    put(definition, off::definition_name, name);
    char32_t chars[3] = {U'ą', U'猫', 0};
    edit[off::edit_text] = 4;
    std::memcpy(edit.data() + off::edit_text + 4, chars, sizeof(chars));
    engine::InputSnapshot snapshot{};
    check(engine::snapshot_edit(edit.data(), "field", snapshot) && snapshot.size == 2 && snapshot.points[1] == U'猫',
          "shared native edit snapshot");
    check(!engine::snapshot_edit(edit.data(), "wrong", snapshot), "definition name mismatch");
    put<std::uintptr_t>(edit, off::edit_buffer, 0);
    check(!engine::snapshot_edit(edit.data(), "field", snapshot), "text buffer vtable mismatch");
    put(edit, off::edit_buffer, engine::image_base + rva::vt_text_buffer);
    edit[off::edit_text] = 10;
    check(!engine::snapshot_edit(edit.data(), "field", snapshot), "native inline UTF32 length bound");
    edit[off::edit_text] = 4;
    engine::Utf8Snapshot utf{};
    std::strcpy(utf.bytes, "Zażółć 😀");
    utf.size = std::strlen(utf.bytes);
    check(engine::decode_utf8(utf) && utf.decoded.points[utf.decoded.size - 1] == 0x1f600,
          "shared strict UTF8 supplementary");
    const char* bad[] = {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "[name]", "£", "$"};
    for (auto value : bad) {
        utf = {};
        std::strcpy(utf.bytes, value);
        utf.size = std::strlen(value);
        check(!engine::decode_utf8(utf), "invalid or formatting text rejected");
    }
    std::array<unsigned char, 0x100> lookup_root{};
    std::array<unsigned char, 0x300> dummy_owner{};
    put(lookup_root, 0x88, dummy_owner.data());
    std::array<unsigned char, 0x300> instant{};
    put(instant, 0, engine::image_base + rva::vt_instant_text);
    engine::macos::construct_name("123", name, nullptr);
    put(instant, off::instant_text, name);
    found_widget = instant.data();
    engine::find_instant = find;
    engine::Utf8Snapshot carrier{};
    check(engine::carrier(lookup_root.data(), "epoch", carrier, false) && carrier.size == 3,
          "injected native carrier lookup");
    std::uint64_t epoch = 0;
    check(editor::read_epoch_carrier(lookup_root.data(), "epoch", epoch) && epoch == 123, "shared epoch carrier");
    std::array<unsigned char, 0xc00> window{};
    std::array<unsigned char, 0x200> model{};
    std::array<unsigned char, 0xeb0> root{};
    put(window, off::window_model, model.data());
    put(model, 0, engine::image_base + rva::vt_sg_model);
    engine::macos::NativeString model_name{};
    engine::macos::construct_name(
        editor::models[0].name, model_name, +[](std::size_t n) noexcept -> void* { return std::malloc(n); });
    put(model, off::model_name, model_name);
    put(window, off::window_root, root.data());
    put(window, off::window_context, engine::image_base + rva::vt_sg_context);
    put(window, off::window_context + off::context_window, window.data());
    put(window, off::window_context + off::context_model, model.data());
    put(window, off::window_context + off::context_root, root.data());
    put(root, 0, engine::image_base + rva::vt_sg_root);
    root[off::root_flags] = capie::core::flag::shown;
    window[off::window_desired] = 1;
    check(editor::window_may_be_own(window.data()), "shared native model prefilter");
    editor::View view{};
    check(editor::inspect_window(window.data(), view) == editor::ViewResult::visible && view.root == root.data(),
          "native four-array root guard");
    root[off::root_flags] = capie::core::flag::shown | capie::core::flag::hidden_a;
    check(editor::inspect_window(window.data(), view) == editor::ViewResult::hidden, "native visibility flags");
    root[off::root_flags] = capie::core::flag::shown;
    put(window, off::window_context + off::context_root, static_cast<void*>(nullptr));
    check(editor::inspect_window(window.data(), view) == editor::ViewResult::invalid, "context root identity mismatch");
    put(window, off::window_context + off::context_root, root.data());
    put(root, 0xe98, &name);
    put<std::int32_t>(root, 0xe98 + 8, 1);
    put<std::int32_t>(root, 0xe98 + 0xc, 1);
    check(editor::inspect_window(window.data(), view) == editor::ViewResult::invalid, "paired edit-name list mismatch");
    engine::macos::NativeString public_name{};
    engine::macos::construct_name("capie_v1_ed__a__b", public_name, nullptr);
    put(model, off::model_name, public_name);
    check(editor::window_may_be_own(window.data()), "public grammar native prefilter");
    check(editor::inspect_window(window.data(), view) == editor::ViewResult::unrelated,
          "private observer rejects public model");
    std::array<unsigned char, 24> list{};
    std::array<void*, 2> children{edit.data(), instant.data()};
    put(list, 0, children.data());
    put<std::int32_t>(list, 8, 2);
    put<std::int32_t>(list, 12, 2);
    check(engine::bounded_list(list.data(), 0), "shared bounded native pointer list");
    put<std::int32_t>(list, 8, 1);
    check(!engine::bounded_list(list.data(), 0), "shared native list count exceeds capacity");
    if (model_name.bytes[0] & 1) {
        void* ptr;
        std::memcpy(&ptr, model_name.bytes.data() + 16, 8);
        std::free(ptr);
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
