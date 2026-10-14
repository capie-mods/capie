/**
 * @file flag_layer.hpp
 * @brief The player-chosen country flag as its own layer (API level 6): reading it from country variables.
 *
 * A mod stores the chosen flag file in the country's own variables (CONTRACT.md section 17):
 *
 *     capie_cf_ver      1 = the layer is set (format 1); absent = no layer
 *     capie_cf_len      characters of the file stem (file name without `.tga`), 1..63
 *     capie_cf_s        the stem, three characters per element: c0 * 4096 + c1 * 64 + c2
 *     capie_cf_cos_len  snapshot of the cosmetic tag at the choice: absent = none, 0 = no tag, 1..63
 *     capie_cf_cos      that cosmetic tag, packed like the stem
 *     capie_cf_rev      set last by every change (refresh trigger, value unused)
 *
 * File-name alphabet: 1..26 `a`..`z`, 27..52 `A`..`Z`, 53..62 `0`..`9`, 63 `_`. Path
 * characters cannot be expressed, so a stem never leaves the flag folders.
 *
 * `read_flag_layer` decodes the variables with the variable lookup of `name_layer.hpp`;
 * `flag_shown` applies the cosmetic-tag gate: a snapshot that no longer matches the
 * country's current cosmetic tag means "no layer" (the game's flag shows at once; the mod's
 * daily script resets the state). Fail-closed: anything unreadable or out of range is "no layer".
 * Pure functions templated over the memory policy; no writes, no game calls.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "name_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace capie::country::flag {

using core::uptr;
using layer::Lookup;
using layer::Status;
using layer::Table;

/// @brief Format variable; `1` while the layer is set.
inline constexpr std::string_view version_variable = "capie_cf_ver";
/// @brief Length of the stem.
inline constexpr std::string_view length_variable = "capie_cf_len";
/// @brief Packed stem array.
inline constexpr std::string_view stem_array = "capie_cf_s";
/// @brief Length of the cosmetic snapshot (absent = no snapshot).
inline constexpr std::string_view cosmetic_length_variable = "capie_cf_cos_len";
/// @brief Packed cosmetic snapshot array.
inline constexpr std::string_view cosmetic_array = "capie_cf_cos";
/// @brief Commit variable; every change ends with `set_variable = { capie_cf_rev = 1 }`.
inline constexpr char commit_variable[] = "capie_cf_rev";
/// @brief Longest stem or cosmetic tag in characters.
inline constexpr std::size_t max_chars = 63;
/// @brief Characters per element.
inline constexpr std::size_t chars_per_element = 3;
/// @brief Base of the packing (6 bits per character).
inline constexpr std::int64_t pack_base = 64;
/// @brief Largest element value (three 6-bit characters).
inline constexpr std::int64_t max_element = (pack_base * pack_base * pack_base) - 1;
/// @brief Highest character code (`_`).
inline constexpr std::int64_t max_char_code = 63;

/**
 * @brief Character of a file-name code.
 * @param code 1..63.
 * @return The character, or 0 for an invalid code.
 */
constexpr char char_of(std::int64_t code) noexcept {
    // The table of CONTRACT.md 17.3; the numbers are the alphabet's ranges.
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    if (code >= 1 && code <= 26) {
        return static_cast<char>('a' + (code - 1));
    }
    if (code >= 27 && code <= 52) {
        return static_cast<char>('A' + (code - 27));
    }
    if (code >= 53 && code <= 62) {
        return static_cast<char>('0' + (code - 53));
    }
    return code == 63 ? '_' : '\0';
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
}

/**
 * @brief Code of a file-name character (the inverse of `char_of`).
 * @param c Character.
 * @return 1..63, or 0 when `c` is outside the alphabet.
 */
