/**
 * @file catalog_records.hpp
 * @brief The command records of a catalog action (API level 9, CONTRACT.md 21.3), built as plain data.
 *
 * Every action of a catalog view is forwarded as a normal command; CAPIE adds these temporary
 * records (after removing every incoming `capie_*` record, `capie_v1_slot` included):
 *
 *     capie_cap, capie_ver          API level and build number (as for an editor Apply)
 *     capie_catalog                 1 = this action is served; 0 = not available (feature off, or a
 *                                   catalog that is pending its spikes); -code = sentinel
 *     capie_cat                     3 = vp
 *     capie_cn                      items in the view
 *     capie_pg, capie_pgs           page shown (1-based) and pages (0 and 0 when empty)
 *     capie_pn                      items on the page
 *     capie_par, capie_ppar         slot set and preview set to show (0 for the VP view)
 *     capie_st                      vp: the state's numeric id (0 = the scope holds no state)
 *     capie_vp_<k>, capie_vpv_<k>   vp: province id and VP value of row k, k < capie_cn (at most 32)
 *
 * With `capie_catalog` other than 1 only `capie_cap`, `capie_ver` and `capie_catalog` are sent.
 * Pure data: no game memory, no allocation. The editor DLL turns the list into game records.
 */
#pragma once
#include "../../core/game_build.hpp"
#include "vp_view.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace capie::catalog::records {

/// @brief Longest record name (`capie_catalog`).
inline constexpr std::size_t max_name = 15;
/// @brief Most records one catalog action adds (10 header + st + 2 x 32 rows).
inline constexpr std::size_t max_records = 10 + 1 + (2 * vp::max_rows);
/// @brief `capie_cat` of the VP view.
inline constexpr std::int64_t catalog_vp = 3;

/// @brief Sentinel codes (`capie_catalog = -code`, spec 5.4).
enum class Sentinel : std::int64_t {
    window = 1,         ///< The window is not visible, or it failed the consistency check.
    registry_full = 2,  ///< The view registry is full.
    view_invalid = 3,   ///< The view is invalid (slot sets; flag and portrait views only).
    slot = 4,           ///< `pick` without a valid `capie_v1_slot`.
    thread = 5,         ///< `ThreadForbidCount != 0` at the click.
};

/// @brief One record.
struct Record {
    std::array<char, max_name + 1> name{};  ///< Name, NUL-terminated.
    std::int64_t value{};                   ///< Unscaled value (may be negative for a sentinel).
};

/// @brief The records of one action.
struct List {
    std::array<Record, max_records> items{};  ///< Records in order.
    std::size_t count{};                      ///< Used.
    bool overflow{};                          ///< A name or the count did not fit.

    /**
     * @brief Appends a record.
     * @param name Name (at most 15 bytes).
     * @param value Value.
     */
    void add(std::string_view name, std::int64_t value) noexcept {
        if (count >= items.size() || name.size() > max_name || name.empty()) {
            overflow = true;
            return;
        }
        auto& item = items[count++];
        std::memcpy(item.name.data(), name.data(), name.size());
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
 * @brief The header every action carries: `capie_cap`, `capie_ver`, `capie_catalog`.
 * @param catalog `capie_catalog` value.
 * @return List.
 */
inline List header(std::int64_t catalog) noexcept {
    List out{};
    out.add("capie_cap", static_cast<std::int64_t>(core::capie_api_level));
    out.add("capie_ver", static_cast<std::int64_t>(core::capie_build_number));
    out.add("capie_catalog", catalog);
    return out;
}

/**
 * @brief A sentinel: the header with `capie_catalog = -code`.
 * @param code Reason.
 * @return List.
 */
inline List sentinel(Sentinel code) noexcept {
    return header(-static_cast<std::int64_t>(code));
}

/**
 * @brief An action CAPIE does not serve (feature off, or a catalog pending its spikes).
 * @return The header with `capie_catalog = 0`.
 */
inline List unavailable() noexcept {
    return header(0);
}

/**
 * @brief The records of a `vp` action.
 * @param list The state's VPs (`state` 0 = no state; an empty list = no VP).
 * @return List.
 */
inline List vp_view(const vp::List& list) noexcept {
    List out = header(1);
    const auto count = static_cast<std::int64_t>(list.count);
    out.add("capie_cat", catalog_vp);
    out.add("capie_cn", count);
    out.add("capie_pg", count != 0 ? 1 : 0);
    out.add("capie_pgs", count != 0 ? 1 : 0);
    out.add("capie_pn", count);
    out.add("capie_par", 0);
    out.add("capie_ppar", 0);
    out.add("capie_st", list.state);
    std::array<char, max_name + 1> name{};
    for (std::size_t k = 0; k < list.count; ++k) {
        static_cast<void>(std::snprintf(name.data(), name.size(), "capie_vp_%zu", k));
        out.add(name.data(), list.rows[k].id);
        static_cast<void>(std::snprintf(name.data(), name.size(), "capie_vpv_%zu", k));
        out.add(name.data(), list.rows[k].value);
    }
    return out;
}

}  // namespace capie::catalog::records
