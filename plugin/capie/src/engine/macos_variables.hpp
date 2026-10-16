/**
 * @file macos_variables.hpp
 * @brief Bounded read-only variable-table adapter for pinned macOS HOI4 1.19.3.
 * Evidence: docs/macos/layers-map.md, native lookup 0x100b031c0.
 * No native calls, writes or allocations. Header/entry/heap reads are not an atomic snapshot.
 */
#pragma once
#include "../core/macos_game_layout.hpp"
#include "macos_string.hpp"

namespace capie::engine::macos::variables {
namespace layout = core::macos::off;

/// @brief Highest accepted bucket mask, matching the feature's existing sanity bound.
inline constexpr std::uint32_t max_mask = 0xffffff;
/// @brief Maximum CAPIE variable name bytes, matching shared country/faction/flag/portrait readers.
inline constexpr std::size_t max_variable_name = 32;
/// @brief The engine uses one byte for probe distance; never follow more than 255 entries.
inline constexpr std::size_t max_probe = 255;
/// @brief Mac entry size, independently observed at native lookup 0x100b032c2.
inline constexpr std::size_t entry_size = layout::variable_entry_size;

/// @brief Validated table header copied from a variable container.
struct Table {
    std::uintptr_t entries{};  ///< Container+0x10, zero means no variables.
    std::uint32_t mask{};      ///< Container+0x1c, must be 2^n-1 within max_mask.
    std::uint64_t slots{};     ///< mask + extra byte at container+0x20 + 1.
};

/// @brief Distinguish absence from an unsafe read; only found updates the caller's value.
enum class Lookup { found, absent, unreadable };

/** @brief Native 32-bit FNV-1a over exact key bytes. @param name Bytes. @return Hash. */
constexpr std::uint32_t fnv1a(std::string_view name) noexcept {
    std::uint32_t hash = 0x811c9dc5;
    for (const char byte : name) {
        hash = (hash ^ static_cast<unsigned char>(byte)) * 0x01000193;
    }
    return hash;
}

/**
 * @brief Read and validate the table header; resets out on failure.
 * @tparam M Bounded memory policy with read(address, out, bytes).
 * @param mem Memory policy. @param container Variable container address. @param out Header output.
 * @return True for a readable plausible header, including a null entries pointer.
 */
template <class M>
bool read_table(const M& mem, std::uintptr_t container, Table& out) noexcept {
    out = {};
    std::array<unsigned char, layout::variables_extra + 1> header{};
    if (!detail::valid_range(container, header.size()) || !mem.read(container, header.data(), header.size())) {
        return false;
    }
    Table table;
    std::memcpy(&table.entries, header.data() + layout::variables_entries, sizeof(table.entries));
    std::memcpy(&table.mask, header.data() + layout::variables_mask, sizeof(table.mask));
    if (table.mask > max_mask || ((table.mask + 1) & table.mask) != 0) {
        return false;
    }
    table.slots = static_cast<std::uint64_t>(table.mask) + header[layout::variables_extra] + 1;
    out = table;
    return true;
}

namespace impl {
/// @brief Supplies copied key header bytes to the existing string decoder; heap reads use memory policy.
template <class M>
struct EntryMemory {
    const M& memory;                                     ///< Underlying safe reader.
    std::uintptr_t key_address;                          ///< Native entry+8, used only as an address identity.
    const std::array<unsigned char, entry_size>& entry;  ///< Stable copied entry.

    /** @brief Read a copied header or actual heap content. @return True on a complete read. */
    bool read(std::uintptr_t address, void* out, std::size_t bytes) const noexcept {
        if (address == key_address && bytes == 24) {
            std::memcpy(out, entry.data() + layout::variable_entry_key, 24);
            return true;
        }
        return memory.read(address, out, bytes);
    }
};
}  // namespace impl

/**
 * @brief Look up one raw signed fixed-point value using native FNV/robin-hood semantics.
 * @tparam M Bounded memory policy.
 * @param mem Memory policy. @param table Header from read_table. @param name At most32 bytes.
 * @param value Receives raw int64 only on found; unchanged on absent/unreadable.
 * @return Found, absent, or unreadable/inconsistent. Probing advances into extra slots without wrapping.
 */
template <class M>
Lookup find_variable(const M& mem, const Table& table, std::string_view name, std::int64_t& value) noexcept {
    if (table.entries == 0 || table.slots == 0 || name.size() > max_variable_name) {
        return Lookup::absent;
    }
    if (table.mask > max_mask || ((table.mask + 1) & table.mask) != 0 ||
        table.slots < static_cast<std::uint64_t>(table.mask) + 1 ||
        table.slots > static_cast<std::uint64_t>(table.mask) + 256) {
        return Lookup::unreadable;
    }
    std::uint64_t index = fnv1a(name) & table.mask;
    for (std::size_t distance = 1; distance <= max_probe; ++distance, ++index) {
        if (index >= table.slots || index > (std::numeric_limits<std::uintptr_t>::max() - table.entries) / entry_size) {
            return Lookup::unreadable;
        }
        const auto address = table.entries + index * entry_size;
        std::array<unsigned char, entry_size> entry{};
        if (!detail::valid_range(address, entry.size()) || !mem.read(address, entry.data(), entry.size())) {
            return Lookup::unreadable;
        }
        if (entry[layout::variable_entry_probe] < distance) {
            return Lookup::absent;
        }
        const impl::EntryMemory<M> snapshot{mem, address + layout::variable_entry_key, entry};
        detail::StringView key;
        if (!detail::decode<char>(snapshot, address + layout::variable_entry_key, key)) {
            return Lookup::unreadable;
        }
        if (key.size != name.size()) {
            continue;
        }
        std::array<char, max_variable_name> bytes{};
        if (!detail::copy(mem, key, bytes.data(), false)) {
            return Lookup::unreadable;
        }
        if (std::string_view(bytes.data(), key.size) == name) {
            std::memcpy(&value, entry.data() + layout::variable_entry_value, sizeof(value));
            return Lookup::found;
        }
    }
    return Lookup::unreadable;
}
}  // namespace capie::engine::macos::variables
