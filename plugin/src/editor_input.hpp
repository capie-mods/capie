#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rn::editor {
inline constexpr std::size_t max_field_length = 32;

struct FieldCodes {
    // Logical tools/gen.py CHARS values; these are not engine fixed-point
    // storage, a serialized command, or text bytes. The bridge owns that step.
    std::array<std::uint8_t, max_field_length> codes{};
    std::size_t length{};
};

enum class InputError {
    none, empty, too_long, invalid_scalar, control_character,
    render_delimiter, unsupported_character
};

struct InputResult {
    std::optional<FieldCodes> value;
    InputError error;
    // UTF-32 codepoint index, or npos on success. Length failure reports the
    // first excess point (32). A rejected field never exposes a valid prefix.
    std::size_t error_index;
};

// Accept an explicit-size view of the CTextBuffer UTF-32 value. Reading engine
// memory/lifetimes and posting commands are deliberately outside this helper.
// Do not trim, normalize, truncate, replace characters, or use a NUL terminator
// to derive the view's size: each of those can change the user's intended name.
inline InputResult encode_field(std::u32string_view text) noexcept {
    if (text.empty()) return {std::nullopt, InputError::empty, 0};
    if (text.size() > max_field_length) {
        return {std::nullopt, InputError::too_long, max_field_length};
    }

    // tools/gen.py CHARS: a-z=1..26, A-Z=27..52, 0-9=53..62,
    // space/-/./'=63..66, Polish lowercase=67..75, uppercase=76..84.
    constexpr std::u32string_view alphabet =
        U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'"
        U"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ";
    constexpr std::u32string_view delimiters = U"\"[]|$§£\\{}#@";
    static_assert(alphabet.size() == 84);

    FieldCodes field{};
    field.length = text.size();
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto point = text[index];
        if (point > 0x10ffffU || (point >= 0xd800U && point <= 0xdfffU)) {
            return {std::nullopt, InputError::invalid_scalar, index};
        }
        if (point < 0x20U || (point >= 0x7fU && point <= 0x9fU)) {
            return {std::nullopt, InputError::control_character, index};
        }
        if (delimiters.find(point) != std::u32string_view::npos) {
            return {std::nullopt, InputError::render_delimiter, index};
        }
        const auto code_index = alphabet.find(point);
        if (code_index == std::u32string_view::npos) {
            return {std::nullopt, InputError::unsupported_character, index};
        }
        field.codes[index] = static_cast<std::uint8_t>(code_index + 1);
    }
    return {field, InputError::none, std::u32string_view::npos};
}
}
