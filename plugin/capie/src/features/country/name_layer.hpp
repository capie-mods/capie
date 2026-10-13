/**
 * @file name_layer.hpp
 * @brief The player-given country name as its own layer (API level 4): reading it from country variables.
 *
 * A mod stores the name a player gave a country in that country's own variables, with plain
 * script effects, so the game saves and synchronises it like any other variable:
 *
 *     capie_cn_n    array of character codes of the name       (1..32 codes, CONTRACT section 7)
 *     capie_cn_a    array of character codes of the adjective  (1..32 codes)
 *     capie_cn_ver  1 = the layer is set (format 1); absent = no layer
 *     capie_cn_rev  set last by every change; tells CAPIE to refresh the names (value unused)
 *
 * The cosmetic tag is not touched, so flag and colour stay the game's. Priority on screen:
 * layer > current cosmetic tag > base name (`name_layer_hooks.hpp` implements it).
 *
 * This file reads those variables from game memory and turns them into the localisation key
 * `RN1|<name>|<adjective>|` that the existing country hooks already answer and cache
 * (`country_names.hpp`). It reproduces the game's own variable lookup (`BC7E80` -> `BC4960`,
 * pinned in `name_layer_pins.hpp`):
 *
 *     container = [country + 0x218]
 *     table     = container + 0x10: entries [table + 8], int32 mask [table + 0x14],
 *                 byte extra slots [table + 0x18]
 *     bucket    = (int64)(int32)FNV-1a(name) & mask; entries are 0x30 bytes; byte +4 is the
 *                 probe distance (0 = empty); the key is an MSVC string at +8; the value an
 *                 int64 at +0x28 in fixed point (x 100000)
 *     probe     = robin hood: step k (from 1) stops when entry.distance < k
 *
 * Arrays are ordinary variables named `name^0`, `name^1`, ... and `name^num` (seen in saves).
 *
 * Fail-closed: anything unreadable or out of range (a missing element, a fraction, a code
 * outside 1..264, a length outside 1..32, a text the RN1 guard refuses) means "no layer"; the
 * game then shows the cosmetic tag or the base name, as without CAPIE. Nothing here writes,
 * allocates or calls the game. Templated over the memory policy so offline tests use fakes.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../editor/input_codes.hpp"
#include "country_protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace capie::country::layer {

using core::uptr;

/// @brief Array of the name's character codes.
inline constexpr std::string_view name_array = "capie_cn_n";
/// @brief Array of the adjective's character codes.
inline constexpr std::string_view adjective_array = "capie_cn_a";
/// @brief Format variable; `1` while the layer is set.
inline constexpr std::string_view version_variable = "capie_cn_ver";
/// @brief Commit variable; every change of the layer ends with `set_variable = { capie_cn_rev = 1 }`.
inline constexpr char commit_variable[] = "capie_cn_rev";
/// @brief Suffix of an array's element count.
inline constexpr std::string_view count_suffix = "^num";
/// @brief Fixed-point scale of HOI4 variables.
inline constexpr std::int64_t fixed_point = 100000;
/// @brief The only format version this build reads.
inline constexpr std::int64_t format_version = 1;
/// @brief Most codes per text.
inline constexpr std::size_t max_codes = editor::max_field_length;
/// @brief Highest character code (extended alphabet).
inline constexpr std::int64_t max_code = static_cast<std::int64_t>(editor::extended_alphabet_size);
/// @brief Longest probe CAPIE follows (the engine's probe distance is one byte).
inline constexpr std::size_t max_probe = 255;
/// @brief Largest bucket mask accepted (sanity bound, 16 Mi buckets).
inline constexpr std::int64_t max_mask = 0xFFFFFF;
/// @brief Largest key capacity accepted in an entry (sanity bound).
inline constexpr std::uint64_t max_key_capacity = 1048576;
/// @brief Longest variable name CAPIE looks up (`capie_cn_n^num` is 14).
inline constexpr std::size_t max_variable_name = 32;
/// @brief Longest key `RN1|` + 128 + `|` + 128 + `|`.
inline constexpr std::size_t max_key_size = protocol_prefix.size() + (2 * max_field_bytes) + 2;
/// @brief FNV-1a offset basis (BC4981).
inline constexpr std::uint32_t fnv_basis = 0x811C9DC5U;
/// @brief FNV-1a prime (BC499B).
inline constexpr std::uint32_t fnv_prime = 0x01000193U;
/// @brief Inline capacity of an MSVC string.
inline constexpr std::uint64_t sso_capacity = 15;

/**
 * @brief The engine's 32-bit FNV-1a over the variable name bytes (BC4960).
 * @param name Name bytes.
 * @return Hash.
 */
