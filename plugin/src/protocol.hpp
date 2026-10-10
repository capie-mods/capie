#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace rn {
// The scripted GUI excludes | and engine markup from both buffers.
// The final | separates the payload from vanilla ideology/DEF/ADJ suffixes.
struct CountryText { std::string name; std::string adjective; bool adjective_key; };

inline bool valid_text(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    for (const unsigned char c : s) {
        if (c < 0x20 || c == 0x7f || c == '|' || c == '"' || c == '\\' ||
            c == '[' || c == ']' || c == '$' || c == '{' || c == '}' ||
            c == '#' || c == '@') return false;
    }
    // Reject invalid UTF-8, overlong forms, surrogate codepoints and > U+10FFFF.
    size_t points = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i++]);
        unsigned n = 0; unsigned value = 0; unsigned minimum = 0;
        if (c < 0x80) value = c;
        else if (c >= 0xc2 && c <= 0xdf) { n = 1; value = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 2; value = c & 0xf; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        while (n--) {
            if (i == s.size()) return false;
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        if (++points > 32) return false;
    }
    return true;
}

inline std::optional<CountryText> decode_country_key(std::string_view key) {
    constexpr std::string_view prefix = "RN1|";
    if (!key.starts_with(prefix)) return std::nullopt;
    const auto middle = key.find('|', prefix.size());
    if (middle == key.npos) return std::nullopt;
    const auto last = key.find('|', middle + 1);
    if (last == key.npos) return std::nullopt;
    const auto name = key.substr(prefix.size(), middle - prefix.size());
    const auto adjective = key.substr(middle + 1, last - middle - 1);
    if (!valid_text(name) || !valid_text(adjective)) return std::nullopt;
    const auto suffix = key.substr(last + 1);
    // Vanilla country keys append only ASCII identifier suffixes.
    for (const char c : suffix) {
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) return std::nullopt;
    }
    if (suffix.size() > 96) return std::nullopt;
    return CountryText{std::string(name), std::string(adjective), suffix.ends_with("_ADJ")};
}
}
