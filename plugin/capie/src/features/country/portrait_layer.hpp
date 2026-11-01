/**
 * @file portrait_layer.hpp
 * @brief The player-chosen leader portrait as its own layer (API level 7): reading it from character variables.
 *
 * A mod stores the chosen sprites in the ruling leader's own variables (CONTRACT.md section 18):
 *
 *     capie_pt_ver   1 = format 1, 2 = format 2 (API level 9); absent = no layer
 *     capie_pt_len   characters of the large name: 1..63 (format 1), 1..126 (format 2)
 *     capie_pt_s     the large name, three characters per element: 17.3 packing (format 1, sprite
 *                    names only) or packing B (format 2, a sprite name or a `gfx/...` texture path,
 *                    `packing_b.hpp`)
 *     capie_pt_slen  characters of the small name; absent or 0 = the game's small portrait
 *     capie_pt_ss    the small name, packed like the large one
 *     capie_pt_rev   set last by every change, on a country (refresh trigger, value unused)
 *
 * The character's variable container is inline at character+0xD8 (CCharacter::Write and
 * Read), the same container type as a country's. The ruling leader of a country is
 * found with the chain of the `set_country_leader_portrait` effect: country+0xF90
 * politics, +0xD0 ruling party, first of the leader roles at +0x70 (count +0x7C), role+0x8
 * character. Pure functions templated over the memory policy; no writes, no game calls.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "flag_layer.hpp"
#include "name_layer.hpp"
#include "packing_b.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace capie::country::portrait {

using core::uptr;
using Text = packing_b::Text;
using layer::Lookup;
using layer::Status;
using layer::Table;

/// @brief Format variable; `1` while the layer is set.
inline constexpr std::string_view version_variable = "capie_pt_ver";
/// @brief Length of the large sprite name.
inline constexpr std::string_view length_variable = "capie_pt_len";
/// @brief Packed large sprite name.
inline constexpr std::string_view large_array = "capie_pt_s";
/// @brief Length of the small sprite name (absent or 0 = the game's small portrait).
inline constexpr std::string_view small_length_variable = "capie_pt_slen";
/// @brief Packed small sprite name.
inline constexpr std::string_view small_array = "capie_pt_ss";
/// @brief Commit variable (any country scope); every change ends with `set_variable = { capie_pt_rev = 1 }`.
inline constexpr char commit_variable[] = "capie_pt_rev";
/// @brief Format 2 (API level 9): packing B, sprite names or texture paths.
inline constexpr std::int64_t format_paths = 2;
/// @brief Portrait type of a country leader (`civilian`; the effect passes edx = 0).
inline constexpr std::int32_t type_civilian = 0;
/// @brief Portrait size `large` (the effect passes r8d = 1); 0 is `small` (`ui.size_small_is_zero`).
inline constexpr std::int32_t size_large = 1;

/// @brief The portrait layer of one character.
struct Layer {
    Status status{Status::absent};  ///< Result of reading the variables.
    Text large;                     ///< Large sprite name when valid.
    Text small;                     ///< Small sprite name; empty = the game's small portrait.
    std::int64_t version_raw{};     ///< Raw `capie_pt_ver` (diagnostics).
    std::int64_t format{};          ///< 1 or 2 when valid, else 0.
};

/**
 * @brief Reads one name in the packing of a format.
 * @param mem Memory policy.
 * @param table Variable table.
 * @param format 1 (17.3 packing) or 2 (packing B).
 * @param length_name Length variable.
 * @param array Array name.
 * @param allow_empty Length 0 allowed (the small name).
 * @param out Receives the name.
 * @return Status of the read.
 */
template <class M>
Status read_name(const M& mem, const Table& table, std::int64_t format, std::string_view length_name,
                 std::string_view array, bool allow_empty, Text& out) noexcept {
    out = Text{};
    if (format == format_paths) {
        return packing_b::read_packed(mem, table, length_name, array, allow_empty, out);
    }
    flag::Text narrow{};
    const auto status = flag::read_packed(mem, table, length_name, array, allow_empty, narrow);
    if (status == Status::valid) {
        for (std::size_t i = 0; i < narrow.size; ++i) {
            out.bytes[i] = narrow.bytes[i];
        }
        out.size = narrow.size;
    }
    return status;
}

/**
 * @brief Reads the portrait layer of a character.
 * @param mem Memory policy.
 * @param character Character object address.
 * @return The layer; `status != valid` means "no layer".
 */
template <class M>
Layer read_portrait_layer(const M& mem, uptr character) noexcept {
    Layer out{};
    if (character == 0 || character > core::uptr_max - core::off::character_variables) {
        out.status = Status::unreadable;
        return out;
    }
    Table table{};
    if (!layer::read_table(mem, character + core::off::character_variables, table)) {
        out.status = Status::unreadable;
        return out;
    }
    const auto version = layer::find_variable(mem, table, version_variable, out.version_raw);
    if (version != Lookup::found) {
        out.status = version == Lookup::absent ? Status::absent : Status::unreadable;
        return out;
    }
    std::int64_t format = 0;
    if (!layer::whole(out.version_raw, format) || (format != layer::format_version && format != format_paths)) {
        out.status = Status::invalid;
        return out;
    }
    auto status = read_name(mem, table, format, length_variable, large_array, false, out.large);
    if (status == Status::absent) {
        status = Status::invalid;  // a set layer needs its large name
    }
    if (status == Status::valid) {
        const auto small = read_name(mem, table, format, small_length_variable, small_array, true, out.small);
        if (small == Status::absent) {
            out.small = Text{};
        } else if (small != Status::valid) {
            status = small;
        }
    }
    out.status = status;
    out.format = status == Status::valid ? format : 0;
    if (status != Status::valid) {
        out.large = Text{};
        out.small = Text{};
    }
    return out;
}

/**
 * @brief Whether an object is a `CCharacter` (primary vtable).
 * @param mem Memory policy.
 * @param image Image base.
 * @param object Object address.
 * @return `true` for a readable character.
 */
template <class M>
bool is_character(const M& mem, uptr image, uptr object) noexcept {
    uptr vtable = 0;
    return object != 0 && core::rd(mem, object, 0, vtable) && vtable == image + core::rva::vt_character;
}

/**
 * @brief The ruling country leader of a country (the game's chain, read only).
 * @param mem Memory policy.
 * @param image Image base.
 * @param country Country object.
 * @param character Receives the character (0 when the country has none).
 * @return `false` when the chain is unreadable or does not end in a character.
 */
template <class M>
bool ruling_leader(const M& mem, uptr image, uptr country, uptr& character) noexcept {
    character = 0;
    uptr politics = 0;
    uptr party = 0;
    if (!core::rd(mem, country, core::off::country_politics, politics) || politics == 0 ||
        !core::rd(mem, politics, core::off::politics_ruling_party, party)) {
        return false;
    }
    if (party == 0) {
        return true;  // no ruling party: no leader
    }
    std::int32_t count = 0;
    if (!core::rd(mem, party, core::off::party_leader_count, count) || count < 0) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    uptr roles = 0;
    uptr role = 0;
    uptr found = 0;
    if (!core::rd(mem, party, core::off::party_leaders, roles) || !core::rd(mem, roles, 0, role) ||
        !core::rd(mem, role, core::off::leader_role_character, found) || !is_character(mem, image, found)) {
        return false;
    }
    character = found;
    return true;
}

}  // namespace capie::country::portrait
