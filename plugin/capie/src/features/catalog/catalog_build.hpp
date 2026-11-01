/**
 * @file catalog_build.hpp
 * @brief Building, filtering and paging the flag and portrait catalogs (spec 3.1 to 3.3), as pure data.
 *
 * The inputs are what the game reports at run time (`catalog_sources.hpp` reads them): the three
 * `gfx/flags` listings (large, `medium/`, `small/`), the portrait-like sprites of the sprite
 * database with their texture files, and the files under `gfx/leaders`. Nothing here reads game
 * memory or allocates game objects.
 *
 * - **Flag catalog:** the stems present in all three sizes (so a chosen flag never mixes sizes),
 *   1..63 characters of the 17.3 alphabet.
 * - **Portrait catalog:** sprites whose name starts, ignoring case, with `GFX_portrait` or
 *   `GFX_leader` and does not end with `_small`, whose name fits packing B; plus `.dds`, `.png`
 *   and `.tga` files under `gfx/leaders/` that pass the path rules, except a file that is the
 *   texture of a kept sprite (the sprite wins).
 * - Both are sorted by byte order (`memcmp`), without duplicates, and capped at 65 535 items.
 * - **Filters:** `flagown`: a stem equal to the scope country's tag text `T` or starting with `T_`,
 *   or equal to / starting with `<cosmetic>_` of its current cosmetic tag; `portraitown`: a name
 *   that contains `_T_` or a path that contains `/T/`.
 * - **Paging:** page size N (1..48), 1-based pages, `0 / 0` for an empty view.
 */
#pragma once
#include "../country/flag_layer.hpp"
#include "../country/packing_b.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace capie::catalog::build {

/// @brief Most items of one catalog.
inline constexpr std::size_t max_items = 65535;
/// @brief Most slots of one page.
inline constexpr std::size_t max_page = 48;
/// @brief Folder of the large flags (the listing's entries start with it).
inline constexpr std::string_view flag_folder = "gfx/flags/";
/// @brief Folder of the leader portraits.
inline constexpr std::string_view leader_folder = "gfx/leaders/";

/// @brief Kind of a catalog item.
enum class Kind : std::uint8_t {
    sprite = 0,  ///< A sprite name (portrait catalog).
    path = 1,    ///< A texture path (portrait catalog).
    stem = 2,    ///< A flag stem (flag catalog).
};

/// @brief One item.
struct Item {
    std::string name;     ///< Stem, sprite name or path.
    Kind kind{};          ///< Kind.
    std::string texture;  ///< The file a slot shows (flag: medium file; sprite: its texture; path: itself).
};

/// @brief A built catalog.
struct Catalog {
    std::vector<Item> items;  ///< Sorted, unique.
    std::size_t dropped{};    ///< Items past the cap.
};

/**
 * @brief Lower-case ASCII copy.
 * @param text Text.
 * @return Lower case.
 */
inline std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        c = country::packing_b::lower(c);
    }
    return out;
}

/**
 * @brief The stem of a listing entry `<folder><stem>.tga` (no sub-folder).
 * @param entry Entry path as the game lists it.
 * @param folder Expected folder, with the trailing `/`.
 * @param stem Receives the stem.
 * @return `true` for a `.tga` file directly in `folder`.
 */
inline bool flag_stem(std::string_view entry, std::string_view folder, std::string_view& stem) {
    if (entry.size() <= folder.size() + 4 || lower(entry.substr(0, folder.size())) != folder ||
        !country::packing_b::ends_with_nocase(entry, ".tga")) {
        return false;
    }
    stem = entry.substr(folder.size(), entry.size() - folder.size() - 4);
    return stem.find('/') == std::string_view::npos;
}

/**
 * @brief Whether a stem fits the 17.3 alphabet (1..63 characters).
 * @param stem Stem.
 * @return `true` when packable.
 */
inline bool packable_stem(std::string_view stem) {
    return !stem.empty() && stem.size() <= country::flag::max_chars &&
           std::ranges::all_of(stem, [](char c) { return country::flag::code_of(c) != 0; });
}

/**
 * @brief Sorts by byte order, removes duplicates and applies the cap.
 * @param catalog Catalog.
 */
inline void finish(Catalog& catalog) {
    std::ranges::sort(catalog.items, [](const Item& a, const Item& b) { return a.name < b.name; });
    const auto tail = std::ranges::unique(catalog.items, [](const Item& a, const Item& b) { return a.name == b.name; });
    catalog.items.erase(tail.begin(), tail.end());
    if (catalog.items.size() > max_items) {
        catalog.dropped += catalog.items.size() - max_items;
        catalog.items.resize(max_items);
    }
}

/**
 * @brief The flag catalog from the three listings.
 * @param large Entries of `gfx/flags`.
 * @param medium Entries of `gfx/flags/medium`.
 * @param small Entries of `gfx/flags/small`.
 * @return Catalog of stems present in all three.
 */
