/**
 * @file config.hpp
 * @brief Every fixed name, limit and timeout of the CAPIE launcher, plus the run-time policy object.
 *
 * The launcher is the small Windows program that players put into the Steam launch options
 * (`"...\RenameAnythingLauncher.exe" %command%`) or start from a shortcut. It starts the
 * Paradox Launcher exactly as Steam would have, waits until the player starts Hearts of
 * Iron IV from it, and loads `capie_core.dll` and then `capie_editor.dll` into that one
 * game process once the game has finished loading. It never attaches to a game it did not
 * start, and it never stops a game from running: when anything does not match (unknown
 * game build, damaged bundle, foreign process), the game runs without CAPIE and the player
 * is told why.
 *
 * This header holds data only. Values that a test must be able to change (the pinned game
 * hash, the bundle, the game log path, the timeouts) live in `Policy`; the production
 * values are built in `launcher_main.cpp` from these constants and from
 * `launcher_manifest.generated.hpp`, which the build writes from the reviewed binaries.
 * No other launcher file defines a hash, a bundle or game file name, or a timeout.
 *
 * Shared with CAPIE: the game hash and both DLL names come from `core/game_build.hpp`, so
 * the launcher, the injector and the DLLs cannot disagree about which build they support.
 */
#pragma once
#include "../core/game_build.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace capie::launcher {