constexpr std::uint32_t fnv1a(std::string_view name) noexcept {
    std::uint32_t hash = fnv_basis;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * fnv_prime;
    }
    return hash;
}

/// @brief The variable table of one container, copied out of game memory.
struct Table {
    uptr entries{};         ///< First entry (0 = no variables).
    std::int64_t mask{};    ///< Bucket mask (sign-extended int32, as `movsxd r12,[rcx+14]`).
    std::uint64_t slots{};  ///< Readable entries: mask + extra + 1 (end of `BC7EA3`).
};

/// @brief Raw entry layout (0x30 bytes).
struct Entry {
    std::uint32_t hash_or_pad;        ///< +0x00: not used by the lookup.
    std::uint8_t probe;               ///< +0x04: probe distance, 0 = empty.
    std::array<std::uint8_t, 3> pad;  ///< +0x05.
    union {
        std::array<char, 16> small;  ///< Inline key bytes (capacity <= 15).
        uptr pointer;                ///< Heap key bytes.
    } key;                           ///< +0x08.
    std::uint64_t key_size;          ///< +0x18.
    std::uint64_t key_capacity;      ///< +0x20.
    std::int64_t value;              ///< +0x28.
};
static_assert(sizeof(Entry) == core::off::variable_entry_size &&
              offsetof(Entry, probe) == core::off::variable_entry_probe &&
              offsetof(Entry, key) == core::off::variable_entry_key &&
              offsetof(Entry, value) == core::off::variable_entry_value);

/**
 * @brief Reads the variable table of a container.
 * @param mem Memory policy.
 * @param container Container address (`[country + 0x218]`).
 * @param out Receives the table.
 * @return `false` when unreadable or implausible (mask negative, not 2^n - 1, or too large).
 */
template <class M>
bool read_table(const M& mem, uptr container, Table& out) noexcept {
    out = Table{};
    std::int32_t mask = 0;
    std::uint8_t extra = 0;
    if (!core::rd(mem, container, core::off::variables_entries, out.entries) ||
        !core::rd(mem, container, core::off::variables_mask, mask) ||
        !core::rd(mem, container, core::off::variables_extra, extra)) {
        return false;
    }
    if (mask < 0 || mask > max_mask || ((static_cast<std::int64_t>(mask) + 1) & mask) != 0) {
        return false;
    }
    out.mask = mask;
    out.slots = static_cast<std::uint64_t>(mask) + extra + 1;
    return true;
}

/// @brief Result of a variable lookup.
enum class Lookup : unsigned {
    found,       ///< Present; value set.
    absent,      ///< Not in the table.
    unreadable,  ///< Memory could not be read or the table is inconsistent.
};

/**
 * @brief Compares an entry's key with a name.
 * @param mem Memory policy.
 * @param entry Copied entry.
 * @param name Name.
 * @param equal Receives the result.
 * @return `false` when the key bytes could not be read or the key is implausible.
 */
template <class M>
bool key_equals(const M& mem, const Entry& entry, std::string_view name, bool& equal) noexcept {
    if (entry.key_size > entry.key_capacity || entry.key_capacity > max_key_capacity) {
        return false;
    }
    equal = false;
    if (entry.key_size != name.size()) {
        return true;
    }
    std::array<char, max_variable_name> bytes{};
    if (name.size() > bytes.size()) {
        return true;
    }
    if (entry.key_capacity <= sso_capacity) {
        std::memcpy(bytes.data(), entry.key.small.data(), name.size());
    } else if (!mem.read(entry.key.pointer, bytes.data(), name.size())) {
        return false;
    }
    equal = std::memcmp(bytes.data(), name.data(), name.size()) == 0;
    return true;
}

/**
 * @brief Finds a variable exactly as `BC4960` does (bounded by `slots` and `max_probe`).
 * @param mem Memory policy.
 * @param table Table of the container.
 * @param name Variable name (at most `max_variable_name` bytes).
 * @param value Receives the raw fixed-point value when found.
 * @return Lookup result.
 */
