/**
 * @file playset_gate.hpp
 * @brief The player's on/off switch for CAPIE: the small "CAPIE" mod in the active playset.
 *
 * The Paradox Launcher cannot get an extra button, so the switch is a mod. Before it starts
 * Hearts of Iron IV the Paradox Launcher writes the playset it is about to run into
 * `Documents\Paradox Interactive\Hearts of Iron IV\dlc_load.json`, and the game reads its mod
 * list from that file:
 *
 * @code{.json}
 * {"enabled_mods":["mod/ugc_3274762952.mod","mod/capie.mod"],"disabled_dlcs":[]}
 * @endcode
 *
 * Each entry names a mod descriptor relative to the game's user folder. A Workshop mod is
 * always `mod/ugc_<Workshop ID>.mod`; a local mod is `mod/<file>.mod`. The launcher reads
 * the file once the game process it started exists (the playset is final then) and loads
 * CAPIE only when the CAPIE mod is listed. Everything else is vanilla:
 *
 *  - CAPIE mod listed                         -> `enabled`
 *  - valid file, CAPIE mod not listed         -> `disabled` (the player's choice; no notice)
 *  - missing, unreadable, too large, invalid
 *    JSON, no `enabled_mods` array, a non-string
 *    entry, a duplicate key, or no identity    -> `unknown` (no CAPIE, one notice)
 *
 * The game always starts; this check only decides whether the launcher injects anything.
 *
 * How the CAPIE mod is recognised lives in one value, `ModIdentity` (production values in
 * `config.hpp`): today the local descriptor path `mod/capie.mod`; once the mod is on the
 * Workshop its ID goes into `capie_mod_workshop_id` and `mod/ugc_<ID>.mod` matches as well.
 *
 * `parse_enabled_mods`, `mod_matches` and `evaluate_playset` are pure, so the tests drive every
 * format; `read_playset` only adds the bounded, shared, read-only file access.
 */
#pragma once
#include "config.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace capie::launcher {

/// @brief Whether the CAPIE mod is enabled in the playset the game was started with.
enum class PlaysetState : std::uint8_t {
    enabled,   ///< the CAPIE mod is listed: CAPIE may load
    disabled,  ///< the file is valid and the CAPIE mod is not listed: vanilla
    unknown,   ///< the file could not be read or understood: vanilla, the player is told
};

/// @brief Result of the playset check.
struct PlaysetVerdict {
    PlaysetState state = PlaysetState::unknown;  ///< decision
    std::wstring reason;                         ///< one line for the log and the notice
};

/**
 * @brief Text of a playset state for the log.
 * @param state State.
 * @return Short tag.
 */
constexpr const wchar_t* playset_state_name(PlaysetState state) noexcept {
    switch (state) {
        case PlaysetState::enabled:
            return L"enabled";
        case PlaysetState::disabled:
            return L"disabled";
        case PlaysetState::unknown:
            break;
    }
    return L"unknown";
}

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

/**
 * @brief Whether an identity can recognise anything at all.
 * @param identity Identity.
 * @return `true` when it has a valid Workshop ID or at least one descriptor path.
 */
inline bool identity_usable(const ModIdentity& identity) noexcept {
    return detail::all_digits(identity.workshop_id) ||
           std::ranges::any_of(identity.descriptors, [](const std::string& descriptor) { return !descriptor.empty(); });
}

/**
 * @brief Whether one `enabled_mods` entry names the CAPIE mod.
 * @param entry Entry from dlc_load.json.
 * @param identity Identity.
 * @return `true` for `mod/ugc_<workshop_id>.mod` or one of the descriptor paths (case-insensitive,
 *         either slash).
 */
inline bool mod_matches(std::string_view entry, const ModIdentity& identity) {
    const std::string normal = detail::normalise_descriptor(entry);
    if (detail::all_digits(identity.workshop_id) && normal == "mod/ugc_" + identity.workshop_id + ".mod") {
        return true;
    }
    return std::ranges::any_of(identity.descriptors, [&normal](const std::string& descriptor) {
        return !descriptor.empty() && normal == detail::normalise_descriptor(descriptor);
    });
}

/**
 * @brief The decision for one dlc_load.json content.
 * @param json File content.
 * @param identity How the CAPIE mod is recognised.
 * @return Verdict with a reason.
 */
inline PlaysetVerdict evaluate_playset(std::string_view json, const ModIdentity& identity) {
    if (!identity_usable(identity)) {
        return {.state = PlaysetState::unknown, .reason = L"no CAPIE mod identity is configured"};
    }
    const auto mods = parse_enabled_mods(json);
    if (!mods) {
        return {.state = PlaysetState::unknown,
                .reason = L"dlc_load.json is not valid or has no single enabled_mods list of strings"};
    }
    for (const auto& entry : *mods) {
        if (mod_matches(entry, identity)) {
            return {.state = PlaysetState::enabled, .reason = L"CAPIE mod enabled in the playset: " + from_utf8(entry)};
        }
    }
    return {.state = PlaysetState::disabled,
            .reason = L"CAPIE mod not enabled in the playset (" + std::to_wstring(mods->size()) + L" mods enabled)"};
}

/**
 * @brief Reads dlc_load.json (shared, read-only, bounded) and decides.
 * @param path Absolute path of dlc_load.json.
 * @param identity How the CAPIE mod is recognised.
 * @return Verdict; any file problem is `unknown`.
 * @note Opened with full sharing so the Paradox Launcher and the game are never disturbed.
 */
inline PlaysetVerdict read_playset(const std::wstring& path, const ModIdentity& identity) {
    if (path.empty()) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json path is unknown"};
    }
    const Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return {.state = PlaysetState::unknown,
                .reason = L"dlc_load.json cannot be opened (error " + std::to_wstring(GetLastError()) + L"): " + path};
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < 0 ||
        std::cmp_greater(size.QuadPart, dlc_load_read_limit)) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json size is not usable: " + path};
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD actual = 0;
    if (!content.empty() &&
        (ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &actual, nullptr) == 0 ||
         actual != content.size())) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json cannot be read: " + path};
    }
    return evaluate_playset(content, identity);
}

}  // namespace capie::launcher
