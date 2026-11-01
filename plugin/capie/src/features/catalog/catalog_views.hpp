/**
 * @file catalog_views.hpp
 * @brief The flag and portrait catalog views (spec 3 to 5): catalogs, pages, slot sets, picks (UI thread).
 *
 * Built on the assumption that the spikes S1/S2 pass (`docs/contributing/test-session-api9-spikes.md`).
 * Every action runs inside the Dispatch hook on the UI thread, after the `ThreadForbidCount` check:
 *
 * - `flag` / `flagown` / `portrait` / `portraitown`: builds the catalog once per session (the flag
 *   catalog from the three `gfx/flags` listings; the portrait catalog from the sprite database and
 *   `gfx/leaders`, rebuilt when the sprite map's node count changes), filters it for the scope
 *   country, shows page 1.
 * - `first` / `prev` / `next` / `last`: moves the page of the view's current catalog.
 * - `pick`: the clicked slot comes from the click's own `capie_v1_slot` record (a dynamic list
 *   entry's value), read before the reserved records are removed; the item's **name** goes into
 *   the records, never an index.
 *
 * A page is loaded into the **hidden** slot set (`slot_sprites.hpp`), then `capie_par` tells the mod
 * to show it; a pick loads the hidden preview and flips `capie_ppar`. Labels: the stem, the sprite
 * name or the file name without its folders, published as `CAPIE_V1_T__<MOD>__<VIEW>__<k>`, `__SEL`,
 * `__PAGE` and `__COUNT`.
 */
#pragma once
#include "catalog_build.hpp"
#include "catalog_names.hpp"
#include "catalog_pick.hpp"
#include "catalog_records.hpp"
#include "catalog_sources.hpp"
#include "slot_sprites.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace capie::catalog::pickers {

using names::Action;

/// @brief Picker state of one registered view (UI thread only).
struct Picker {
    bool checked{};                     ///< Slots counted.
    std::size_t slots{};                ///< N (0 = no slot sprites: invalid for flag/portrait).
    std::vector<std::string> blank;     ///< Placeholder file per sprite: set 0, set 1, p0, p1.
    std::int64_t catalog{};             ///< 0 none, 1 flag, 2 portrait.
    std::vector<std::uint32_t> view;    ///< Indexes into the catalog.
    std::size_t page{};                 ///< 1-based page shown (0 = empty).
    int shown{};                        ///< Slot set shown.
    int preview{};                      ///< Preview set shown.
};

/// @brief Catalogs and pickers of this process (UI thread only).
struct State {
    std::array<Picker, 16> pickers{};   ///< Per view slot.
    build::Catalog flags;               ///< Flag catalog (built once).
    bool flags_built{};                 ///< `flags` is valid.
    build::Catalog portraits;           ///< Portrait catalog.
    std::int32_t portrait_stamp{-1};    ///< Sprite map node count at the build.
    sources::GetFilesFn get_files{};    ///< The game's GetFiles (null = catalogs off).
};

/// @brief State of this process.
inline State state{};

/**
 * @brief The label of an item: the stem, the sprite name, or the file name without its folders.
 * @param item Item.
 * @return Label.
 */
inline std::string label_of(const build::Item& item) {
    if (item.kind != build::Kind::path) {
        return item.name;
    }
    const auto slash = item.name.rfind('/');
    return slash == std::string::npos ? item.name : item.name.substr(slash + 1);
}

/**
 * @brief Builds (once) the flag catalog.
 * @return `true` when available.
 */
