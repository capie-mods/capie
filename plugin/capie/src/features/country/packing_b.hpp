/**
 * @file packing_b.hpp
 * @brief Packing B (API level 9, CONTRACT.md 21.3): names with `/ . - '`, up to 126 characters.
 *
 * The 17.3 file-name alphabet (`flag_layer.hpp`) is full and cannot express a texture path. Packing B
 * stores the ASCII value of each character, three per element:
 *
 *     element = (c0 * 128 + c1) * 128 + c2      (at most 2 097 151 < 2 147 483, the script limit)
 *
 * Allowed characters: `A-Z a-z 0-9 _ - . / '` (codes 39..122). The last element is padded with 0.
 * A length is 1..126, so `^num` = ceil(length / 3) is at most 42. Invalid: a code outside the
 * set, a 0 before the end, a non-zero code after it, a fraction, a bad length or `^num`.
 *
 * A name that contains `/` is a texture path and must pass the path rules: it starts with `gfx/`,
 * ends with `.dds`, `.png` or `.tga` (any case), and has no `..`, no `//` and no leading `/`.
 * Everything else is a sprite name.
 *
 * Pure functions over the variable lookup of `name_layer.hpp`; no writes, no game calls.
 */
#pragma once
#include "name_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace capie::country::packing_b {

using layer::Lookup;
using layer::Status;
using layer::Table;

/// @brief Longest name in characters.
inline constexpr std::size_t max_chars = 126;
/// @brief Characters per element.
inline constexpr std::size_t chars_per_element = 3;
/// @brief Base of the packing (7 bits per character).
inline constexpr std::int64_t pack_base = 128;
/// @brief Largest element value (three 7-bit characters).
inline constexpr std::int64_t max_element = (pack_base * pack_base * pack_base) - 1;
/// @brief Most elements of one name.
inline constexpr std::size_t max_elements = max_chars / chars_per_element;
/// @brief Prefix every texture path must have.
inline constexpr std::string_view path_prefix = "gfx/";

/// @brief A decoded name: a sprite name or a texture path.
struct Text {
    std::array<char, max_chars + 1> bytes{};  ///< Characters, NUL-terminated.
    std::size_t size{};                       ///< Length.

    /// @brief The text. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/**
 * @brief Whether a character belongs to packing B.
 * @param c Character.
 * @return `true` for `A-Z a-z 0-9 _ - . / '`.
 */
constexpr bool allowed(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
           c == '.' || c == '/' || c == '\'';
}

/**
 * @brief Character of a code (the ASCII value), or 0 when outside the set.
 * @param code Code.
 * @return Character or 0.
 */
constexpr char char_of(std::int64_t code) noexcept {
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers): ASCII range of the set
    if (code < 39 || code > 122) {
        return '\0';
    }
    const auto c = static_cast<char>(code);
    return allowed(c) ? c : '\0';
}

/**
 * @brief Lower-case ASCII.
 * @param c Character.
 * @return Lower case.
 */
constexpr char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/**
 * @brief Whether `text` ends with `suffix`, ignoring ASCII case.
 * @param text Text.
 * @param suffix Lower-case suffix.
 * @return `true` on a match.
 */
