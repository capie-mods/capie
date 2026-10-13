/**
 * @file public_schema.hpp
 * @brief Record names, limits and sentinel codes of the public editor contract (API level 2).
 *
 * Grammar editors (see `public_names.hpp`) receive these temporary records in their Apply
 * command, as `plugin/capie/CONTRACT.md` section 5 defines them:
 *   `capie_cap`, `capie_ver`, `capie_epoch`, `capie_nf`, and per field n = 1..4
 *   `capie_f<n>_err`, `capie_f<n>_at`, `capie_f<n>_len`, `capie_f<n>_0 .. _31`.
 * Every name fits the 15-byte short-string layout the transport builds directly. Any
 * incoming record whose name starts with `capie_` is removed before CAPIE adds its own.
 *
 * Rename Anything's own editors keep the legacy names of `schema.hpp`; nothing here
 * applies to them. Data only.
 */
#pragma once
#include "../../core/game_build.hpp"
#include "public_names.hpp"
#include "schema.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace capie::editor {

/// @brief Prefix every public record name starts with; incoming records with it are removed.
inline constexpr std::string_view public_reserved_prefix = "capie_";
/// @brief API level record.
inline constexpr char public_key_cap[] = "capie_cap";
/// @brief Build number record (multiplayer handshake).
inline constexpr char public_key_ver[] = "capie_ver";
/// @brief Opening epoch record (or `-code` for a sentinel).
inline constexpr char public_key_epoch[] = "capie_epoch";
/// @brief Number of transported fields.
inline constexpr char public_key_fields[] = "capie_nf";
/// @brief Format of a field header record: field number, suffix.
inline constexpr char public_field_header_format[] = "capie_f%zu_%s";
/// @brief Format of a field code record: field number, slot.
inline constexpr char public_field_slot_format[] = "capie_f%zu_%u";
/// @brief Field header suffix: InputError code.
inline constexpr char public_suffix_err[] = "err";
/// @brief Field header suffix: 1-based position of the first rejected character.
inline constexpr char public_suffix_at[] = "at";
/// @brief Field header suffix: number of characters.
inline constexpr char public_suffix_len[] = "len";

/// @brief Header records of a payload Apply: cap, ver, epoch, field count.
inline constexpr std::size_t public_header_records = 4;
/// @brief Records of a sentinel Apply: cap, ver, epoch.
inline constexpr std::size_t public_sentinel_records = 3;
/// @brief Records per field: err, at, len and 32 codes.
inline constexpr std::size_t public_records_per_field = 3 + code_slots;
/// @brief Most records one public Apply adds (144).
inline constexpr std::size_t max_public_records =
    public_header_records + (grammar::max_fields * public_records_per_field);
/// @brief Most records any own or public Apply adds (sizes the dispatch frame).
inline constexpr std::size_t max_frame_records =
    max_public_records > max_transport_records ? max_public_records : max_transport_records;

/// @brief Longest record name the public transport builds (`capie_epoch`, `capie_f4_len`).
inline constexpr std::size_t max_public_key = 12;
static_assert(sizeof(public_key_epoch) - 1 <= max_public_key && max_public_key <= max_record_key);

/// @brief Most grammar editors registered in one game session.
inline constexpr std::size_t max_public_editors = 32;
/// @brief Most grammar editors one mod id can register.
inline constexpr std::size_t max_editors_per_mod = 8;

/// @brief API level sent as `capie_cap`.
inline constexpr std::uint64_t public_api_level = core::capie_api_level;
/// @brief Build number sent as `capie_ver`.
inline constexpr std::uint64_t public_build_number = core::capie_build_number;

/// @brief Why a grammar Apply carries no payload (sent as `capie_epoch = -code`).
enum class PublicSentinel : unsigned {
    stale = 1,           ///< Opening not ready or changed: close and reopen.
    registry_full = 2,   ///< Session limit of grammar editors reached.
    layout_invalid = 3,  ///< A fifth field, or a gap between fields.
};

}  // namespace capie::editor
