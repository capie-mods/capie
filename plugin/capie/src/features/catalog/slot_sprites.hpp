/**
 * @file slot_sprites.hpp
 * @brief Slot sprites of a flag or portrait view: discovery and texture re-point (spec 4, UI thread).
 *
 * A mod declares, per view, two sets of `N` slot sprites and two preview sprites in its own `.gfx`:
 *
 *     GFX_capie_v1_s__<mod>__<view>__<set>_<slot>   set 0 or 1, slot 0..N-1 (N = 1..48)
 *     GFX_capie_v1_s__<mod>__<view>__p<set>         the preview of each set
 *
 * N is the number of consecutive slots of set 0 from 0; set 1 must have the same N and both previews
 * must exist, else the view is invalid (sentinel -3). Each must be a `CSpriteType`.
 *
 * Re-point protocol (texture-release research 4.1; spec 15.1): re-find the sprite with FindSprite and
 * check its vtable and texture manager before every write; `AddTexture` the file; `GetTexture` (0:
 * release the new id, keep the slot); write the id, the texture and the size in that order; queue the
 * sprite's old id for `ReleaseTexture` after at least 2 GUI updates (`release_after` window ticks and
 * 300 ms), on the UI thread. A slot past the end of a page is re-pointed to its placeholder file with a
 * fresh AddTexture. Every id CAPIE obtains is released exactly once; portrait-path textures (the
 * portrait layer's entries) are never touched here. Only the hidden set and the hidden preview are
 * ever written. At most `max_outstanding` CAPIE references are held (fail-closed guard).
 */
#pragma once
#include "../../core/game_layout.hpp"
#include "../../core/memory.hpp"
#include "../../engine/game_calls.hpp"
#include "../../engine/msvc_string.hpp"
#include "catalog_names.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace capie::catalog::slots {

using core::uptr;
using engine::MsvcString;

/// @brief `sprite* FindSprite(database*, const string* name)`.
using FindFn = void* (*)(void*, const MsvcString*);
/// @brief `int AddTexture(texmgr*, const string* path, const options*, bool load_now)`.
using AddFn = int (*)(void*, const MsvcString*, const void*, bool);
/// @brief `texture* GetTexture(texmgr*, int id)`.
using GetFn = void* (*)(void*, int);
/// @brief `int* TextureSize(texmgr*, int out[2], int id)`.
using SizeFn = std::int32_t* (*)(void*, std::int32_t*, int);
/// @brief `void ReleaseTexture(texmgr*, int id)`.
using ReleaseFn = void (*)(void*, int);

/// @brief Most slots per set.
inline constexpr std::size_t max_slots = 48;
/// @brief Window updates before a deferred release.
inline constexpr unsigned release_after = 3;
/// @brief Milliseconds before a deferred release.
inline constexpr unsigned long long release_ms = 300;
/// @brief Fail-closed bound of outstanding CAPIE references.
inline constexpr int max_outstanding = 1024;
/// @brief Deferred releases held at most (two sets and previews of 16 views, twice).
inline constexpr std::size_t max_pending = 4 * ((2 * max_slots) + 2);
/// @brief Prefix of a slot sprite.
inline constexpr std::string_view sprite_prefix = "GFX_capie_v1_s__";

/// @brief The lazy loader's AddTexture options (`sprite.options`).
struct Options {
    std::int32_t zero{0};                    ///< First word.
    std::int32_t flags{0x1000001};           ///< Load flags.
    std::uint8_t tail{0};                    ///< Last byte.
    std::array<std::uint8_t, 3> padding{};  ///< Unused.
};
static_assert(sizeof(Options) == 12);

/// @brief Game functions (set at initialization when the sprite pins passed).
struct Calls {
    FindFn find{};        ///< FindSprite.
    AddFn add{};          ///< AddTexture.
    GetFn get{};          ///< GetTexture.
    SizeFn size{};        ///< TextureSize.
    ReleaseFn release{};  ///< ReleaseTexture (null = never release: per-session cap).
};

/// @brief Calls of this process.
inline Calls calls{};
/// @brief CAPIE references not yet released.
inline int outstanding = 0;

/// @brief A release waiting for the swap to be drawn.
struct Pending {
    int id{-1};                   ///< Id.
    uptr texmgr{};                ///< Its texture manager.
    unsigned ticks{};             ///< Window updates still to wait.
    unsigned long long due{};     ///< Earliest time.
};

