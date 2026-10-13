/**
 * @file console_text.hpp
 * @brief Text rules for console arguments: country tags, ids, names and their codes.
 *
 * A name typed in the console must satisfy exactly what an editor field must satisfy, so
 * the same text reaches the same setter either way:
 *
 * - UTF-8 that decodes to 1..32 Unicode scalars (the console edit box is assumed to give
 *   UTF-8, like every `CEditBox` CAPIE reads; open item O3);
 * - every code point in the CAPIE alphabet (API level 2 extended table, codes 1..264),
 *   checked by `editor::encode_field`, which also rejects controls and markup;
 * - no leading or trailing space and no two spaces in a row (SDK errors 33 and 34);
 * - for country names, also `country::valid_text`, the RN1 decoder's own guard.
 *
 * Nothing is trimmed, normalised or truncated. Tags are checked for syntax only (three
 * characters `A-Z` / `0-9`); whether the country exists is decided by the game's command.
 * Pure functions: no allocation, no Windows API, no game memory.
 */
#pragma once
#include "../country/country_protocol.hpp"
#include "../editor/input_codes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace capie::console::text {

/// @brief Length of a country tag (`GER`, `D01`).
inline constexpr std::size_t tag_length = 3;
/// @brief Longest mod or command id (CONTRACT section 3 id rule).
inline constexpr std::size_t max_id = 12;
/// @brief Longest text in code points.
inline constexpr std::size_t max_points = editor::max_field_length;

/// @brief Why a text was rejected. Values 1..6 equal `editor::InputError`; 33, 34 equal SDK codes.
enum class TextError : unsigned {
    none = 0,                   ///< Valid.
    empty = 1,                  ///< No characters.
    too_long = 2,               ///< More than 32 code points.
    invalid_scalar = 3,         ///< Surrogate or > U+10FFFF.
    control_character = 4,      ///< C0/C1 control.
    markup_character = 5,       ///< HOI4 markup or delimiter character.
    outside_alphabet = 6,       ///< Not in the CAPIE alphabet.
    edge_space = 33,            ///< Leading or trailing space.
    double_space = 34,          ///< Two spaces in a row.
    invalid_utf8 = 40,          ///< Bytes are not well-formed UTF-8.
    rejected_by_protocol = 41,  ///< `country::valid_text` refused it (defensive; should not happen).
};

/// @brief Result of `check_text`.
struct TextCheck {
    TextError error{TextError::none};  ///< First error.
    std::size_t position{};            ///< 1-based code-point position of the error, 0 when none.
    editor::FieldCodes codes{};        ///< Codes on success (length 0 for an allowed empty text).
};

/// @brief UTF-8 lead-byte classes (RFC 3629, section 4).
namespace utf8 {
inline constexpr unsigned char ascii_end = 0x80;     ///< First non-ASCII byte.
inline constexpr unsigned char two_first = 0xc2;     ///< First valid 2-byte lead.
inline constexpr unsigned char two_last = 0xdf;      ///< Last 2-byte lead.
inline constexpr unsigned char three_first = 0xe0;   ///< First 3-byte lead.
inline constexpr unsigned char three_last = 0xef;    ///< Last 3-byte lead.
inline constexpr unsigned char four_first = 0xf0;    ///< First 4-byte lead.
inline constexpr unsigned char four_last = 0xf4;     ///< Last 4-byte lead.
inline constexpr unsigned char two_mask = 0x1f;      ///< Payload of a 2-byte lead.
inline constexpr unsigned char three_mask = 0x0f;    ///< Payload of a 3-byte lead.
inline constexpr unsigned char four_mask = 0x07;     ///< Payload of a 4-byte lead.
inline constexpr unsigned char tail_mask = 0xc0;     ///< Bits that mark a continuation byte.
inline constexpr unsigned char tail_marker = 0x80;   ///< Value of those bits.
inline constexpr unsigned char tail_payload = 0x3f;  ///< Payload of a continuation byte.
inline constexpr unsigned tail_bits = 6;             ///< Payload bits per continuation byte.
inline constexpr char32_t two_min = 0x80;            ///< Smallest 2-byte scalar (no overlong).
inline constexpr char32_t three_min = 0x800;         ///< Smallest 3-byte scalar.
inline constexpr char32_t four_min = 0x10000;        ///< Smallest 4-byte scalar.
}  // namespace utf8

/// @brief Decoded code points of one argument.
struct Points {
    std::array<char32_t, max_points + 1> items{};  ///< Code points; one extra slot detects "too long".
    std::size_t count{};                           ///< Number decoded (at most `max_points + 1`).
    bool truncated{};                              ///< More than `max_points` code points.
};

/**
 * @brief Decodes UTF-8 strictly (no overlongs, no surrogates, nothing above U+10FFFF).
 * @param bytes Input.
 * @param out Receives up to `max_points + 1` code points.
 * @return `false` on malformed input.
 */
