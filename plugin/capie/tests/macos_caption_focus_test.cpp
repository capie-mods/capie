/** @file macos_caption_focus_test.cpp @brief Shared caption/focus native read-only boundaries. */
#include "../src/features/caption/caption_refresh.hpp"
#include "../src/features/focus/focus_release.hpp"

#include <array>
#include <cstdio>
#include <cstring>
namespace caption = capie::caption;
namespace focus = capie::focus;
namespace off = capie::core::off;
namespace rva = capie::core::rva;
constexpr std::uintptr_t image = 0x100000000;
unsigned checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
struct Memory {
    bool read(std::uintptr_t at, void* out, std::size_t size) const noexcept {
        if (size == 4 && (at == image + rva::token_politics_tab || at == image + rva::token_player_context)) {
            std::uint32_t token = at == image + rva::token_politics_tab ? capie::core::value::token_politics_tab
                                                                        : capie::core::value::token_player_context;
            std::memcpy(out, &token, 4);
            return true;
        }
        return capie::core::ProcessMemory{}.read(at, out, size);
    }
};
template <class T, std::size_t N>
void put(std::array<unsigned char, N>& object, std::size_t at, T value) {
    std::memcpy(object.data() + at, &value, sizeof(value));
}
int main() {
    Memory mem;
    std::array<unsigned char, 0xc00> window{};
    std::array<unsigned char, 0x300> model{};
    put(window, off::window_model, model.data());
    put(model, 0, image + rva::vt_sg_model);
    put(model, off::model_context_token, capie::core::value::token_player_context);
    put(model, off::model_parent_token, capie::core::value::token_politics_tab);
    put<std::uintptr_t>(window, off::window_parent, 0x123);
    std::uintptr_t parent = 0;
    auto resolve = [&] {
        return caption::detail::resolve_model(mem, image, reinterpret_cast<std::uintptr_t>(window.data()), nullptr,
                                              parent);
    };
    check(resolve() == caption::Reject::none && parent == 0x123, "native empty parent names accepted");
    model[0x40] = 2;
    model[0x41] = 'x';
    check(resolve() == caption::Reject::parent_override, "native short parent override rejected");
    model[0x40] = 0;
    model[0x58] = 46;
    check(resolve() == caption::Reject::parent_override, "malformed inline parent length rejected");
    model[0x58] = 0;
    std::uint64_t tagged = 33, length = 0;
    char long_empty[32]{};
    put(model, 0x40, tagged);
    put(model, 0x48, length);
    put(model, 0x50, long_empty);
    check(resolve() == caption::Reject::none, "native long empty parent accepted");
    long_empty[0] = 'x';
    check(resolve() == caption::Reject::parent_override, "unterminated empty parent refused");
    std::array<unsigned char, 0x400> edit{};
    edit[off::edit_text] = 4;
    const char32_t text[3] = {U'ą', U'猫', 0};
    std::memcpy(edit.data() + off::edit_text + 4, text, sizeof(text));
    focus::Text snapshot{};
    check(focus::edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), snapshot) && snapshot.size == 2 &&
              snapshot.points[1] == U'猫',
          "native focus short snapshot");
    std::array<char32_t, 8> heap = {U'a', U'b', U'c', U'd', U'e', 0};
    put<std::uint64_t>(edit, off::edit_text, 9);
    put<std::uint64_t>(edit, off::edit_text + 8, 5);
    put(edit, off::edit_text + 16, heap.data());
    check(focus::edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), snapshot) && snapshot.size == 5,
          "native focus heap snapshot");
    put<std::uint64_t>(edit, off::edit_text + 8, 8);
    check(!focus::edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), snapshot),
          "native focus size capacity bound");
    put<std::uint64_t>(edit, off::edit_text + 8, 5);
    put<std::uint64_t>(edit, off::edit_text, 131075);
    check(!focus::edit_text(mem, reinterpret_cast<std::uintptr_t>(edit.data()), snapshot),
          "native focus capacity policy bound");
    check(!focus::edit_text(mem, ~std::uintptr_t{0} - 8, snapshot), "focus text address overflow");
    check(std::size(caption::slot_pins) == 4, "four native slot checks retained");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
