/**
 * @file catalog_pick.hpp
 * @brief Records of the paging and `pick` actions of the flag and portrait catalogs (spec 5.3), as pure data.
 *
 *     every page action  capie_catalog = 1, capie_cat (1 flag, 2 portrait), capie_cn, capie_pg,
 *                        capie_pgs, capie_pn, capie_par, capie_ppar
 *     pick, in addition  capie_sel_fmt   1 = 17.3 packing (a flag stem), 2 = packing B
 *                        capie_sel_len, capie_sel_0 .. capie_sel_41   the item's name
 *                        capie_sml_len, capie_sml_0 ..                portrait: the small name (packing B)
 *                        capie_cos_len, capie_cos_0 .. capie_cos_20   flag: the scope country's current
 *                                                                     cosmetic tag (17.3; 0 = none,
 *                                                                     -1 = not expressible)
 *
 * Names travel, never indexes (spec 2, principle 2). Pure data: no game memory.
 */
#pragma once
#include "../country/flag_layer.hpp"
#include "../country/packing_b.hpp"
#include "catalog_build.hpp"
#include "catalog_records.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace capie::catalog::pick {

/// @brief `capie_cat` of the flag catalog.
inline constexpr std::int64_t catalog_flag = 1;
/// @brief `capie_cat` of the portrait catalog.
inline constexpr std::int64_t catalog_portrait = 2;
/// @brief `capie_sel_fmt` of a 17.3 name.
inline constexpr std::int64_t format_173 = 1;
/// @brief `capie_sel_fmt` of a packing B name.
inline constexpr std::int64_t format_b = 2;

/// @brief Records of the pick and page actions (header 10 + sel 1 + 43 + sml 43 or cos 22).
inline constexpr std::size_t max_records = 10 + 1 + 43 + 43;

/// @brief A record list long enough for a pick.
struct List {
    std::array<records::Record, max_records> items{};  ///< Records.
    std::size_t count{};                               ///< Used.
    bool overflow{};                                   ///< A record did not fit.

    /**
     * @brief Appends a record.
     * @param name Name (at most 15 bytes).
     * @param value Value.
     */
    void add(std::string_view name, std::int64_t value) noexcept {
        if (count >= items.size() || name.empty() || name.size() > records::max_name) {
            overflow = true;
            return;
        }
        auto& item = items[count++];
        item = records::Record{};
        for (std::size_t i = 0; i < name.size(); ++i) {
            item.name[i] = name[i];
        }
        item.value = value;
    }

    /**
     * @brief Finds a record (tests).
     * @param name Name.
     * @param value Receives the value.
     * @return `true` when present.
     */
    bool find(std::string_view name, std::int64_t& value) const noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (std::string_view(items[i].name.data()) == name) {
                value = items[i].value;
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief The header of a served page: catalog, counts, page and sets.
 * @param catalog `capie_cat`.
 * @param items Items in the view.
 * @param page The page.
 * @param set Slot set to show.
 * @param preview Preview set to show.
 * @return List.
 */
inline List page(std::int64_t catalog, std::size_t items, const build::Page& page, int set, int preview) noexcept {
    List out{};
    out.add("capie_cap", static_cast<std::int64_t>(core::capie_api_level));
    out.add("capie_ver", static_cast<std::int64_t>(core::capie_build_number));
    out.add("capie_catalog", 1);
    out.add("capie_cat", catalog);
    out.add("capie_cn", static_cast<std::int64_t>(items));
    out.add("capie_pg", static_cast<std::int64_t>(page.page));
    out.add("capie_pgs", static_cast<std::int64_t>(page.pages));
    out.add("capie_pn", static_cast<std::int64_t>(page.count));
    out.add("capie_par", set);
    out.add("capie_ppar", preview);
    return out;
}

/**
 * @brief Appends `<prefix>_len` and `<prefix>_<k>` of a name in 17.3 packing.
 * @param out List.
 * @param prefix `capie_sel` or `capie_cos`.
 * @param text Text (checked by the caller).
 */
inline void add_173(List& out, std::string_view prefix, std::string_view text) noexcept {
    std::array<char, records::max_name + 1> name{};
    static_cast<void>(std::snprintf(name.data(), name.size(), "%.*s_len", static_cast<int>(prefix.size()), prefix.data()));
    out.add(name.data(), static_cast<std::int64_t>(text.size()));
    const auto per = country::flag::chars_per_element;
    for (std::size_t index = 0; index * per < text.size(); ++index) {
        std::int64_t element = 0;
        for (std::size_t slot = 0; slot < per; ++slot) {
            const auto at = (index * per) + slot;
            element = (element * country::flag::pack_base) + (at < text.size() ? country::flag::code_of(text[at]) : 0);
        }
        static_cast<void>(std::snprintf(name.data(), name.size(), "%.*s_%zu", static_cast<int>(prefix.size()),
                                        prefix.data(), index));
        out.add(name.data(), element);
    }
}

/**
 * @brief Appends `<prefix>_len` and `<prefix>_<k>` of a name in packing B.
 * @param out List.
 * @param prefix `capie_sel` or `capie_sml`.
 * @param text Text (checked by the caller).
 */
inline void add_b(List& out, std::string_view prefix, std::string_view text) noexcept {
    std::array<char, records::max_name + 1> name{};
    static_cast<void>(std::snprintf(name.data(), name.size(), "%.*s_len", static_cast<int>(prefix.size()), prefix.data()));
    out.add(name.data(), static_cast<std::int64_t>(text.size()));
    const auto per = country::packing_b::chars_per_element;
    for (std::size_t index = 0; index * per < text.size(); ++index) {
        static_cast<void>(std::snprintf(name.data(), name.size(), "%.*s_%zu", static_cast<int>(prefix.size()),
                                        prefix.data(), index));
        out.add(name.data(), country::packing_b::element_of(text, index));
    }
}

/**
 * @brief The `pick` records of a flag stem.
 * @param base Page header (`page`).
 * @param stem Picked stem (17.3, 1..63).
 * @param cosmetic Scope country's current cosmetic tag (empty = none).
 * @return List; `overflow` when the stem cannot be expressed.
 */
inline List flag_pick(const List& base, std::string_view stem, std::string_view cosmetic) noexcept {
    List out = base;
    if (!build::packable_stem(stem)) {
        out.overflow = true;
        return out;
    }
    out.add("capie_sel_fmt", format_173);
    add_173(out, "capie_sel", stem);
    if (cosmetic.empty()) {
        out.add("capie_cos_len", 0);
    } else if (build::packable_stem(cosmetic)) {
        add_173(out, "capie_cos", cosmetic);
    } else {
        out.add("capie_cos_len", -1);  // not expressible: the mod leaves the snapshot absent
    }
    return out;
}

/**
 * @brief The `pick` records of a portrait item.
 * @param base Page header (`page`).
 * @param name Sprite name or path (packing B).
 * @param small Small name: `<name>_small` when that sprite exists, else the name (paths: the path).
 * @return List; `overflow` when a name cannot be expressed.
 */
inline List portrait_pick(const List& base, std::string_view name, std::string_view small) noexcept {
    List out = base;
    if (!country::packing_b::valid_name(name) || !country::packing_b::valid_name(small)) {
        out.overflow = true;
        return out;
    }
    out.add("capie_sel_fmt", format_b);
    add_b(out, "capie_sel", name);
    add_b(out, "capie_sml", small);
    return out;
}

}  // namespace capie::catalog::pick
