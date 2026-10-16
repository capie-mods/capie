/** @file playset_json.hpp
 * @brief Platform-independent strict playset JSON reader, extracted unchanged from playset_gate.hpp.
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace capie::launcher {
namespace detail {

/// @brief Largest nesting of arrays and objects accepted in dlc_load.json.
inline constexpr unsigned json_depth_limit = 32;
/// @brief First byte that is not an ASCII control character.
inline constexpr unsigned char json_first_printable = 0x20;
/// @brief Hex digits of a `\u` escape.
inline constexpr std::size_t json_unicode_digits = 4;

/**
 * @brief A strict reader for the one JSON document the launcher needs.
 *
 * It accepts RFC 8259 JSON (strings with every escape, numbers, literals, nested arrays and
 * objects) and extracts the top-level `enabled_mods` array of strings. Anything outside the
 * grammar makes the whole document invalid; there is no lenient mode.
 */
class JsonReader {
public:
    /**
     * @brief Starts reading.
     * @param text Whole document (a UTF-8 BOM is skipped).
     */
    explicit JsonReader(std::string_view text) noexcept : text_(text) {
        constexpr std::string_view bom = "\xEF\xBB\xBF";
        if (text_.starts_with(bom)) {
            at_ = bom.size();
        }
    }

    /**
     * @brief Reads the document and returns the `enabled_mods` entries.
     * @return The entries, or no value when the document is invalid, the key is missing or
     *         repeated, or the value is not an array of strings.
     */
    std::optional<std::vector<std::string>> enabled_mods() {
        skip_space();
        if (!consume('{')) {
            return std::nullopt;
        }
        std::optional<std::vector<std::string>> found;
        bool repeated = false;
        skip_space();
        if (!consume('}')) {
            for (;;) {
                skip_space();
                std::string key;
                if (!read_string(key)) {
                    return std::nullopt;
                }
                skip_space();
                if (!consume(':')) {
                    return std::nullopt;
                }
                skip_space();
                if (key == "enabled_mods") {
                    repeated = repeated || found.has_value();
                    found = read_string_array();
                    if (!found) {
                        return std::nullopt;
                    }
                } else if (!skip_value(1)) {
                    return std::nullopt;
                }
                skip_space();
                if (consume('}')) {
                    break;
                }
                if (!consume(',')) {
                    return std::nullopt;
                }
            }
        }
        skip_space();
        if (at_ != text_.size() || repeated) {
            return std::nullopt;
        }
        return found;
    }

private:
    /// @brief Skips JSON whitespace.
    void skip_space() noexcept {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }

    /**
     * @brief Consumes one expected character.
     * @param expected Character.
     * @return Whether it was there.
     */
    bool consume(char expected) noexcept {
        if (at_ < text_.size() && text_[at_] == expected) {
            ++at_;
            return true;
        }
        return false;
    }

    /**
     * @brief Consumes a literal word (`true`, `false`, `null`).
     * @param word Word.
     * @return Whether it was there.
     */
    bool consume_word(std::string_view word) noexcept {
        if (text_.substr(at_).starts_with(word)) {
            at_ += word.size();
            return true;
        }
        return false;
    }

    /**
     * @brief Reads four hex digits of a `\u` escape.
     * @param value Receives the code unit.
     * @return Whether four hex digits were there.
     */
    bool read_hex4(std::uint32_t& value) noexcept {
        if (text_.size() - at_ < json_unicode_digits) {
            return false;
        }
        value = 0;
        for (std::size_t i = 0; i < json_unicode_digits; ++i) {
            const char digit = text_[at_ + i];
            std::uint32_t nibble = 0;
            if (digit >= '0' && digit <= '9') {
                nibble = static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                nibble = static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                nibble = static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                return false;
            }
            value = (value << 4U) | nibble;
        }
        at_ += json_unicode_digits;
        return true;
    }

    /**
     * @brief Appends a code point as UTF-8.
     * @param out Output.
     * @param code Code point (at most U+10FFFF).
     */
    static void append_utf8(std::string& out, std::uint32_t code) {
        constexpr std::uint32_t one_byte_end = 0x80;
        constexpr std::uint32_t two_byte_end = 0x800;
        constexpr std::uint32_t three_byte_end = 0x10000;
        constexpr std::uint32_t two_byte_lead = 0xC0;
        constexpr std::uint32_t three_byte_lead = 0xE0;
        constexpr std::uint32_t four_byte_lead = 0xF0;
        constexpr std::uint32_t continuation = 0x80;
        constexpr std::uint32_t six_bits = 0x3F;
        constexpr unsigned shift_1 = 6;
        constexpr unsigned shift_2 = 12;
        constexpr unsigned shift_3 = 18;
        const auto tail = [&out](std::uint32_t bits) {
            out.push_back(static_cast<char>(continuation | (bits & six_bits)));
        };
        if (code < one_byte_end) {
            out.push_back(static_cast<char>(code));
        } else if (code < two_byte_end) {
            out.push_back(static_cast<char>(two_byte_lead | (code >> shift_1)));
            tail(code);
        } else if (code < three_byte_end) {
            out.push_back(static_cast<char>(three_byte_lead | (code >> shift_2)));
            tail(code >> shift_1);
            tail(code);
        } else {
            out.push_back(static_cast<char>(four_byte_lead | (code >> shift_3)));
            tail(code >> shift_2);
            tail(code >> shift_1);
            tail(code);
        }
    }

