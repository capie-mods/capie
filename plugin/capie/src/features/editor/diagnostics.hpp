/**
 * @file diagnostics.hpp
 * @brief Editor activation flags and the `RN_INLINE` / `RN_IEBR` diagnostic lines.
 *
 * `active` is the master switch of the editor bridge: every hook body checks it after
 * forwarding to the original and does nothing else while it is false. It becomes true
 * only at the very end of a successful `RNInitialize`.
 *
 * `diagnostic()` writes one `RN_INLINE` line about an editor event. In the debug flavor
 * the line also carries the copied text, its UTF-8 form and code points, and the strict
 * input verdict, exactly as the accepted bridge logged them. The release flavor writes
 * the event without the text: typed names are player data and are not needed to read a
 * lifecycle log. `bridge_rejected()` writes the `RN_IEBR ... guard=...; no retry` line of
 * a dropped action (error channel).
 *
 * All output goes through `core/log.hpp`; nothing here touches game memory.
 */
#pragma once
#include "../../core/log.hpp"
#include "../../engine/widgets.hpp"
#include "input_codes.hpp"
#include "schema.hpp"

#include <atomic>
#include <cstdio>
#include <string_view>

namespace capie::editor {

using core::logging::debug_diagnostics;
using core::logging::write_log;
using engine::InputSnapshot;
using engine::max_codepoints;

/// @brief Master switch: set at the end of a successful initialization.
inline std::atomic<bool> active{false};
/// @brief 0 = never initialized, 1 = initialization attempted (a failed attempt is final).
inline std::atomic<unsigned> init_state{0};
/// @brief Monotonic id shared by related log lines of one observation.
inline std::atomic<unsigned long> observation_id{0};

/// @brief Size of one `RN_INLINE` line buffer.
inline constexpr std::size_t inline_line_capacity = 3072;
/// @brief Size of a short `RN_IEBR` line buffer.
inline constexpr std::size_t short_line_capacity = 256;
/// @brief Escaped UTF-8 needs at most 6 bytes per code point (`\u00XX`).
inline constexpr std::size_t escaped_bytes_per_point = 6;
/// @brief Lower-case hex digits used in control-character escapes.
inline constexpr char escape_hex_digits[] = "0123456789abcdef";
/// @brief A code point renders as at most 9 characters in `codepoints=[...]` (" 10FFFF" + slack).
inline constexpr std::size_t codepoint_chars = 9;

/**
 * @brief Allocates the next observation id.
 * @return New id (starts at 1).
 */
inline unsigned long next_observation_id() noexcept {
    return observation_id.fetch_add(1) + 1;
}

/**
 * @brief Escapes code points as UTF-8 for a quoted log field (`"` and `\` escaped, controls as `\u00XX`).
 * @param points Code points.
 * @param count Number of points.
 * @param out Destination (at least `count * 6 + 1` bytes).
 * @param size Receives the byte count.
 * @return `false` on an invalid scalar.
 */
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers): UTF-8 encoding table
inline bool encode_utf8(const std::uint32_t* points, std::size_t count, char* out, std::size_t& size) noexcept {
    size = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto cp = points[i];
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            return false;
        }
        if (cp == '"' || cp == '\\') {
            out[size++] = '\\';
            out[size++] = static_cast<char>(cp);
        } else if (cp < 0x20 || cp == 0x7f) {
            out[size++] = '\\';
            out[size++] = 'u';
            out[size++] = '0';
            out[size++] = '0';
            out[size++] = escape_hex_digits[(cp >> 4) & 15];
            out[size++] = escape_hex_digits[cp & 15];
        } else if (cp < 0x80) {
            out[size++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            out[size++] = static_cast<char>(0xc0 | (cp >> 6));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[size++] = static_cast<char>(0xe0 | (cp >> 12));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else {
            out[size++] = static_cast<char>(0xf0 | (cp >> 18));
            out[size++] = static_cast<char>(0x80 | ((cp >> 12) & 63));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        }
    }
    out[size] = '\0';
    return true;
}