constexpr bool ends_with_nocase(std::string_view text, std::string_view suffix) noexcept {
    if (text.size() < suffix.size()) {
        return false;
    }
    const auto tail = text.substr(text.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (lower(tail[i]) != suffix[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a name is a texture path (it contains `/`).
 * @param name Name.
 * @return `true` for a path.
 */
constexpr bool is_path(std::string_view name) noexcept {
    return name.find('/') != std::string_view::npos;
}

/**
 * @brief The path rules of CONTRACT.md 21.3.
 * @param path Candidate path.
 * @return `true` when it may be loaded.
 */
constexpr bool valid_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > max_chars || !path.starts_with(path_prefix) ||
        path.find("..") != std::string_view::npos || path.find("//") != std::string_view::npos) {
        return false;
    }
    for (const char c : path) {
        if (!allowed(c)) {
            return false;
        }
    }
    return ends_with_nocase(path, ".dds") || ends_with_nocase(path, ".png") || ends_with_nocase(path, ".tga");
}

/**
 * @brief Whether a name is acceptable: a path that passes the rules, or a sprite name of the set.
 * @param name Name.
 * @return `true` when valid.
 */
constexpr bool valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > max_chars) {
        return false;
    }
    if (is_path(name)) {
        return valid_path(name);
    }
    for (const char c : name) {
        if (!allowed(c)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Packs one element (encoder for tests, the console and records).
 * @param text Text in the set.
 * @param index Element index.
 * @return The element value (padding 0).
 */
constexpr std::int64_t element_of(std::string_view text, std::size_t index) noexcept {
    std::int64_t element = 0;
    for (std::size_t slot = 0; slot < chars_per_element; ++slot) {
        const auto at = (index * chars_per_element) + slot;
        element = (element * pack_base) + (at < text.size() ? static_cast<unsigned char>(text[at]) : 0);
    }
    return element;
}

/**
 * @brief Decodes one element into `out` (characters before `length` only).
 * @param value Element value.
 * @param index Element index.
 * @param length Text length.
 * @param out Receives the characters.
 * @return `false` for an invalid element.
 */
constexpr bool decode_element(std::int64_t value, std::size_t index, std::size_t length, Text& out) noexcept {
    if (value < 0 || value > max_element) {
        return false;
    }
    const std::int64_t codes[chars_per_element] = {value / (pack_base * pack_base), (value / pack_base) % pack_base,
                                                   value % pack_base};
    for (std::size_t slot = 0; slot < chars_per_element; ++slot) {
        const auto position = (index * chars_per_element) + slot;
        if (position < length) {
            const char c = char_of(codes[slot]);
            if (c == '\0') {
                return false;  // a 0 (or a code outside the set) inside the text
            }
            out.bytes[out.size++] = c;
        } else if (codes[slot] != 0) {
            return false;  // padding must be 0
        }
    }
    return true;
}

/**
 * @brief Decodes one packed name: `<length_name>` and `<array>^0..`.
 * @param mem Memory policy.
 * @param table Variable table.
 * @param length_name Length variable.
 * @param array Array name.
 * @param allow_empty `true` when length 0 means "none".
 * @param out Receives the text.
 * @return `valid`, `absent` (length variable missing), `invalid` or `unreadable`.
 */
template <class M>
Status read_packed(const M& mem, const Table& table, std::string_view length_name, std::string_view array,
                   bool allow_empty, Text& out) noexcept {
    std::int64_t raw = 0;
    const auto found = layer::find_variable(mem, table, length_name, raw);
    if (found != Lookup::found) {
        return found == Lookup::absent ? Status::absent : Status::unreadable;
    }
    std::int64_t length = 0;
    if (!layer::whole(raw, length) || length < (allow_empty ? 0 : 1) || std::cmp_greater(length, max_chars)) {
        return Status::invalid;
    }
    out = Text{};
    if (length == 0) {
        return Status::valid;
    }
    const auto size = static_cast<std::size_t>(length);
    const auto elements = (size + chars_per_element - 1) / chars_per_element;
    std::array<char, layer::max_variable_name> name{};
    std::int64_t count = 0;
    const auto num = layer::find_variable(mem, table, layer::element_name(array, "num", name), raw);
    if (num == Lookup::unreadable) {
        return Status::unreadable;
    }
    if (num == Lookup::absent || !layer::whole(raw, count) || std::cmp_not_equal(count, elements)) {
        return Status::invalid;
    }
    for (std::size_t index = 0; index < elements; ++index) {
        std::array<char, 3> digits{};
        std::size_t used = 0;
        constexpr std::size_t ten = 10;
        if (index >= ten) {
            digits[used++] = static_cast<char>('0' + (index / ten));
        }
        digits[used++] = static_cast<char>('0' + (index % ten));
        const auto lookup =
            layer::find_variable(mem, table, layer::element_name(array, {digits.data(), used}, name), raw);
        if (lookup == Lookup::unreadable) {
            return Status::unreadable;
        }
        std::int64_t value = 0;
        if (lookup == Lookup::absent || !layer::whole(raw, value) || !decode_element(value, index, size, out)) {
            return Status::invalid;
        }
    }
    return valid_name(out.view()) ? Status::valid : Status::invalid;
}

}  // namespace capie::country::packing_b
