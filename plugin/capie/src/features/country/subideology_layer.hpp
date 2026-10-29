/**
 * @file subideology_layer.hpp
 * @brief The ruling-party ideology name layer (API level 8): its variables and the government it is bound to.
 *
 * A mod stores the name in the country's own variables (CONTRACT.md section 20):
 *
 *     capie_sn_n    array of character codes (1..32 codes, CONTRACT section 7)
 *     capie_sn_ver  1 = the layer is set (format 1); absent = no layer
 *     capie_sn_rev  revision, set last to a new value by every change; CAPIE compares it
 *
 * What the politics panel shows on the ideology line of the ruling-party box is the localised
 * token of the ruling party's ideology object (`[party + off::party_ideology]`, a
 * `CIdeologyGroup`, token at `+off::ideology_key`; Ghidra 2026-10-29: the `CPoliticalParty`
 * constructor stores it there and the `CIdeologyGroup` constructor builds the token string
 * there). The design note took it for the sub-ideology; the sub-ideology belongs to the country
 * leader. A `Binding` is therefore the pair (ideology token, ruling leader with its id): the name
 * lapses when the ruling party or its leader (and with it the sub-ideology) changes.
 *
 * `read_ideology_layer` decodes the variables with the lookup of `name_layer.hpp`;
 * `read_binding` reads the current government through the game's chain (politics, ruling party,
 * its ideology, the leader chain of `portrait_layer.hpp`), checking each object's vtable.
 * Pure functions templated over the memory policy; no writes, no game calls.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "name_layer.hpp"
#include "portrait_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace capie::country::subideology {

using core::uptr;
using layer::Lookup;
using layer::Status;
using layer::Table;

/// @brief Array of the name's character codes.
inline constexpr std::string_view name_array = "capie_sn_n";
/// @brief Format variable; `1` while the layer is set.
inline constexpr std::string_view version_variable = "capie_sn_ver";
/// @brief Commit variable; its value is the revision CAPIE compares.
inline constexpr char commit_variable[] = "capie_sn_rev";
/// @brief Longest ideology token compared (vanilla tokens have at most 13 characters).
inline constexpr std::size_t max_key = 63;

/// @brief The layer of one country.
struct Layer {
    layer::Layer text;    ///< Status and the UTF-8 text (`text.view()`).
    std::int64_t rev{};   ///< Raw `capie_sn_rev` (0 when absent).
};

/**
 * @brief Reads the ruling-party ideology name layer of a country.
 * @param mem Memory policy.
 * @param country Country object address.
 * @return The layer; `text.status != valid` means "no layer".
 */
template <class M>
Layer read_ideology_layer(const M& mem, uptr country) noexcept {
    Layer out{};
    Table table{};
    layer::open_layer(mem, country, version_variable, table, out.text);
    if (out.text.status != Status::valid) {
        return out;
    }
    layer::KeyWriter writer{out.text};
    out.text.status = layer::append_array(mem, table, name_array, writer, out.text.name_points);
    if (out.text.status == Status::valid) {
        std::int64_t raw = 0;
        const auto rev = layer::find_variable(mem, table, std::string_view(commit_variable), raw);
        if (rev == Lookup::unreadable) {
            out.text.status = Status::unreadable;
        } else {
            out.rev = rev == Lookup::found ? raw : 0;
        }
    }
    if (out.text.status != Status::valid) {
        out.text.key_size = 0;
        out.rev = 0;
    }
    return out;
}

/// @brief The government a name is bound to: the ruling party's ideology token and the ruling leader.
struct Binding {
    std::array<char, max_key + 1> key{};  ///< Ideology token, NUL-terminated.
    std::size_t key_size{};               ///< Its length.
    uptr leader{};                        ///< Ruling leader character (0 = the party has none).
    std::int32_t leader_id{};             ///< Its id.

    /// @brief The token. @return View.
    [[nodiscard]] std::string_view view() const noexcept { return {key.data(), key_size}; }

    /**
     * @brief Same government.
     * @param other Other binding.
     * @return `true` when token, leader and id are equal.
     */
    [[nodiscard]] bool same(const Binding& other) const noexcept {
        return view() == other.view() && leader == other.leader && leader_id == other.leader_id;
    }
};

/**
 * @brief Whether an object has a given primary vtable.
 * @param mem Memory policy.
 * @param object Object address.
 * @param expected Expected vtable address.
 * @return `true` for a readable object with that vtable.
 */
template <class M>
bool has_vtable(const M& mem, uptr object, uptr expected) noexcept {
    uptr vtable = 0;
    return object != 0 && core::rd(mem, object, 0, vtable) && vtable == expected;
}

/**
 * @brief Reads an ideology group's token (an MSVC string, bounded).
 * @param mem Memory policy.
 * @param group `CIdeologyGroup` object.
 * @param out Receives the token.
 * @return `false` when unreadable, empty or longer than `max_key`.
 */
template <class M>
bool read_token(const M& mem, uptr group, Binding& out) noexcept {
    struct Raw {
        union {
            std::array<char, 16> small;
            uptr pointer;
        } data;
        std::uint64_t size;
        std::uint64_t capacity;
    } raw{};
    if (!core::rd(mem, group, core::off::ideology_key, raw) || raw.size == 0 || raw.size > max_key ||
        raw.size > raw.capacity) {
        return false;
    }
    if (raw.capacity <= layer::sso_capacity) {
        std::memcpy(out.key.data(), raw.data.small.data(), static_cast<std::size_t>(raw.size));
    } else if (!mem.read(raw.data.pointer, out.key.data(), static_cast<std::size_t>(raw.size))) {
        return false;
    }
    out.key_size = static_cast<std::size_t>(raw.size);
    out.key[out.key_size] = '\0';
    return true;
}

/**
 * @brief Reads the government of a country: ruling party's ideology token and ruling leader.
 * @param mem Memory policy.
 * @param image Image base (vtables).
 * @param country Country object.
 * @param out Receives the binding.
 * @return `false` when the country has no ruling party or any object of the chain is unreadable or of another class.
 */
template <class M>
bool read_binding(const M& mem, uptr image, uptr country, Binding& out) noexcept {
    out = Binding{};
    uptr politics = 0;
    uptr party = 0;
    uptr group = 0;
    if (!core::rd(mem, country, core::off::country_politics, politics) || politics == 0 ||
        !core::rd(mem, politics, core::off::politics_ruling_party, party) ||
        !has_vtable(mem, party, image + core::rva::vt_political_party) ||
        !core::rd(mem, party, core::off::party_ideology, group) ||
        !has_vtable(mem, group, image + core::rva::vt_ideology_group) || !read_token(mem, group, out)) {
        return false;
    }
    uptr leader = 0;
    if (!portrait::ruling_leader(mem, image, country, leader)) {
        return false;
    }
    out.leader = leader;
    return leader == 0 || core::rd(mem, leader, core::off::character_id, out.leader_id);
}

}  // namespace capie::country::subideology
