/**
 * @file country_protocol.hpp
 * @brief The RN1 country-name protocol carried in a vanilla `cosmetic_tag`.
 *
 * The scripted Apply button stores `RN1|Name|Adjective|` as the country's cosmetic tag.
 * The game then asks localisation for keys built from that tag: the bare tag, `_DEF`
 * and `_ADJ` (ideology variants fall back to these). `decode_country_key` answers those
 * three forms and nothing else; other systems that probe cosmetic-tag prefixes (for
 * example decision titles) get `nullopt` and keep their vanilla text.
 *
 * Validation (`valid_text`): 1..32 Unicode scalars of well-formed UTF-8, at most 128
 * bytes, no control characters and none of the delimiters or markup characters `|"\[]${}#@`.
 * The scripted GUI already restricts input to its alphabet; this is the native guard.
 *
 * Pure functions; no I/O, no globals. Allocates the two result strings.
 */
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace capie::country {

/// @brief Prefix of every protocol key.
inline constexpr std::string_view protocol_prefix = "RN1|";
/// @brief Field delimiter.
inline constexpr char field_delimiter = '|';
/// @brief Suffix of the "definite" name form.
inline constexpr std::string_view definite_suffix = "_DEF";
/// @brief Suffix of the adjective form.
inline constexpr std::string_view adjective_suffix = "_ADJ";
/// @brief Largest field in bytes (32 code points of up to 4 bytes).
inline constexpr std::size_t max_field_bytes = 128;
/// @brief Largest field in Unicode code points.
inline constexpr std::size_t max_field_codepoints = 32;

/// @brief Decoded key: both texts and which form was asked for.
struct CountryText {
    std::string name;       ///< Country name.
    std::string adjective;  ///< Country adjective.
    bool adjective_key;     ///< Key had the `_ADJ` suffix.
};

/**
 * @brief Checks one field of the payload.
 * @param s Field bytes.
 * @return `true` for 1..32 code points of valid UTF-8 without controls or delimiters.
 * @note The numeric literals below are the UTF-8 encoding table (RFC 3629, section 4) and
 *       ASCII controls; naming each byte class would hide the table, so the magic-number
 *       check is suppressed for this function only.
 */
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
inline bool valid_text(std::string_view s) {
    if (s.empty() || s.size() > max_field_bytes) {
        return false;
    }
    for (const unsigned char c : s) {
        if (c < 0x20 || c == 0x7f || c == '|' || c == '"' || c == '\\' || c == '[' || c == ']' || c == '$' ||
            c == '{' || c == '}' || c == '#' || c == '@') {
            return false;
        }
    }
    // Reject invalid UTF-8, overlong forms, surrogate code points and > U+10FFFF.
    std::size_t points = 0;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        unsigned n = 0;
        unsigned value = 0;
        unsigned minimum = 0;
        if (c < 0x80) {
            value = c;
        } else if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            value = c & 0x1fU;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            value = c & 0xfU;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            value = c & 7U;
            minimum = 0x10000;
        } else {
            return false;
        }
        while (n-- != 0) {
            if (i == s.size()) {
                return false;
            }
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0U) != 0x80) {
                return false;
            }
            value = (value << 6) | (c & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
        if (++points > max_field_codepoints) {
            return false;
        }
    }
    return true;
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Decodes a localisation key of the form `RN1|Name|Adjective|[_DEF|_ADJ]`.
 * @param key Localisation key.
 * @return Decoded texts, or `nullopt` for any other key (including other suffixes).
 */
inline std::optional<CountryText> decode_country_key(std::string_view key) {
    if (!key.starts_with(protocol_prefix)) {
        return std::nullopt;
    }
    const auto middle = key.find(field_delimiter, protocol_prefix.size());
    if (middle == std::string_view::npos) {
        return std::nullopt;
    }
    const auto last = key.find(field_delimiter, middle + 1);
    if (last == std::string_view::npos) {
        return std::nullopt;
    }
    const auto name = key.substr(protocol_prefix.size(), middle - protocol_prefix.size());
    const auto adjective = key.substr(middle + 1, last - middle - 1);
    if (!valid_text(name) || !valid_text(adjective)) {
        return std::nullopt;
    }
    const auto suffix = key.substr(last + 1);
    // Other systems also probe cosmetic_tag + content-specific suffixes (e.g. decisions).
    // Claiming arbitrary identifiers corrupts unrelated localisation. BuildNameKey falls
    // back from ideology-specific variants to these base forms.
    if (!suffix.empty() && suffix != definite_suffix && suffix != adjective_suffix) {
        return std::nullopt;
    }
    return CountryText{std::string(name), std::string(adjective), suffix == adjective_suffix};
}

}  // namespace capie::country
