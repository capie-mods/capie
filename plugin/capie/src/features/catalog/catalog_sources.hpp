/**
 * @file catalog_sources.hpp
 * @brief What the running game has, read for the flag and portrait catalogs (UI thread, `capie_editor.dll`).
 *
 * - **Files:** the game's own VFS listing `GetFiles(dir, out, ends_with, contains, depth)`
 *   (`rva::vfs_get_files`; it honours `replace_path`). The result array is the game's
 *   `CArray {data, int capacity, int count, allocator*}`; CAPIE passes the same shared allocator
 *   the game's callers pass (`rva::gui_structural`, pin `files.allocator`). Entries are 0x40 bytes
 *   and start with the MSVC string `"<dir>/<relative path>"`. The array is never freed (spec R6:
 *   once per folder and session).
 * - **Sprites:** a read-only walk of the sprite database's hash map (`db + 0x100`: bucket count
 *   `+0x104`, buckets `+0x108`, node `{object, next}`), keeping `CSpriteType` objects (vtable check)
 *   with their name (`+0x10`) and texture file (`+0x70`). Reads are SEH-only copies.
 * - **Scope country:** the tag index at scope `+0x8` (the scope's country setter), its tag text
 *   `[gamestate + 0x358] + tag * 0x20` (`TagText`), and its current cosmetic tag (`+0x1488` of
 *   `[gamestate + 0x310][tag]`).
 *
 * Every function returns `false` (or an empty result) on any doubt; game calls are SEH-guarded and
 * latch on a fault. Pins: `catalog_source_pins.hpp`.
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../engine/game_calls.hpp"
#include "../../engine/msvc_string.hpp"
#include "catalog_build.hpp"

#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace capie::catalog::sources {

using core::uptr;

/// @brief `void GetFiles(const char* dir, CArray* out, const char* ends_with, const char* contains, int depth)`.
using GetFilesFn = void (*)(const char*, void*, const char*, const char*, int);

/// @brief The game's `CArray` of listing entries.
struct FileArray {
    uptr data{};          ///< Entries (0x40 bytes each).
    std::int32_t capacity{};  ///< Capacity.
    std::int32_t count{};     ///< Count.
    uptr allocator{};     ///< The game's shared allocator.
};
static_assert(sizeof(FileArray) == 0x18);

/// @brief Size of one listing entry.
inline constexpr uptr file_entry_size = 0x40;
/// @brief Sanity bound of one listing.
inline constexpr std::int32_t max_files = 200000;
/// @brief Sanity bound of the sprite map.
inline constexpr std::uint32_t max_buckets = 1U << 22;
/// @brief Most sprite nodes walked.
inline constexpr std::size_t max_nodes = 400000;
/// @brief Offset of the sprite map inside the database (`FindSprite`'s thunk adds it).
inline constexpr uptr sprite_map = 0x100;
/// @brief Bucket count of the map.
inline constexpr uptr map_bucket_count = 0x4;
/// @brief Bucket array of the map.
inline constexpr uptr map_buckets = 0x8;
/// @brief Next pointer of a node.
inline constexpr uptr node_next = 0x8;
/// @brief Name of a sprite.
inline constexpr uptr sprite_name = 0x10;
/// @brief Stride of the tag text array.
inline constexpr uptr tag_text_stride = 0x20;

/**
 * @brief Copies an MSVC string from game memory with SEH-only reads.
 * @param address String object.
 * @param out Receives the bytes.
 * @return `false` when unreadable or implausible.
 */
inline bool read_string(uptr address, std::string& out) noexcept {
    engine::MsvcString raw{};
    if (!core::seh_copy(&raw, reinterpret_cast<const void*>(address), sizeof(raw)) || raw.size > raw.capacity ||
        raw.capacity > engine::max_string_capacity || raw.size > 4096) {  // NOLINT(readability-magic-numbers)
        return false;
    }
    try {
        out.resize(static_cast<std::size_t>(raw.size));
    } catch (...) {
        return false;
    }
    if (raw.size == 0) {
        return true;
    }
    const void* bytes = raw.capacity <= engine::sso_capacity ? static_cast<const void*>(raw.data.small)
                                                             : static_cast<const void*>(raw.data.pointer);
    return core::seh_copy(out.data(), bytes, out.size());
}

/**
 * @brief Calls GetFiles under SEH (no object needs unwinding here).
 * @param get_files The game's function.
 * @param dir Folder.
 * @param array Result array.
 * @param ends_with Suffix filter or null.
 * @param depth Depth.
 * @return `false` on a fault (latched).
 */
inline bool call_get_files(GetFilesFn get_files, const char* dir, FileArray* array, const char* ends_with,
                           int depth) noexcept {
    __try {
        get_files(dir, array, ends_with, nullptr, depth);
        return true;
    } __except (engine::game_call_exception(GetExceptionInformation(), engine::GameCall::catalog_files)) {
        return false;
    }
}
/**
 * @brief Lists a folder with the game's `GetFiles` (UI thread).
 * @param get_files The game's function (null = unresolved).
 * @param dir Folder, without a trailing `/` (`gfx/flags`).
 * @param ends_with Suffix filter or null.
 * @param depth Recursion depth (0 = the folder only).
 * @param out Receives the entry paths.
 * @return `false` when the call or a read failed (the latch is set on a fault).
 */