inline bool ensure_flags() noexcept {
    if (state.flags_built) {
        return true;
    }
    try {
        std::vector<std::string> large;
        std::vector<std::string> medium;
        std::vector<std::string> small;
        if (!sources::list_files(state.get_files, "gfx/flags", ".tga", 0, large) ||
            !sources::list_files(state.get_files, "gfx/flags/medium", ".tga", 0, medium) ||
            !sources::list_files(state.get_files, "gfx/flags/small", ".tga", 0, small)) {
            return false;
        }
        state.flags = build::flags(large, medium, small);
        state.flags_built = true;
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Builds the portrait catalog when the sprite map changed (or never built).
 * @return `true` when available.
 */
inline bool ensure_portraits() noexcept {
    try {
        std::int32_t stamp = 0;
        if (!sources::sprite_count(stamp)) {
            return false;
        }
        if (stamp == state.portrait_stamp) {
            return true;  // the sprite map is unchanged since the last build
        }
        std::vector<build::Sprite> sprites;
        if (!sources::walk_sprites(sprites, stamp)) {
            return false;
        }
        std::vector<std::string> files;
        if (!sources::list_files(state.get_files, "gfx/leaders", nullptr, 8, files)) {  // NOLINT(readability-magic-numbers): spec 3.1 depth
            return false;
        }
        state.portraits = build::portraits(sprites, files);
        state.portrait_stamp = stamp;
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Counts a view's slots once and saves the placeholder files.
 * @param picker Picker.
 * @param id View ids.
 * @return `true` when the view has valid slot sets.
 */
inline bool ensure_slots(Picker& picker, const names::EditorId& id) noexcept {
    if (!picker.checked) {
        picker.checked = true;
        picker.slots = slots::count_slots(id);
        try {
            picker.blank.assign((2 * picker.slots) + 2, std::string{});
            for (int set = 0; set < 2; ++set) {
                for (std::size_t k = 0; k < picker.slots; ++k) {
                    static_cast<void>(slots::placeholder(slots::sprite_name(id, set, static_cast<int>(k)),
                                                         picker.blank[(static_cast<std::size_t>(set) * picker.slots) + k]));
                }
                static_cast<void>(slots::placeholder(slots::sprite_name(id, set, -1),
                                                     picker.blank[(2 * picker.slots) + static_cast<std::size_t>(set)]));
            }
        } catch (...) {
            picker.slots = 0;
        }
    }
    return picker.slots != 0;
}

/**
 * @brief The catalog of a picker.
 * @param picker Picker.
 * @return Catalog (empty when none).
 */
inline const build::Catalog& catalog_of(const Picker& picker) noexcept {
    static const build::Catalog empty{};
    return picker.catalog == pick::catalog_flag ? state.flags : (picker.catalog == pick::catalog_portrait ? state.portraits : empty);
}

/**
 * @brief Loads a page into the hidden set and flips the shown set.
 * @param picker Picker.
 * @param id View ids.
 * @param page The page.
 * @param publish Label sink `(key, text)`.
 * @return `false` when a re-point failed (the catalog is then off for the click).
 */
template <class Publish>
bool load_page(Picker& picker, const names::EditorId& id, const build::Page& page, Publish&& publish) {
    const int hidden = 1 - picker.shown;
    const auto& catalog = catalog_of(picker);
    for (std::size_t k = 0; k < picker.slots; ++k) {
        const auto name = slots::sprite_name(id, hidden, static_cast<int>(k));
        const auto& blank = picker.blank[(static_cast<std::size_t>(hidden) * picker.slots) + k];
        std::string text;
        auto outcome = slots::Repoint::done;
        if (k < page.count) {
            const auto& item = catalog.items[picker.view[page.first + k]];
            outcome = slots::repoint(name, item.texture);
            if (outcome == slots::Repoint::missing && !blank.empty()) {
                outcome = slots::repoint(name, blank);
            }
            text = label_of(item);
        } else if (!blank.empty()) {
            outcome = slots::repoint(name, blank);
        }
        if (outcome == slots::Repoint::failed) {
            return false;
        }
        publish(names::row_key(id, k), text.c_str());
    }
    picker.shown = hidden;
    picker.page = page.page;
    std::array<char, 32> line{};
    static_cast<void>(std::snprintf(line.data(), line.size(), "%zu / %zu", page.page, page.pages));
    publish(names::text_key(id, "PAGE"), line.data());
    static_cast<void>(std::snprintf(line.data(), line.size(), "%zu", picker.view.size()));
    publish(names::text_key(id, "COUNT"), line.data());
    return true;
}

/**
 * @brief Runs a flag or portrait action.
 * @param picker Picker of the view.
 * @param id View ids.
 * @param action Action.
 * @param scope Scope (context + 0x3C0).
 * @param slot Clicked slot for `pick` (-1 = none or invalid).
 * @param publish Label sink `(key, text)`.
 * @return Records.
 */
template <class Publish>
pick::List run(Picker& picker, const names::EditorId& id, Action action, std::uintptr_t scope, std::int64_t slot,
               Publish&& publish) {
    const auto off = [] {
        pick::List out{};
        const auto header = records::unavailable();
        for (std::size_t i = 0; i < header.count; ++i) {
            out.add(header.items[i].name.data(), header.items[i].value);
        }
        return out;
    };
    const auto sentinel = [](records::Sentinel code) {
        pick::List out{};
        const auto header = records::sentinel(code);
        for (std::size_t i = 0; i < header.count; ++i) {
            out.add(header.items[i].name.data(), header.items[i].value);
        }
        return out;
    };
    if (state.get_files == nullptr) {
        return off();
    }
    if (!ensure_slots(picker, id)) {
        return sentinel(records::Sentinel::view_invalid);
    }
    std::string tag;
    std::string cosmetic;
    if (!sources::scope_country(scope, tag, cosmetic)) {
        return off();
    }
    std::size_t requested = picker.page;
    switch (action) {
        case Action::flag:
        case Action::flagown:
            if (!ensure_flags()) {
                return off();
            }
            picker.catalog = pick::catalog_flag;
            picker.view = action == Action::flag ? build::all(state.flags) : build::flag_own(state.flags, tag, cosmetic);
            requested = 1;
            break;
        case Action::portrait:
        case Action::portraitown:
            if (!ensure_portraits()) {
                return off();
            }
            picker.catalog = pick::catalog_portrait;
            picker.view = action == Action::portrait ? build::all(state.portraits) : build::portrait_own(state.portraits, tag);
            requested = 1;
            break;
        case Action::first:
            requested = 1;
            break;
        case Action::prev:
            requested = requested > 1 ? requested - 1 : 1;
            break;
        case Action::next:
            requested = requested + 1;
            break;
        case Action::last:
            requested = static_cast<std::size_t>(-1);
            break;
        default:
            break;
    }
    if (picker.catalog == 0) {
        return off();  // a page or pick action before any catalog action
    }
    const auto page = build::page_of(picker.view.size(), picker.slots, requested);
    if (action == Action::pick) {
        const auto current = build::page_of(picker.view.size(), picker.slots, picker.page);
        if (slot < 0 || static_cast<std::size_t>(slot) >= current.count) {
            return sentinel(records::Sentinel::slot);
        }
        const auto& item = catalog_of(picker).items[picker.view[current.first + static_cast<std::size_t>(slot)]];
        const int hidden = 1 - picker.preview;
        if (slots::repoint(slots::sprite_name(id, hidden, -1), item.texture) == slots::Repoint::failed) {
            return off();
        }
        picker.preview = hidden;
        publish(names::text_key(id, "SEL"), label_of(item).c_str());
        const auto base = pick::page(picker.catalog, picker.view.size(), current, picker.shown, picker.preview);
        if (picker.catalog == pick::catalog_flag) {
            return pick::flag_pick(base, item.name, cosmetic);
        }
        std::string small = item.name;
        slots::Slot found{};
        if (item.kind == build::Kind::sprite && slots::resolve(item.name + "_small", found)) {
            small = item.name + "_small";
        }
        return pick::portrait_pick(base, item.name, small);
    }
    if (!load_page(picker, id, page, publish)) {
        return off();
    }
    return pick::page(picker.catalog, picker.view.size(), page, picker.shown, picker.preview);
}

}  // namespace capie::catalog::pickers