    /**
     * @brief Reads the escape after a backslash.
     * @param out Output.
     * @return Whether the escape is valid.
     */
    bool read_escape(std::string& out) {
        if (at_ >= text_.size()) {
            return false;
        }
        const char kind = text_[at_++];
        switch (kind) {
            case '"':
            case '\\':
            case '/':
                out.push_back(kind);
                return true;
            case 'b':
                out.push_back('\b');
                return true;
            case 'f':
                out.push_back('\f');
                return true;
            case 'n':
                out.push_back('\n');
                return true;
            case 'r':
                out.push_back('\r');
                return true;
            case 't':
                out.push_back('\t');
                return true;
            case 'u':
                break;
            default:
                return false;
        }
        constexpr std::uint32_t high_first = 0xD800;
        constexpr std::uint32_t low_first = 0xDC00;
        constexpr std::uint32_t low_end = 0xE000;
        constexpr std::uint32_t surrogate_base = 0x10000;
        constexpr unsigned surrogate_shift = 10;
        std::uint32_t code = 0;
        if (!read_hex4(code)) {
            return false;
        }
        if (code >= low_first && code < low_end) {
            return false;  // a low surrogate without its high half
        }
        if (code >= high_first && code < low_first) {
            std::uint32_t low = 0;
            if (!consume('\\') || !consume('u') || !read_hex4(low) || low < low_first || low >= low_end) {
                return false;
            }
            code = surrogate_base + ((code - high_first) << surrogate_shift) + (low - low_first);
        }
        append_utf8(out, code);
        return true;
    }

    /**
     * @brief Reads a string token.
     * @param out Receives the decoded UTF-8 text.
     * @return Whether a valid string was there.
     */
    bool read_string(std::string& out) {
        if (!consume('"')) {
            return false;
        }
        while (at_ < text_.size()) {
            const char next = text_[at_++];
            if (next == '"') {
                return true;
            }
            if (static_cast<unsigned char>(next) < json_first_printable) {
                return false;
            }
            if (next == '\\') {
                if (!read_escape(out)) {
                    return false;
                }
            } else {
                out.push_back(next);
            }
        }
        return false;
    }

    /**
     * @brief Reads one or more decimal digits.
     * @return Whether at least one digit was there.
     */
    bool read_digits() noexcept {
        const std::size_t start = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            ++at_;
        }
        return at_ > start;
    }

    /**
     * @brief Skips a number token (RFC 8259 grammar).
     * @return Whether a valid number was there.
     */
    bool skip_number() noexcept {
        static_cast<void>(consume('-'));
        if (!consume('0') && !read_digits()) {  // a leading zero stands alone
            return false;
        }
        if (consume('.') && !read_digits()) {
            return false;
        }
        if (consume('e') || consume('E')) {
            if (!consume('+')) {
                static_cast<void>(consume('-'));
            }
            if (!read_digits()) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Skips any value.
     * @param depth Current nesting depth.
     * @return Whether a valid value was there.
     */
    bool skip_value(unsigned depth) {  // NOLINT(misc-no-recursion): bounded by json_depth_limit
        if (depth > json_depth_limit || at_ >= text_.size()) {
            return false;
        }
        const char first = text_[at_];
        if (first == '"') {
            std::string ignored;
            return read_string(ignored);
        }
        if (first == '{' || first == '[') {
            const bool object = first == '{';
            const char close = object ? '}' : ']';
            ++at_;
            skip_space();
            if (consume(close)) {
                return true;
            }
            for (;;) {
                skip_space();
                if (object) {
                    std::string ignored;
                    if (!read_string(ignored)) {
                        return false;
                    }
                    skip_space();
                    if (!consume(':')) {
                        return false;
                    }
                    skip_space();
                }
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_space();
                if (consume(close)) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
            }
        }
        if (consume_word("true") || consume_word("false") || consume_word("null")) {
            return true;
        }
        return skip_number();
    }

    /**
     * @brief Reads an array whose elements are all strings.
     * @return The strings, or no value when it is not such an array.
     */
    std::optional<std::vector<std::string>> read_string_array() {
        if (!consume('[')) {
            return std::nullopt;
        }
        std::vector<std::string> items;
        skip_space();
        if (consume(']')) {
            return items;
        }
        for (;;) {
            skip_space();
            std::string item;
            if (!read_string(item)) {
                return std::nullopt;
            }
            items.push_back(std::move(item));
            skip_space();
            if (consume(']')) {
                return items;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

/**
 * @brief Normalises a descriptor path for comparison: `/` separators, ASCII lower case.
 * @param path Path as written in dlc_load.json or in the identity.
 * @return Normalised path.
 */
inline std::string normalise_descriptor(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const char ch : path) {
        if (ch == '\\') {
            out.push_back('/');
        } else if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

/**
 * @brief Whether a text is a non-empty run of decimal digits.
 * @param text Text.
 * @return `true` for digits only.
 */
inline bool all_digits(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char ch) { return ch >= '0' && ch <= '9'; });
}

}  // namespace detail

/**
 * @brief Extracts `enabled_mods` from the text of dlc_load.json.
 * @param json File content.
 * @return Entries, or no value when the document is not the expected shape.
 */
inline std::optional<std::vector<std::string>> parse_enabled_mods(std::string_view json) {
    detail::JsonReader reader(json);
    return reader.enabled_mods();
}


} // namespace capie::launcher