/**
 * @brief UTF-8 byte length of a valid code point.
 * @param cp Code point.
 * @return 1..4.
 */
constexpr std::size_t utf8_length(std::uint32_t cp) noexcept {
    if (cp < 0x80) {
        return 1;
    }
    if (cp < 0x800) {
        return 2;
    }
    return cp < 0x10000 ? 3 : 4;
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Writes one `RN_INLINE` event line.
 * @param id Observation id.
 * @param event Event name (stable; QA tooling greps it).
 * @param field Field index.
 * @param epoch Opening epoch.
 * @param generation Lifecycle generation.
 * @param text Copied text, or null for a line without text.
 * @param producer Thread that copied the text.
 * @note Release flavor: the text, code points and input verdict are omitted (player data).
 */
inline void diagnostic(unsigned long id, const char* event, std::size_t field, std::uint64_t epoch,
                       std::uint64_t generation, const InputSnapshot* text = nullptr, unsigned long producer = 0) noexcept {
    char line[inline_line_capacity]{};
    if (text == nullptr || !debug_diagnostics) {
        static_cast<void>(
            std::snprintf(line, sizeof(line),
                          "RN_INLINE id=%lu event=%s field=%s epoch=%llu generation=%llu thread=%lu; no Apply\r\n", id,
                          event, fields[field].edit, static_cast<unsigned long long>(epoch),
                          static_cast<unsigned long long>(generation), core::platform::thread_id()));
    } else {
        char encoded[(max_codepoints * escaped_bytes_per_point) + 1]{};
        std::size_t escaped = 0;
        const bool valid = encode_utf8(text->points, text->size, encoded, escaped);
        char32_t points[max_codepoints]{};
        for (std::size_t i = 0; i < text->size; ++i) {
            points[i] = static_cast<char32_t>(text->points[i]);
        }
        const auto converted = encode_field(std::u32string_view(points, text->size));
        char codepoints[(max_codepoints * codepoint_chars) + 1]{};
        std::size_t position = 0;
        std::size_t utf8_bytes = 0;
        for (std::size_t i = 0; i < text->size; ++i) {
            position +=
                static_cast<std::size_t>(std::snprintf(codepoints + position, sizeof(codepoints) - position, "%s%04X",
                                                       i != 0 ? " " : "", static_cast<unsigned>(text->points[i])));
            if (valid) {
                utf8_bytes += utf8_length(text->points[i]);
            }
        }
        static_cast<void>(std::snprintf(
            line, sizeof(line),
            "RN_INLINE id=%lu event=%s field=%s epoch=%llu generation=%llu producer_thread=%lu "
            "consumer_thread=%lu utf32_len=%zu utf8_valid=%u utf8_bytes=%zu escaped_bytes=%zu text=\"%s\" "
            "codepoints=[%s] strict_input_ok=%u input_error=%u error_index=%zu; no Apply\r\n",
            id, event, fields[field].edit, static_cast<unsigned long long>(epoch),
            static_cast<unsigned long long>(generation), producer, core::platform::thread_id(), text->size, valid ? 1U : 0U,
            utf8_bytes, valid ? escaped : 0, valid ? encoded : "<invalid scalar>", codepoints,
            converted.value ? 1U : 0U, static_cast<unsigned>(converted.error), converted.error_index));
    }
    write_log(line);
}

/**
 * @brief Writes the line of a dropped or refused action (error channel).
 * @param id Observation id.
 * @param reason Guard name.
 */
inline void bridge_rejected(unsigned long id, const char* reason) noexcept {
    char line[short_line_capacity]{};
    static_cast<void>(std::snprintf(line, sizeof(line), "RN_IEBR id=%lu guard=%s; no retry\r\n", id, reason));
    write_log(line);
}

}  // namespace capie::editor