/// @brief Deferred releases (UI thread only).
inline std::array<Pending, max_pending> pending{};

/**
 * @brief The name of slot `slot` of set `set` (`slot` = -1: the preview).
 * @param id View ids.
 * @param set 0 or 1.
 * @param slot 0..47, or -1.
 * @return `GFX_capie_v1_s__<mod>__<view>__<set>_<slot>` or `…__p<set>`.
 */
inline std::string sprite_name(const names::EditorId& id, int set, int slot) {
    std::array<char, 96> out{};
    if (slot < 0) {
        static_cast<void>(std::snprintf(out.data(), out.size(), "%.*s%.*s__%.*s__p%d",
                                        static_cast<int>(sprite_prefix.size()), sprite_prefix.data(),
                                        static_cast<int>(id.mod_size), id.mod.data(), static_cast<int>(id.editor_size),
                                        id.editor.data(), set));
    } else {
        static_cast<void>(std::snprintf(out.data(), out.size(), "%.*s%.*s__%.*s__%d_%d",
                                        static_cast<int>(sprite_prefix.size()), sprite_prefix.data(),
                                        static_cast<int>(id.mod_size), id.mod.data(), static_cast<int>(id.editor_size),
                                        id.editor.data(), set, slot));
    }
    return out.data();
}

/// @brief A resolved slot sprite.
struct Slot {
    uptr sprite{};  ///< CSpriteType.
    uptr texmgr{};  ///< Texture manager (the database's).
};

/**
 * @brief FindSprite plus the checks of spec 4.3 (vtable, the database's texture manager).
 * @param name Sprite name.
 * @param slot Receives the sprite.
 * @return `false` when missing, not a CSpriteType, or a fault.
 */
inline bool resolve(const std::string& name, Slot& slot) noexcept {
    slot = Slot{};
    uptr db = 0;
    uptr db_texmgr = 0;
    if (calls.find == nullptr || !engine::game_calls_allowed() ||
        !core::rd(core::ProcessMemory{}, engine::image_base, core::rva::sprite_database_global, db) || db == 0 ||
        !core::rd(core::ProcessMemory{}, db, core::off::db_texture_manager, db_texmgr) || db_texmgr == 0) {
        return false;
    }
    const auto argument = engine::borrowed_string(name.data(), name.size());
    void* found = nullptr;
    __try {
        found = calls.find(reinterpret_cast<void*>(db), &argument);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        return false;
    }
    const auto sprite = reinterpret_cast<uptr>(found);
    uptr vtable = 0;
    uptr sprite_db = 0;
    uptr texmgr = 0;
    if (sprite == 0 || !core::rd(core::ProcessMemory{}, sprite, 0, vtable) ||
        vtable != engine::image_base + core::rva::vt_sprite_type ||
        !core::rd(core::ProcessMemory{}, sprite, core::off::sprite_db, sprite_db) ||
        !core::rd(core::ProcessMemory{}, sprite_db, core::off::db_texture_manager, texmgr) || texmgr != db_texmgr) {
        return false;
    }
    slot = {sprite, texmgr};
    return true;
}

/**
 * @brief Counts a view's slots: consecutive slots of set 0 from 0, the same in set 1, both previews.
 * @param id View ids.
 * @return N (1..48), or 0 when the view is invalid.
 */
inline std::size_t count_slots(const names::EditorId& id) noexcept {
    try {
        std::size_t n = 0;
        Slot slot{};
        while (n < max_slots && resolve(sprite_name(id, 0, static_cast<int>(n)), slot)) {
            ++n;
        }
        if (n == 0 || resolve(sprite_name(id, 0, static_cast<int>(n)), slot)) {
            return 0;  // none, or more than 48
        }
        for (std::size_t k = 0; k < n; ++k) {
            if (!resolve(sprite_name(id, 1, static_cast<int>(k)), slot)) {
                return 0;
            }
        }
        if (resolve(sprite_name(id, 1, static_cast<int>(n)), slot) || !resolve(sprite_name(id, 0, -1), slot) ||
            !resolve(sprite_name(id, 1, -1), slot)) {
            return 0;
        }
        return n;
    } catch (...) {
        return 0;
    }
}

/**
 * @brief Drops one CAPIE-owned reference now (UI thread).
 * @param texmgr Texture manager.
 * @param id Id.
 */
