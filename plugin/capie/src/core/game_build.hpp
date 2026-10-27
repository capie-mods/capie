/**
 * @file game_build.hpp
 * @brief The one game build CAPIE supports, and CAPIE's own identity strings and version numbers.
 *
 * CAPIE refuses to do anything unless the running executable hashes to
 * `game_exe_sha256`. The RVAs and byte signatures in `signature_table.hpp` and the
 * layouts in `engine/layout.hpp` are valid for this build only. A future runtime scanner
 * (see `scanner.hpp`) may accept other builds, but only under the all-or-nothing rule in
 * docs/API-DECISIONS.md, decision 10(a).
 *
 * This file holds data only. It does not read files or memory.
 */
#pragma once
#include <cstdint>

namespace capie::core {

/// @brief Lower-case SHA-256 of the only accepted `hoi4.exe` (HOI4 1.19.4.0, Steam, x64).
inline constexpr char game_exe_sha256[] = "b6650c85002374218e60598bd04aaa26cbad996fedce77126fcf92fc6bd6d888";
/// @brief File name of the game executable, compared case-insensitively by the tools.
inline constexpr wchar_t game_exe_name[] = L"hoi4.exe";
/// @brief Human-readable version of the accepted build, used in messages only.
inline constexpr char game_version[] = "1.19.4.0";
/// @brief Preferred image base of the accepted build; RVAs are relative to the real base.
inline constexpr std::uintptr_t game_preferred_base = 0x140000000;

/// @brief CAPIE product version shown to players ("CAPIE 1.5 loaded").
inline constexpr char capie_version[] = "1.5";
/// @brief Full release version, answered for the `CAPIE_BUILD` localisation key.
inline constexpr char capie_release[] = "1.5.1";
/// @brief Script API level (CONTRACT.md section 1): sent as `capie_cap`, answered for `CAPIE_API`.
inline constexpr unsigned capie_api_level = 7;
/// @brief Text of `capie_api_level` for the `CAPIE_API` localisation key.
inline constexpr char capie_api_text[] = "7";
/// @brief Release as one integer, major * 10000 + minor * 100 + patch; sent as `capie_ver`.
inline constexpr unsigned capie_build_number = 10501;

/// @brief File name of the country/localisation DLL.
inline constexpr wchar_t core_dll_name[] = L"capie_core.dll";
/// @brief File name of the editor bridge DLL.
inline constexpr wchar_t editor_dll_name[] = L"capie_editor.dll";
/// @brief Log file written next to the editor DLL.
inline constexpr wchar_t editor_log_name[] = L"capie_editor.log";
/// @brief Log file written next to the core DLL (name layer; counts only, never a name).
inline constexpr wchar_t core_log_name[] = L"capie_core.log";

}  // namespace capie::core
