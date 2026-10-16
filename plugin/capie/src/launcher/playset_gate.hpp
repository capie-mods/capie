/**
 * @file playset_gate.hpp
 * @brief The player's on/off switch for CAPIE: the small "CAPIE" mod in the active playset.
 *
 * The Paradox Launcher cannot get an extra button, so the switch is a mod. Before it starts
 * Hearts of Iron IV the Paradox Launcher writes the playset it is about to run into
 * `Documents\Paradox Interactive\Hearts of Iron IV\dlc_load.json`, and the game reads its mod
 * list from that file:
 *
 * @code{.json}
 * {"enabled_mods":["mod/ugc_3274762952.mod","mod/capie.mod"],"disabled_dlcs":[]}
 * @endcode
 *
 * Each entry names a mod descriptor relative to the game's user folder. A Workshop mod is
 * always `mod/ugc_<Workshop ID>.mod`; a local mod is `mod/<file>.mod`. The launcher reads
 * the file once the game process it started exists (the playset is final then) and loads
 * CAPIE only when the CAPIE mod is listed. Everything else is vanilla:
 *
 *  - CAPIE mod listed                         -> `enabled`
 *  - valid file, CAPIE mod not listed         -> `disabled` (the player's choice; no notice)
 *  - missing, unreadable, too large, invalid
 *    JSON, no `enabled_mods` array, a non-string
 *    entry, a duplicate key, or no identity    -> `unknown` (no CAPIE, one notice)
 *
 * The game always starts; this check only decides whether the launcher injects anything.
 *
 * How the CAPIE mod is recognised lives in one value, `ModIdentity` (production values in
 * `config.hpp`): today the local descriptor path `mod/capie.mod`; once the mod is on the
 * Workshop its ID goes into `capie_mod_workshop_id` and `mod/ugc_<ID>.mod` matches as well.
 *
 * `parse_enabled_mods`, `mod_matches` and `evaluate_playset` are pure, so the tests drive every
 * format; `read_playset` only adds the bounded, shared, read-only file access.
 */
#pragma once
#include "config.hpp"
#include "playset_json.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace capie::launcher {

/// @brief Whether the CAPIE mod is enabled in the playset the game was started with.
enum class PlaysetState : std::uint8_t {
    enabled,   ///< the CAPIE mod is listed: CAPIE may load
    disabled,  ///< the file is valid and the CAPIE mod is not listed: vanilla
    unknown,   ///< the file could not be read or understood: vanilla, the player is told
};

/// @brief Result of the playset check.
struct PlaysetVerdict {
    PlaysetState state = PlaysetState::unknown;  ///< decision
    std::wstring reason;                         ///< one line for the log and the notice
};

/**
 * @brief Text of a playset state for the log.
 * @param state State.
 * @return Short tag.
 */
constexpr const wchar_t* playset_state_name(PlaysetState state) noexcept {
    switch (state) {
        case PlaysetState::enabled:
            return L"enabled";
        case PlaysetState::disabled:
            return L"disabled";
        case PlaysetState::unknown:
            break;
    }
    return L"unknown";
}

/**
 * @brief Whether an identity can recognise anything at all.
 * @param identity Identity.
 * @return `true` when it has a valid Workshop ID or at least one descriptor path.
 */
inline bool identity_usable(const ModIdentity& identity) noexcept {
    return detail::all_digits(identity.workshop_id) ||
           std::ranges::any_of(identity.descriptors, [](const std::string& descriptor) { return !descriptor.empty(); });
}

/**
 * @brief Whether one `enabled_mods` entry names the CAPIE mod.
 * @param entry Entry from dlc_load.json.
 * @param identity Identity.
 * @return `true` for `mod/ugc_<workshop_id>.mod` or one of the descriptor paths (case-insensitive,
 *         either slash).
 */
inline bool mod_matches(std::string_view entry, const ModIdentity& identity) {
    const std::string normal = detail::normalise_descriptor(entry);
    if (detail::all_digits(identity.workshop_id) && normal == "mod/ugc_" + identity.workshop_id + ".mod") {
        return true;
    }
    return std::ranges::any_of(identity.descriptors, [&normal](const std::string& descriptor) {
        return !descriptor.empty() && normal == detail::normalise_descriptor(descriptor);
    });
}

/**
 * @brief The decision for one dlc_load.json content.
 * @param json File content.
 * @param identity How the CAPIE mod is recognised.
 * @return Verdict with a reason.
 */
inline PlaysetVerdict evaluate_playset(std::string_view json, const ModIdentity& identity) {
    if (!identity_usable(identity)) {
        return {.state = PlaysetState::unknown, .reason = L"no CAPIE mod identity is configured"};
    }
    const auto mods = parse_enabled_mods(json);
    if (!mods) {
        return {.state = PlaysetState::unknown,
                .reason = L"dlc_load.json is not valid or has no single enabled_mods list of strings"};
    }
    for (const auto& entry : *mods) {
        if (mod_matches(entry, identity)) {
            return {.state = PlaysetState::enabled, .reason = L"CAPIE mod enabled in the playset: " + from_utf8(entry)};
        }
    }
    return {.state = PlaysetState::disabled,
            .reason = L"CAPIE mod not enabled in the playset (" + std::to_wstring(mods->size()) + L" mods enabled)"};
}

/**
 * @brief Reads dlc_load.json (shared, read-only, bounded) and decides.
 * @param path Absolute path of dlc_load.json.
 * @param identity How the CAPIE mod is recognised.
 * @return Verdict; any file problem is `unknown`.
 * @note Opened with full sharing so the Paradox Launcher and the game are never disturbed.
 */
inline PlaysetVerdict read_playset(const std::wstring& path, const ModIdentity& identity) {
    if (path.empty()) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json path is unknown"};
    }
    const Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return {.state = PlaysetState::unknown,
                .reason = L"dlc_load.json cannot be opened (error " + std::to_wstring(GetLastError()) + L"): " + path};
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < 0 ||
        std::cmp_greater(size.QuadPart, dlc_load_read_limit)) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json size is not usable: " + path};
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD actual = 0;
    if (!content.empty() &&
        (ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &actual, nullptr) == 0 ||
         actual != content.size())) {
        return {.state = PlaysetState::unknown, .reason = L"dlc_load.json cannot be read: " + path};
    }
    return evaluate_playset(content, identity);
}

}  // namespace capie::launcher