inline bool list_files(GetFilesFn get_files, const char* dir, const char* ends_with, int depth,
                       std::vector<std::string>& out) noexcept {
    out.clear();
    if (get_files == nullptr || !engine::game_calls_allowed()) {
        return false;
    }
    // Never freed (spec R6): the entries belong to the game's shared allocator, and the header is a
    // few bytes per listing.
    auto* array = new (std::nothrow) FileArray{0, 0, 0, engine::image_base + core::rva::gui_structural};
    if (array == nullptr) {
        return false;
    }
    if (!call_get_files(get_files, dir, array, ends_with, depth)) {
        return false;
    }
    if (array->count < 0 || array->count > max_files || array->count > array->capacity) {
        return false;
    }
    try {
        out.reserve(static_cast<std::size_t>(array->count));
        for (std::int32_t i = 0; i < array->count; ++i) {
            std::string path;
            if (!read_string(array->data + (static_cast<uptr>(i) * file_entry_size), path)) {
                return false;
            }
            out.push_back(std::move(path));
        }
    } catch (...) {
        return false;
    }
    return true;
}

/**
 * @brief The sprite map's node count (`db + 0x100`), the rebuild stamp of the portrait catalog.
 * @param count Receives the count.
 * @return `false` when unreadable.
 */
inline bool sprite_count(std::int32_t& count) noexcept {
    uptr db = 0;
    count = 0;
    return core::rd(core::ProcessMemory{}, engine::image_base, core::rva::sprite_database_global, db) && db != 0 &&
           core::rd(core::ProcessMemory{}, db, sprite_map, count);
}
/**
 * @brief Walks the sprite database and returns the portrait-like sprites with their files.
 * @param out Receives the sprites (already filtered by `build::portrait_sprite`).
 * @param count Receives the map's own node count (`db + 0x100`), the rebuild stamp of spec 3.1.
 * @return `false` when the map is unreadable.
 */
inline bool walk_sprites(std::vector<build::Sprite>& out, std::int32_t& count) noexcept {
    out.clear();
    count = 0;
    uptr db = 0;
    std::uint32_t buckets = 0;
    uptr table = 0;
    if (!core::rd(core::ProcessMemory{}, engine::image_base, core::rva::sprite_database_global, db) || db == 0 ||
        !core::rd(core::ProcessMemory{}, db, sprite_map, count) ||
        !core::rd(core::ProcessMemory{}, db, sprite_map + map_bucket_count, buckets) ||
        !core::rd(core::ProcessMemory{}, db, sprite_map + map_buckets, table) || buckets == 0 || buckets > max_buckets ||
        table == 0) {
        return false;
    }
    const uptr vt_sprite = engine::image_base + core::rva::vt_sprite_type;
    std::size_t visited = 0;
    try {
        for (std::uint32_t b = 0; b < buckets; ++b) {
            uptr node = 0;
            if (!core::seh_copy(&node, reinterpret_cast<const void*>(table + (static_cast<uptr>(b) * sizeof(uptr))),
                                sizeof(node))) {
                return false;
            }
            while (node != 0) {
                if (++visited > max_nodes) {
                    return false;
                }
                uptr object = 0;
                uptr vtable = 0;
                uptr next = 0;
                if (!core::seh_copy(&object, reinterpret_cast<const void*>(node), sizeof(object)) ||
                    !core::seh_copy(&next, reinterpret_cast<const void*>(node + node_next), sizeof(next))) {
                    return false;
                }
                build::Sprite sprite;
                if (object != 0 && core::seh_copy(&vtable, reinterpret_cast<const void*>(object), sizeof(vtable)) &&
                    vtable == vt_sprite && read_string(object + sprite_name, sprite.name) &&
                    build::portrait_sprite(sprite.name) && read_string(object + core::off::sprite_path, sprite.texture)) {
                    out.push_back(std::move(sprite));
                }
                node = next;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

/**
 * @brief The scope's country: tag index, tag text and current cosmetic tag.
 * @param scope Scope (window context + 0x3C0).
 * @param tag Receives the tag text (empty = the scope holds no country).
 * @param cosmetic Receives the current cosmetic tag (empty = none).
 * @return `false` when unreadable.
 */
inline bool scope_country(uptr scope, std::string& tag, std::string& cosmetic) noexcept {
    tag.clear();
    cosmetic.clear();
    std::int32_t index = 0;
    uptr gamestate = 0;
    uptr texts = 0;
    uptr countries = 0;
    std::int32_t country_count = 0;
    if (!core::rd(core::ProcessMemory{}, scope, core::off::scope_country, index)) {
        return false;
    }
    if (index <= 0) {
        return true;  // not a country scope: an empty own view
    }
    if (!core::rd(core::ProcessMemory{}, engine::image_base, core::rva::gamestate_global, gamestate) || gamestate == 0 ||
        !core::rd(core::ProcessMemory{}, gamestate, core::off::gamestate_tag_texts, texts) || texts == 0 ||
        !core::rd(core::ProcessMemory{}, gamestate, core::off::gamestate_countries, countries) ||
        !core::rd(core::ProcessMemory{}, gamestate, core::off::gamestate_country_count, country_count) ||
        index >= country_count || !read_string(texts + (static_cast<uptr>(index) * tag_text_stride), tag)) {
        return false;
    }
    uptr country = 0;
    if (core::rd(core::ProcessMemory{}, countries, static_cast<uptr>(index) * sizeof(uptr), country) && country != 0) {
        static_cast<void>(read_string(country + core::off::country_cosmetic_tag, cosmetic));
    }
    return true;
}

}  // namespace capie::catalog::sources