template <class M>
Lookup find_variable(const M& mem, const Table& table, std::string_view name, std::int64_t& value) noexcept {
    if (table.entries == 0 || table.slots == 0) {
        return Lookup::absent;
    }
    if (name.size() > max_variable_name) {
        return Lookup::absent;
    }
    const auto hash = static_cast<std::int64_t>(static_cast<std::int32_t>(fnv1a(name)));
    auto index = static_cast<std::uint64_t>(hash & table.mask);
    for (std::size_t distance = 1; distance <= max_probe; ++distance, ++index) {
        if (index >= table.slots) {
            return Lookup::unreadable;  // the engine never probes past its extra slots
        }
        uptr address = 0;
        if (index > (core::uptr_max - table.entries) / core::off::variable_entry_size ||
            !core::add_offset(table.entries, index * core::off::variable_entry_size, address)) {
            return Lookup::unreadable;
        }
        Entry entry{};
        if (!mem.read(address, &entry, sizeof(entry))) {
            return Lookup::unreadable;
        }
        if (entry.probe < distance) {
            return Lookup::absent;
        }
        bool equal = false;
        if (!key_equals(mem, entry, name, equal)) {
            return Lookup::unreadable;
        }
        if (equal) {
            value = entry.value;
            return Lookup::found;
        }
    }
    return Lookup::unreadable;
}

/// @brief Why a country has no usable layer.
enum class Status : unsigned {
    valid,       ///< The layer is set and valid.
    absent,      ///< No `capie_cn_ver` (or no variables at all): no layer.
    invalid,     ///< `capie_cn_ver` present but a value is out of range: treated as no layer.
    unreadable,  ///< Memory or table could not be read: treated as no layer.
};

/// @brief The layer of one country: the RN1 key plus counts for diagnostics (no text in logs).
struct Layer {
    Status status{Status::absent};         ///< Result.
    std::array<char, max_key_size> key{};  ///< `RN1|<name>|<adjective>|` when valid.
    std::size_t key_size{};                ///< Bytes used in `key`.
    std::int64_t version_raw{};            ///< Raw `capie_cn_ver` (fixed point) when present.
    std::size_t name_points{};             ///< Code points of the name.
    std::size_t adjective_points{};        ///< Code points of the adjective.

    /// @brief The key. @return View (empty unless valid).
    [[nodiscard]] std::string_view view() const noexcept { return {key.data(), key_size}; }
};

/// @brief Bounded byte builder for the key.
struct KeyWriter {
    Layer& layer;     ///< Target.
    bool overflow{};  ///< Set when an append did not fit.

    /// @brief Appends bytes. @param bytes Bytes.
    void add(std::string_view bytes) noexcept {
        if (overflow || bytes.size() > layer.key.size() - layer.key_size) {
            overflow = true;
            return;
        }
        std::memcpy(layer.key.data() + layer.key_size, bytes.data(), bytes.size());
        layer.key_size += bytes.size();
    }

    /**
     * @brief Appends one code point as UTF-8.
     * @param point Unicode scalar from the alphabet table.
     */
    void add_point(char32_t point) noexcept {
        // UTF-8 encoding table (RFC 3629, section 3); the alphabet has no scalar above U+FFFF.
        // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
        std::array<char, 3> bytes{};
        std::size_t count = 0;
        if (point < 0x80) {
            bytes[count++] = static_cast<char>(point);
        } else if (point < 0x800) {
            bytes[count++] = static_cast<char>(0xC0U | (point >> 6U));
            bytes[count++] = static_cast<char>(0x80U | (point & 0x3FU));
        } else {
            bytes[count++] = static_cast<char>(0xE0U | (point >> 12U));
            bytes[count++] = static_cast<char>(0x80U | ((point >> 6U) & 0x3FU));
            bytes[count++] = static_cast<char>(0x80U | (point & 0x3FU));
        }
        // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
        add(std::string_view(bytes.data(), count));
    }
};

/**
 * @brief Builds `<array>^<suffix>` into a buffer.
 * @param array Array name.
 * @param suffix Index digits or `num`.
 * @param out Buffer.
 * @return View of the name.
 */
inline std::string_view element_name(std::string_view array, std::string_view suffix,
                                     std::array<char, max_variable_name>& out) noexcept {
    std::size_t size = 0;
    for (const auto part : {array, std::string_view("^"), suffix}) {
        for (const char c : part) {
            if (size < out.size()) {
                out[size++] = c;
            }
        }
    }
    return {out.data(), size};
}