/// @brief File name of the launcher itself (the bootstrap and the Steam launch option use it).
inline constexpr wchar_t launcher_exe_name[] = L"RenameAnythingLauncher.exe";
/// @brief Bundle subfolder, next to the launcher, that holds the CAPIE binaries.
inline constexpr wchar_t runtime_dir_name[] = L"runtime";
/// @brief File name of the CAPIE injector inside the bundle.
inline constexpr wchar_t injector_exe_name[] = L"capie_injector.exe";
/// @brief Paradox Launcher bootstrapper in the game folder; Steam's `%command%` points to it.
inline constexpr wchar_t paradox_launcher_exe_name[] = L"dowser.exe";
/// @brief Game executable name (same constant as the injector uses).
inline constexpr const wchar_t* game_exe_name = capie::core::game_exe_name;
/// @brief Steam application id of Hearts of Iron IV.
inline constexpr wchar_t steam_app_id[] = L"394360";
/// @brief Game folder name inside `steamapps\common`.
inline constexpr wchar_t steam_game_folder[] = L"steamapps\\common\\Hearts of Iron IV";
/// @brief Steam library list, relative to a Steam root.
inline constexpr wchar_t steam_library_list[] = L"steamapps\\libraryfolders.vdf";
/// @brief Steam uninstall registry key of the game (its `InstallLocation` value is the game folder).
inline constexpr wchar_t steam_uninstall_key[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 394360";
/// @brief Steam's per-user registry key (value `SteamPath`).
inline constexpr wchar_t steam_user_key[] = L"Software\\Valve\\Steam";

/// @brief Folder under %LOCALAPPDATA% shared with the bootstrap.
inline constexpr wchar_t app_data_dir_name[] = L"RenameAnything";
/// @brief Launcher log file name inside the app data folder.
inline constexpr wchar_t log_file_name[] = L"launcher.log";
/// @brief Suffix of the single rotated log file.
inline constexpr wchar_t rotated_log_suffix[] = L".1";
/// @brief File remembering the game chosen in shortcut mode (UTF-16 with BOM, as in the accepted launcher).
inline constexpr wchar_t remembered_game_file[] = L"hoi4.path";
/// @brief Game log, relative to the user's Documents folder (`gameDataPath` in launcher-settings.json).
inline constexpr wchar_t game_log_relative[] = L"Paradox Interactive\\Hearts of Iron IV\\logs\\game.log";

/// @brief Log size that triggers rotation at launcher start.
inline constexpr std::size_t log_rotate_bytes = std::size_t{1} << 20U;
/// @brief Largest part of the game log read when looking for the readiness marker.
inline constexpr std::size_t game_log_read_limit = std::size_t{4} << 20U;
/// @brief Maximum stored helper diagnostic output.
inline constexpr std::size_t helper_output_limit = 16384;
/// @brief Windows command line limit, in characters, including the terminator.
inline constexpr std::size_t command_line_limit = 32767;
/// @brief How many parents are followed when a game escaped the job object.
inline constexpr unsigned max_ancestry_depth = 4;

/**
 * @brief Text the game writes to game.log near the end of loading, before the main menu.
 *
 * HOI4 1.19.3.0 runs its history twice while resetting the game state ("from -1.1.1.1 to
 * 2.1.1.1", then "from 2.1.1.1 to <start date>"), after every database and the map are
 * loaded and right before the front end (main menu) is shown. The string is in the pinned
 * hoi4.exe exactly once. It is a property of the pinned build, like the signatures.
 */
inline constexpr char ready_log_marker[] = "Executing History from";
/// @brief How many marker lines mean "loading finished".
inline constexpr std::size_t ready_marker_count = 2;

/// @brief Process exit codes (the numbers of the accepted launcher, kept).
enum class ExitCode : int {
    ok = 0,                 ///< finished; for the wrapper: the wrapped game tree ended
    usage = 2,              ///< invalid command line or wrapped command
    unsupported_game = 10,  ///< --verify-only: game executable is not the pinned build
    invalid_bundle = 20,    ///< --verify-only: bundle missing or modified
    game_start = 30,        ///< the wrapped program could not be started
    helper_failure = 40,    ///< reserved: injection failure (the wrapper reports it in the log, exits 0)
    unexpected = 50,        ///< unexpected internal error
};

/**
 * @brief What the launcher does when the game hash is not the pinned one.
 *
 * Only `run_without_capie` exists. API decision 10(a) allows an opt-in runtime signature
 * scanner inside CAPIE for unknown builds; when that exists, the launcher gets a second
 * value here (for example `scan_signatures_opt_in`), read from the player's settings, and
 * hands the decision to the DLL instead of skipping it. Nothing of that is implemented.
 */
enum class UnknownBuildPolicy : std::uint8_t { run_without_capie };

/// @brief Timeouts and polling, all in milliseconds.
struct Timeouts {
    /// @brief Poll period of the process tree, the game window and the game log.
    DWORD poll_ms = 250;
    /// @brief How long the game may take to appear in the started tree (player picks a playset first).
    DWORD discovery_ms = 30U * 60U * 1000U;
    /// @brief How long the game may take, from its creation, to reach the main menu.
    DWORD readiness_ms = 15U * 60U * 1000U;
    /// @brief Quiet period after readiness is first seen; window and process are checked again after it.
    DWORD settle_ms = 15000;
    /// @brief Bound for the "is the game window responding" probe.
    DWORD window_probe_ms = 5000;
    /// @brief Outer deadline for one injector run (the injector's own remote waits are 2 x 30 s).
    DWORD helper_ms = 90000;
};

/// @brief One file of the CAPIE bundle.
struct BundleEntry {
    std::wstring name;   ///< file name inside `runtime\`
    std::string sha256;  ///< 64 lower-case hex characters
    bool dll = false;    ///< PE characteristic expected (DLL or EXE)
};

/// @brief Index of `capie_core.dll` in the bundle (injected first).
inline constexpr std::size_t bundle_core = 0;
/// @brief Index of `capie_editor.dll` in the bundle (injected second).
inline constexpr std::size_t bundle_editor = 1;
/// @brief Index of `capie_injector.exe` in the bundle.
inline constexpr std::size_t bundle_injector = 2;
/// @brief Number of bundle files.
inline constexpr std::size_t bundle_size = 3;
/// @brief Bundle description, in the order of the indexes above.
using BundleManifest = std::array<BundleEntry, bundle_size>;

/// @brief Everything a launcher run depends on that tests replace.
struct Policy {
    /// @brief Pinned game hash (lower-case hex).
    std::string game_sha256;
    /// @brief Folder that contains `runtime\` (the launcher's own folder in production).
    std::wstring bundle_root;
    /// @brief Bundle files and their pinned hashes.
    BundleManifest bundle;
    /// @brief Absolute path of game.log.
    std::wstring game_log;
    /// @brief Unknown build handling (see UnknownBuildPolicy).
    UnknownBuildPolicy unknown_build = UnknownBuildPolicy::run_without_capie;
    /// @brief Timeouts.
    Timeouts timeouts;
};

}  // namespace capie::launcher