inline void release_now(uptr texmgr, int id) noexcept {
    if (id < 0 || calls.release == nullptr || !engine::game_calls_allowed()) {
        return;
    }
    __try {
        calls.release(reinterpret_cast<void*>(texmgr), id);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        return;
    }
    --outstanding;
}

/**
 * @brief Queues the release of an id the sprite owned.
 * @param texmgr Texture manager.
 * @param id Old id.
 */
inline void defer_release(uptr texmgr, int id) noexcept {
    if (calls.release == nullptr) {
        return;  // release pins failed: keep the reference (per-session cap)
    }
    for (auto& item : pending) {
        if (item.id < 0) {
            item = {id, texmgr, release_after, GetTickCount64() + release_ms};
            return;
        }
    }
}

/**
 * @brief Drains due releases (call once per window update of any catalog view, UI thread).
 */
inline void drain() noexcept {
    const auto now = GetTickCount64();
    for (auto& item : pending) {
        if (item.id < 0) {
            continue;
        }
        if (item.ticks > 0) {
            --item.ticks;
            continue;
        }
        if (now >= item.due) {
            release_now(item.texmgr, item.id);
            item.id = -1;
        }
    }
}

/// @brief Outcome of a re-point.
enum class Repoint {
    done,     ///< The slot shows the file.
    missing,  ///< The file did not load (the slot keeps its texture).
    failed,   ///< The sprite is gone, the cap is reached, or a fault (catalog off on a fault).
};

/**
 * @brief Re-points one slot sprite to a texture file (UI thread; hidden set only).
 * @param name Sprite name.
 * @param path Texture path.
 * @return Outcome.
 */
inline Repoint repoint(const std::string& name, const std::string& path) noexcept {
    Slot slot{};
    if (calls.add == nullptr || calls.get == nullptr || calls.size == nullptr || !resolve(name, slot) ||
        outstanding >= max_outstanding) {
        return Repoint::failed;
    }
    const auto argument = engine::borrowed_string(path.data(), path.size());
    const Options options{};
    const auto texmgr = reinterpret_cast<void*>(slot.texmgr);
    int id = -1;
    void* texture = nullptr;
    std::array<std::int32_t, 2> size{};
    __try {
        id = calls.add(texmgr, &argument, &options, true);
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        return Repoint::failed;
    }
    ++outstanding;
    __try {
        texture = calls.get(texmgr, id);
        if (texture != nullptr) {
            const std::int32_t* got = calls.size(texmgr, size.data(), id);
            if (got != nullptr) {
                size = {got[0], got[1]};
            }
        }
    } __except (engine::game_call_exception(GetExceptionInformation())) {
        return Repoint::failed;
    }
    if (texture == nullptr) {
        release_now(slot.texmgr, id);
        return Repoint::missing;
    }
    std::int32_t old = -1;
    if (!core::rd(core::ProcessMemory{}, slot.sprite, core::off::sprite_texture_id, old)) {
        release_now(slot.texmgr, id);
        return Repoint::failed;
    }
    // The order of the game's lazy loader: id, texture, size.
    if (!core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_texture_id), &id, sizeof(id)) ||
        !core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_texture), &texture, sizeof(texture)) ||
        !core::seh_copy(reinterpret_cast<void*>(slot.sprite + core::off::sprite_size), size.data(), sizeof(size))) {
        engine::game_call_latch.load()->store(true);  // a half-written sprite: no more game calls
        return Repoint::failed;
    }
    if (old == id) {
        release_now(slot.texmgr, id);  // the same file: AddTexture added a second reference
    } else if (old >= 0) {
        defer_release(slot.texmgr, old);
    }
    return Repoint::done;
}

/**
 * @brief Reads a slot sprite's own texture file (its placeholder), once at registration.
 * @param name Sprite name.
 * @param out Receives the path.
 * @return `false` when unreadable.
 */
inline bool placeholder(const std::string& name, std::string& out) noexcept {
    Slot slot{};
    char bytes[engine::max_name_bytes + 1]{};
    std::size_t size = 0;
    if (!resolve(name, slot) || !engine::string_bytes(reinterpret_cast<void*>(slot.sprite + core::off::sprite_path),
                                                      bytes, sizeof(bytes) - 1, size)) {
        return false;
    }
    try {
        out.assign(bytes, size);
    } catch (...) {
        return false;
    }
    return true;
}

}  // namespace capie::catalog::slots