/**
 * @brief Converts a raw fixed-point value to an integer.
 * @param raw Raw value.
 * @param out Receives `raw / 100000`.
 * @return `false` for a fraction.
 */
constexpr bool whole(std::int64_t raw, std::int64_t& out) noexcept {
    if (raw % fixed_point != 0) {
        return false;
    }
    out = raw / fixed_point;
    return true;
}

/**
 * @brief Decodes one code array and appends its UTF-8 text.
 * @param mem Memory policy.
 * @param table Variable table.
 * @param array Array name.
 * @param writer Key writer.
 * @param points Receives the number of code points.
 * @return `valid`, `invalid` or `unreadable`.
 */
template <class M>
Status append_array(const M& mem, const Table& table, std::string_view array, KeyWriter& writer,
                    std::size_t& points) noexcept {
    std::array<char, max_variable_name> name{};
    std::int64_t raw = 0;
    const auto count_lookup = find_variable(mem, table, element_name(array, "num", name), raw);
    if (count_lookup == Lookup::unreadable) {
        return Status::unreadable;
    }
    std::int64_t count = 0;
    if (count_lookup == Lookup::absent || !whole(raw, count) || count < 1 || std::cmp_greater(count, max_codes)) {
        return Status::invalid;
    }
    const auto start = writer.layer.key_size;
    for (std::int64_t index = 0; index < count; ++index) {
        std::array<char, 3> digits{};
        std::size_t used = 0;
        constexpr std::int64_t ten = 10;
        if (index >= ten) {
            digits[used++] = static_cast<char>('0' + (index / ten));
        }
        digits[used++] = static_cast<char>('0' + (index % ten));
        const auto lookup = find_variable(mem, table, element_name(array, {digits.data(), used}, name), raw);
        if (lookup == Lookup::unreadable) {
            return Status::unreadable;
        }
        std::int64_t code = 0;
        if (lookup == Lookup::absent || !whole(raw, code) || code < 1 || code > max_code) {
            return Status::invalid;
        }
        writer.add_point(editor::extended_alphabet[static_cast<std::size_t>(code - 1)]);
    }
    if (writer.overflow ||
        !valid_text(std::string_view(writer.layer.key.data() + start, writer.layer.key_size - start))) {
        return Status::invalid;
    }
    points = static_cast<std::size_t>(count);
    return Status::valid;
}

/**
 * @brief Reads the layer of a country.
 * @param mem Memory policy.
 * @param country Country object address.
 * @return The layer; `status != valid` means "no layer" for display.
 */
template <class M>
Layer read_layer(const M& mem, uptr country) noexcept {
    Layer layer{};
    uptr container = 0;
    if (!core::rd(mem, country, core::off::country_variables, container)) {
        layer.status = Status::unreadable;
        return layer;
    }
    if (container == 0) {
        return layer;  // no variables at all
    }
    Table table{};
    if (!read_table(mem, container, table)) {
        layer.status = Status::unreadable;
        return layer;
    }
    const auto version = find_variable(mem, table, version_variable, layer.version_raw);
    if (version != Lookup::found) {
        layer.status = version == Lookup::absent ? Status::absent : Status::unreadable;
        return layer;
    }
    std::int64_t format = 0;
    if (!whole(layer.version_raw, format) || format != format_version) {
        layer.status = Status::invalid;
        return layer;
    }
    KeyWriter writer{layer};
    writer.add(protocol_prefix);
    auto status = append_array(mem, table, name_array, writer, layer.name_points);
    writer.add(std::string_view(&field_delimiter, 1));
    if (status == Status::valid) {
        status = append_array(mem, table, adjective_array, writer, layer.adjective_points);
    }
    writer.add(std::string_view(&field_delimiter, 1));
    if (status == Status::valid && writer.overflow) {
        status = Status::invalid;
    }
    layer.status = status;
    if (status != Status::valid) {
        layer.key_size = 0;
    }
    return layer;
}

/**
 * @brief Short name of a status for log lines.
 * @param status Status.
 * @return Text.
 */
constexpr const char* status_name(Status status) noexcept {
    switch (status) {
        case Status::valid:
            return "valid";
        case Status::absent:
            return "absent";
        case Status::invalid:
            return "invalid";
        case Status::unreadable:
            return "unreadable";
    }
    return "?";
}

}  // namespace capie::country::layer
