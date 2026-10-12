/**
 * @file input_codes.hpp
 * @brief Converts edit-box text into the mod's logical character codes (the inline editor alphabet).
 *
 * The scripted side cannot read text, so the editor bridge sends each character as a
 * number: a-z = 1..26, A-Z = 27..52, 0-9 = 53..62, space - . ' = 63..66, Polish lower
 * case = 67..75, upper case = 76..84 (the `CHARS` table of `tools/modgen/charset.py`).
 * A field is 1..32 code points; anything outside the alphabet is an `InputError` with
 * the index of the first offending code point.
 *
 * It only converts an explicit-size view. It does not read game memory, post commands,
 * trim, normalise or truncate; a rejected field never exposes a valid prefix.
 */
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace capie::editor {

/// @brief Largest field in code points.
inline constexpr std::size_t max_field_length = 32;

/// @brief Logical codes of one field (not engine fixed-point values; the transport scales them).
struct FieldCodes {
    std::array<std::uint8_t, max_field_length> codes{};  ///< Code per position, 0 = unused.
    std::size_t length{};                                ///< Number of code points.
};

/// @brief Why a field was rejected; the numeric value is sent to the script as `rn_native(b)_err`.
enum class InputError {
    none,                   ///< 0: valid.
    empty,                  ///< 1: no characters.
    too_long,               ///< 2: more than 32 code points.
    invalid_scalar,         ///< 3: surrogate or > U+10FFFF.
    control_character,      ///< 4: C0/C1 control.
    render_delimiter,       ///< 5: HOI4 markup/delimiter character.
    unsupported_character,  ///< 6: outside the alphabet.
};

/// @brief Result of `encode_field`.
struct InputResult {
    std::optional<FieldCodes> value;     ///< Codes on success.
    InputError error{InputError::none};  ///< Error kind.
    std::size_t error_index{};           ///< Index of the first bad code point (32 for too_long), npos on success.
};

/// @brief The alphabet in code order (index + 1 = code).
inline constexpr std::u32string_view alphabet =
    U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'"
    U"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ";
/// @brief Number of characters in the alphabet (codes 1..84).
inline constexpr std::size_t alphabet_size = 84;
static_assert(alphabet.size() == alphabet_size);
/// @brief Characters with render meaning in HOI4 text.
inline constexpr std::u32string_view delimiters = U"\"[]|$§£\\{}#@";

/// @brief Highest Unicode scalar.
inline constexpr char32_t max_scalar = 0x10ffffU;
/// @brief First surrogate.
inline constexpr char32_t surrogate_first = 0xd800U;
/// @brief Last surrogate.
inline constexpr char32_t surrogate_last = 0xdfffU;
/// @brief First printable ASCII.
inline constexpr char32_t first_printable = 0x20U;
/// @brief DEL (start of the C1 control block check).
inline constexpr char32_t first_c1 = 0x7fU;
/// @brief Last C1 control.
inline constexpr char32_t last_c1 = 0x9fU;

/**
 * @brief Encodes one field.
 * @param text Explicit-size UTF-32 view of the edit-box value.
 * @return Codes, or the first error and its index.
 */
inline InputResult encode_field(std::u32string_view text) noexcept {
    if (text.empty()) {
        return {std::nullopt, InputError::empty, 0};
    }
    if (text.size() > max_field_length) {
        return {std::nullopt, InputError::too_long, max_field_length};
    }
    FieldCodes field{};
    field.length = text.size();
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto point = text[index];
        if (point > max_scalar || (point >= surrogate_first && point <= surrogate_last)) {
            return {std::nullopt, InputError::invalid_scalar, index};
        }
        if (point < first_printable || (point >= first_c1 && point <= last_c1)) {
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

}  // namespace capie::editor
