/**
 * @file input_codes.hpp
 * @brief Converts edit-box text into the mod's logical character codes (the inline editor alphabet).
 *
 * The scripted side cannot read text, so the editor bridge sends each character as a
 * number. There are two tables. The legacy table (API level 1) is Rename Anything's:
 * a-z = 1..26, A-Z = 27..52, 0-9 = 53..62, space - . ' = 63..66, Polish lower case =
 * 67..75, upper case = 76..84 (the `CHARS` table of `tools/modgen/charset.py`). Rename
 * Anything's own fields use only this table, because its script decoder knows no other
 * code. The extended table (API level 2, grammar editors) keeps codes 1..84 and appends
 * `, ! ? ( ) & : /` (85..92), the Latin-1 Supplement letters (93..152) and Latin
 * Extended-A (153..264), as defined once in `sdk/capie-script-sdk/tools/capie_charset.py`
 * (`tests/check_alphabet.py` compares the two lists).
 * A field is 1..32 code points; anything outside the chosen table is an `InputError`
 * with the index of the first offending code point.
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
    std::array<std::uint16_t, max_field_length> codes{};  ///< Code per position, 0 = unused.
    std::size_t length{};                                 ///< Number of code points.
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

/// @brief The legacy alphabet (API level 1) in code order (index + 1 = code).
inline constexpr std::u32string_view alphabet =
    U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'"
    U"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ";
/// @brief Number of characters in the legacy alphabet (codes 1..84).
inline constexpr std::size_t alphabet_size = 84;
static_assert(alphabet.size() == alphabet_size);

/// @brief The extended alphabet (API level 2) in code order: the legacy table, then the additions.
inline constexpr std::u32string_view extended_alphabet =
    U"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'"
    U"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
    U",!?()&:/"
    U"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÔÕÖØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòôõöøùúûüýþÿ"
    U"ĀāĂăĈĉĊċČčĎďĐđĒēĔĕĖėĚěĜĝĞğĠġĢģĤĥĦħĨĩĪīĬĭĮįİıĲĳĴĵĶķĸĹĺĻļĽľĿŀ"
    U"ŅņŇňŉŊŋŌōŎŏŐőŒœŔŕŖŗŘřŜŝŞşŠšŢţŤťŦŧŨũŪūŬŭŮůŰűŲųŴŵŶŷŸŽžſ";
/// @brief Number of characters in the extended alphabet (codes 1..264).
inline constexpr std::size_t extended_alphabet_size = 264;
static_assert(extended_alphabet.size() == extended_alphabet_size &&
              extended_alphabet.substr(0, alphabet_size) == alphabet);

/// @brief Which code table a field is encoded with.
enum class Alphabet {
    legacy,    ///< 84 codes (Rename Anything's fields).
    extended,  ///< 264 codes (grammar editors, API level 2).
};

/**
 * @brief The code table of an alphabet choice.
 * @param choice Table.
 * @return Characters in code order.
 */
constexpr std::u32string_view alphabet_table(Alphabet choice) noexcept {
    return choice == Alphabet::extended ? extended_alphabet : alphabet;
}
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
 * @brief Encodes one field with the chosen table.
 * @param text Explicit-size UTF-32 view of the edit-box value.
 * @param choice Code table.
 * @return Codes, or the first error and its index.
 */
inline InputResult encode_field(std::u32string_view text, Alphabet choice) noexcept {
    const auto table = alphabet_table(choice);
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
        const auto code_index = table.find(point);
        if (code_index == std::u32string_view::npos) {
            return {std::nullopt, InputError::unsupported_character, index};
        }
        field.codes[index] = static_cast<std::uint16_t>(code_index + 1);
    }
    return {field, InputError::none, std::u32string_view::npos};
}

/**
 * @brief Encodes one field with the legacy table (Rename Anything's fields).
 * @param text Explicit-size UTF-32 view of the edit-box value.
 * @return Codes, or the first error and its index.
 */
inline InputResult encode_field(std::u32string_view text) noexcept {
    return encode_field(text, Alphabet::legacy);
}

}  // namespace capie::editor