inline Catalog flags(const std::vector<std::string>& large, const std::vector<std::string>& medium,
                     const std::vector<std::string>& small) {
    const auto stems = [](const std::vector<std::string>& entries, std::string_view folder) {
        std::vector<std::string> out;
        for (const auto& entry : entries) {
            std::string_view stem;
            if (flag_stem(entry, folder, stem) && packable_stem(stem)) {
                out.emplace_back(stem);
            }
        }
        std::ranges::sort(out);
        return out;
    };
    const auto in_large = stems(large, flag_folder);
    const auto in_medium = stems(medium, "gfx/flags/medium/");
    const auto in_small = stems(small, "gfx/flags/small/");
    Catalog out;
    for (const auto& stem : in_large) {
        if (std::ranges::binary_search(in_medium, stem) && std::ranges::binary_search(in_small, stem)) {
            out.items.push_back({stem, Kind::stem, "gfx/flags/medium/" + stem + ".tga"});
        }
    }
    finish(out);
    return out;
}

/// @brief A sprite of the sprite database as the walk reports it.
struct Sprite {
    std::string name;     ///< Sprite name.
    std::string texture;  ///< Its `texturefile`.
};

/**
 * @brief Whether a sprite name is portrait-like (spec 3.1).
 * @param name Sprite name.
 * @return `true` for `GFX_portrait*` / `GFX_leader*` (any case) not ending in `_small`.
 */
inline bool portrait_sprite(std::string_view name) {
    const auto low = lower(name);
    return (low.starts_with("gfx_portrait") || low.starts_with("gfx_leader")) && !low.ends_with("_small");
}

/**
 * @brief The portrait catalog.
 * @param sprites Sprites of the database (any; filtered here).
 * @param files Entries under `gfx/leaders` (any depth).
 * @return Catalog of sprites and paths.
 */
inline Catalog portraits(const std::vector<Sprite>& sprites, const std::vector<std::string>& files) {
    Catalog out;
    std::vector<std::string> used;
    for (const auto& sprite : sprites) {
        if (portrait_sprite(sprite.name) && country::packing_b::valid_name(sprite.name) &&
            !country::packing_b::is_path(sprite.name)) {
            out.items.push_back({sprite.name, Kind::sprite, sprite.texture});
            used.push_back(lower(sprite.texture));
        }
    }
    std::ranges::sort(used);
    for (const auto& file : files) {
        if (!lower(file).starts_with(leader_folder) || !country::packing_b::valid_path(file) ||
            std::ranges::binary_search(used, lower(file))) {
            continue;
        }
        out.items.push_back({file, Kind::path, file});
    }
    finish(out);
    return out;
}

/**
 * @brief The `flagown` filter.
 * @param catalog Flag catalog.
 * @param tag Scope country's tag text (empty = not a country: an empty view).
 * @param cosmetic Its current cosmetic tag (may be empty).
 * @return Indexes into `catalog.items`.
 */
inline std::vector<std::uint32_t> flag_own(const Catalog& catalog, std::string_view tag, std::string_view cosmetic) {
    std::vector<std::uint32_t> out;
    if (tag.empty()) {
        return out;
    }
    const auto matches = [](std::string_view stem, std::string_view key) {
        return !key.empty() && (stem == key || (stem.size() > key.size() && stem.starts_with(key) && stem[key.size()] == '_'));
    };
    for (std::size_t i = 0; i < catalog.items.size(); ++i) {
        const std::string_view stem = catalog.items[i].name;
        if (matches(stem, tag) || matches(stem, cosmetic)) {
            out.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return out;
}

/**
 * @brief The `portraitown` filter.
 * @param catalog Portrait catalog.
 * @param tag Scope country's tag text (empty = an empty view).
 * @return Indexes into `catalog.items`.
 */
inline std::vector<std::uint32_t> portrait_own(const Catalog& catalog, std::string_view tag) {
    std::vector<std::uint32_t> out;
    if (tag.empty()) {
        return out;
    }
    const std::string in_name = "_" + std::string(tag) + "_";
    const std::string in_path = "/" + std::string(tag) + "/";
    for (std::size_t i = 0; i < catalog.items.size(); ++i) {
        const auto& item = catalog.items[i];
        const bool hit = item.kind == Kind::path ? item.name.find(in_path) != std::string::npos
                                                 : item.name.find(in_name) != std::string::npos;
        if (hit) {
            out.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return out;
}

/**
 * @brief Every item (the `flag` and `portrait` actions).
 * @param catalog Catalog.
 * @return Indexes 0..n-1.
 */
inline std::vector<std::uint32_t> all(const Catalog& catalog) {
    std::vector<std::uint32_t> out(catalog.items.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint32_t>(i);
    }
    return out;
}

/// @brief One page of a view.
struct Page {
    std::size_t page{};   ///< 1-based page shown (0 when empty).
    std::size_t pages{};  ///< Number of pages (0 when empty).
    std::size_t first{};  ///< Index into the view of slot 0.
    std::size_t count{};  ///< Items on the page.
};

/**
 * @brief The page `page` of a view of `items` items with `size` slots (clamped to 1..pages).
 * @param items Items in the view.
 * @param size Page size (1..48).
 * @param page Requested 1-based page.
 * @return The page.
 */
constexpr Page page_of(std::size_t items, std::size_t size, std::size_t page) noexcept {
    Page out{};
    if (items == 0 || size == 0 || size > max_page) {
        return out;
    }
    out.pages = (items + size - 1) / size;
    out.page = page < 1 ? 1 : (page > out.pages ? out.pages : page);
    out.first = (out.page - 1) * size;
    out.count = items - out.first < size ? items - out.first : size;
    return out;
}

}  // namespace capie::catalog::build