constexpr std::int64_t code_of(char c) noexcept {
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    if (c >= 'a' && c <= 'z') {
        return 1 + (c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return 27 + (c - 'A');
    }
    if (c >= '0' && c <= '9') {
        return 53 + (c - '0');
    }
    return c == '_' ? 63 : 0;
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
}

/// @brief A decoded file-name text (stem or cosmetic tag).
struct Text {
    std::array<char, max_chars + 1> bytes{};  ///< Characters, NUL-terminated.
    std::size_t size{};                       ///< Length.

    /// @brief The text. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), size}; }
};

/// @brief The flag layer of one country.
struct Layer {
    Status status{Status::absent};  ///< Result of reading the variables.
    Text stem;                      ///< File stem when valid.
    bool has_snapshot{};            ///< `capie_cf_cos_len` present.
    Text snapshot;                  ///< Cosmetic tag at the choice (empty = it had none).
    std::int64_t version_raw{};     ///< Raw `capie_cf_ver` (diagnostics).
};

/**
 * @brief Decodes one packed text: `<length_name>` and `<array>^0..`.
 * @param mem Memory policy.
 * @param table Variable table.
 * @param length_name Length variable.
 * @param array Array name.
 * @param allow_empty `true` for the snapshot (length 0 = no cosmetic tag).
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
    const auto elements =
        (length + static_cast<std::int64_t>(chars_per_element) - 1) / static_cast<std::int64_t>(chars_per_element);
    std::array<char, layer::max_variable_name> name{};
    std::int64_t count = 0;
    const auto num = layer::find_variable(mem, table, layer::element_name(array, "num", name), raw);
    if (num == Lookup::unreadable) {
        return Status::unreadable;
    }
    if (num == Lookup::absent || !layer::whole(raw, count) || count != elements) {
        return Status::invalid;
    }
    for (std::int64_t index = 0; index < elements; ++index) {
        std::array<char, 3> digits{};
        std::size_t used = 0;
        constexpr std::int64_t ten = 10;
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
        if (lookup == Lookup::absent || !layer::whole(raw, value) || value < 0 || value > max_element) {
            return Status::invalid;
        }
        const std::int64_t codes[chars_per_element] = {value / (pack_base * pack_base), (value / pack_base) % pack_base,
                                                       value % pack_base};
        for (std::size_t slot = 0; slot < chars_per_element; ++slot) {
            const auto position =
                (index * static_cast<std::int64_t>(chars_per_element)) + static_cast<std::int64_t>(slot);
            if (position < length) {
                const char c = char_of(codes[slot]);
                if (c == '\0') {
                    return Status::invalid;  // a 0 (or invalid code) inside the text
                }
                out.bytes[out.size++] = c;
            } else if (codes[slot] != 0) {
                return Status::invalid;  // padding must be 0
            }
        }
    }
    return Status::valid;
}

/**
 * @brief Reads the flag layer of a country.
 * @param mem Memory policy.
 * @param country Country object address.
 * @return The layer; `status != valid` means "no layer".
 */
template <class M>
Layer read_flag_layer(const M& mem, uptr country) noexcept {
    Layer out{};
    uptr container = 0;
    if (!core::rd(mem, country, core::off::country_variables, container)) {
        out.status = Status::unreadable;
        return out;
    }
    if (container == 0) {
        return out;
    }
    Table table{};
    if (!layer::read_table(mem, container, table)) {
        out.status = Status::unreadable;
        return out;
    }
    const auto version = layer::find_variable(mem, table, version_variable, out.version_raw);
    if (version != Lookup::found) {
        out.status = version == Lookup::absent ? Status::absent : Status::unreadable;
        return out;
    }
    std::int64_t format = 0;
    if (!layer::whole(out.version_raw, format) || format != layer::format_version) {
        out.status = Status::invalid;
        return out;
    }
    auto status = read_packed(mem, table, length_variable, stem_array, false, out.stem);
    if (status == Status::absent) {
        status = Status::invalid;  // a set layer needs its stem
    }
    if (status == Status::valid) {
        const auto snapshot = read_packed(mem, table, cosmetic_length_variable, cosmetic_array, true, out.snapshot);
        if (snapshot == Status::valid) {
            out.has_snapshot = true;
        } else if (snapshot != Status::absent) {
            status = snapshot;
        }
    }
    out.status = status;
    if (status != Status::valid) {
        out.stem = Text{};
        out.snapshot = Text{};
        out.has_snapshot = false;
    }
    return out;
}

/**
 * @brief Reads a country's current cosmetic tag (bounded; longer or unreadable = failure).
 * @param mem Memory policy.
 * @param country Country object.
 * @param out Receives the bytes (up to `max_chars`).
 * @param too_long Set when the tag is longer than `max_chars` (cannot match any snapshot).
 * @return `false` when the string could not be read.
 */
template <class M>
bool read_cosmetic(const M& mem, uptr country, Text& out, bool& too_long) noexcept {
    struct Raw {
        union {
            std::array<char, 16> small;
            uptr pointer;
        } data;
        std::uint64_t size;
        std::uint64_t capacity;
    } raw{};
    out = Text{};
    too_long = false;
    if (!core::rd(mem, country, core::off::country_cosmetic_tag, raw) || raw.size > raw.capacity) {
        return false;
    }
    if (raw.size > max_chars) {
        too_long = true;
        return true;
    }
    if (raw.capacity <= layer::sso_capacity) {
        std::memcpy(out.bytes.data(), raw.data.small.data(), static_cast<std::size_t>(raw.size));
    } else if (raw.size != 0 && !mem.read(raw.data.pointer, out.bytes.data(), static_cast<std::size_t>(raw.size))) {
        return false;
    }
    out.size = static_cast<std::size_t>(raw.size);
    return true;
}

/**
 * @brief Whether a valid layer is shown, given the country's current cosmetic tag (CONTRACT 17.4).
 * @param flag Layer read from the variables.
 * @param cosmetic Current cosmetic tag.
 * @param cosmetic_too_long The current tag is longer than any snapshot.
 * @return `true` when the layer is valid and its snapshot (if any) still matches.
 */
constexpr bool flag_shown(const Layer& flag, std::string_view cosmetic, bool cosmetic_too_long) noexcept {
    if (flag.status != Status::valid) {
        return false;
    }
    if (!flag.has_snapshot) {
        return true;
    }
    return !cosmetic_too_long && flag.snapshot.view() == cosmetic;
}

}  // namespace capie::country::flag