inline bool decode(std::string_view bytes, Points& out) noexcept {
    out = Points{};
    std::size_t index = 0;
    while (index < bytes.size()) {
        auto lead = static_cast<unsigned char>(bytes[index++]);
        unsigned tails = 0;
        char32_t value = 0;
        char32_t minimum = 0;
        if (lead < utf8::ascii_end) {
            value = lead;
        } else if (lead >= utf8::two_first && lead <= utf8::two_last) {
            tails = 1;
            value = lead & utf8::two_mask;
            minimum = utf8::two_min;
        } else if (lead >= utf8::three_first && lead <= utf8::three_last) {
            tails = 2;
            value = lead & utf8::three_mask;
            minimum = utf8::three_min;
        } else if (lead >= utf8::four_first && lead <= utf8::four_last) {
            tails = 3;
            value = lead & utf8::four_mask;
            minimum = utf8::four_min;
        } else {
            return false;
        }
        for (; tails != 0; --tails) {
            if (index == bytes.size()) {
                return false;
            }
            const auto tail = static_cast<unsigned char>(bytes[index++]);
            if ((tail & utf8::tail_mask) != utf8::tail_marker) {
                return false;
            }
            value = (value << utf8::tail_bits) | (tail & utf8::tail_payload);
        }
        if (value < minimum || value > editor::max_scalar ||
            (value >= editor::surrogate_first && value <= editor::surrogate_last)) {
            return false;
        }
        if (out.count == max_points + 1) {
            out.truncated = true;
            continue;  // keep validating the rest of the bytes
        }
        out.items[out.count++] = value;
    }
    out.truncated = out.truncated || out.count > max_points;
    return true;
}

/**
 * @brief Checks a name or argument and encodes it with the extended alphabet.
 * @param bytes UTF-8 text.
 * @param allow_empty `true` for modder arguments, where `""` is a valid empty value.
 * @return The first error, or the codes.
 */
inline TextCheck check_text(std::string_view bytes, bool allow_empty) noexcept {
    TextCheck result{};
    Points points{};
    if (!decode(bytes, points)) {
        result.error = TextError::invalid_utf8;
        return result;
    }
    if (points.count == 0) {
        if (!allow_empty) {
            result.error = TextError::empty;
        }
        return result;
    }
    if (points.truncated) {
        result.error = TextError::too_long;
        result.position = max_points + 1;
        return result;
    }
    const std::u32string_view view(points.items.data(), points.count);
    const auto encoded = editor::encode_field(view, editor::Alphabet::extended);
    if (!encoded.value) {
        result.error = static_cast<TextError>(static_cast<unsigned>(encoded.error));
        result.position = encoded.error_index + 1;
        return result;
    }
    if (view.front() == U' ') {
        result.error = TextError::edge_space;
        result.position = 1;
        return result;
    }
    if (view.back() == U' ') {
        result.error = TextError::edge_space;
        result.position = view.size();
        return result;
    }
    for (std::size_t index = 1; index < view.size(); ++index) {
        if (view[index] == U' ' && view[index - 1] == U' ') {
            result.error = TextError::double_space;
            result.position = index + 1;
            return result;
        }
    }
    result.codes = *encoded.value;
    return result;
}

/**
 * @brief Checks a country name or adjective (non-empty, alphabet, spacing, RN1 guard).
 * @param bytes UTF-8 text.
 * @return The first error, or the codes.
 */
inline TextCheck check_country_text(std::string_view bytes) noexcept {
    auto result = check_text(bytes, false);
    if (result.error == TextError::none && !country::valid_text(bytes)) {
        result.error = TextError::rejected_by_protocol;
    }
    return result;
}

/**
 * @brief Checks the syntax of a country tag.
 * @param tag Candidate.
 * @return `true` for exactly three characters from `A-Z` and `0-9`.
 */
constexpr bool valid_tag(std::string_view tag) noexcept {
    if (tag.size() != tag_length) {
        return false;
    }
    return std::ranges::all_of(tag, [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
}

/**
 * @brief Checks a mod or command id.
 * @param id Candidate.
 * @return `true` for 1..12 characters from `a-z` and `0-9`.
 */
constexpr bool valid_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > max_id) {
        return false;
    }
    return std::ranges::all_of(id, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

/**
 * @brief English text of a text error for the console.
 * @param error Error.
 * @return Static message.
 */
constexpr std::string_view describe(TextError error) noexcept {
    switch (error) {
        case TextError::none:
            return "ok";
        case TextError::empty:
            return "empty";
        case TextError::too_long:
            return "more than 32 characters";
        case TextError::invalid_scalar:
            return "invalid Unicode character";
        case TextError::control_character:
            return "control character";
        case TextError::markup_character:
            return "markup character";
        case TextError::outside_alphabet:
            return "character outside the CAPIE alphabet";
        case TextError::edge_space:
            return "leading or trailing space";
        case TextError::double_space:
            return "two spaces in a row";
        case TextError::invalid_utf8:
            return "invalid UTF-8";
        case TextError::rejected_by_protocol:
            return "not allowed in a country name";
    }
    return "error";
}

}  // namespace capie::console::text
