/**
 * @file capie_core.cpp
 * @brief Composition root of `capie_core.dll`: country names, CAPIE status text and the capability export.
 *
 * This DLL hooks the three localisation lookups of the pinned `hoi4.exe` so that RN1
 * cosmetic tags show the player's country name and adjective, and so that the mod's
 * status line reads "CAPIE 1.0 loaded" / "CAPIE 1.0 załadowane". All logic lives in
 * `features/country/country_names.hpp`; this file only verifies the image and installs
 * the hooks.
 *
 * It does not write game files, saves or `CCountry` fields. The game updates its own
 * name caches and map labels from the localisation answers.
 *
 * `RNInitialize` (remote thread from the injector): SHA-256 of the executable (10), PE
 * headers (11), `.text` (12), the three masked signatures (20), MinHook (30/31/32), then
 * `active`. A repeated call after success returns 0; after a failure it runs again (as the
 * accepted country DLL did). If activation fails after enabling some hooks, every hook is
 * disabled and the trampolines are kept until process exit, because a callback may be in
 * flight; residual hooks forward to the game while `active` is false.
 *
 * `RNCountryCapabilities` returns `country_capabilities_v1` only while active. The editor
 * bridge queries it before every country Apply.
 */
#include "../core/game_build.hpp"
#include "../core/hook_set.hpp"
#include "../core/init_codes.hpp"
#include "../core/pe_image.hpp"
#include "../core/sha256.hpp"
#include "../core/signature_table.hpp"
#include "../features/country/capability.hpp"
#include "../features/country/country_names.hpp"

#include <MinHook.h>

#include <iterator>

namespace capie::country {

using core::InitCode;

/// @brief Longest path buffer for the executable path.
inline constexpr DWORD max_path_chars = 32768;
/// @brief Debugger message after a successful initialization.
inline constexpr char initialized_message[] =
    "CAPIE 1.0: RN1 country localisation hooks initialized (capie_core, pinned 1.19.3.0)\n";

/**
 * @brief Body of `RNInitialize`.
 * @return Result code.
 */
inline InitCode initialize() {
    if (active.load()) return InitCode::ok;
    wchar_t path[max_path_chars]{};
    if (GetModuleFileNameW(nullptr, path, max_path_chars) == 0 || core::sha256_file(path) != core::game_exe_sha256) {
        return InitCode::unsupported_executable;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    core::TextSection text{};
    if (const auto located = core::locate_text(base, text); located != InitCode::ok) return located;
    if (!core::verify_all(text, core::country_signatures)) return InitCode::signature_mismatch;
    const core::HookSpec hooks[] = {
        {"country.lookup_n", core::rva::loc_lookup_n, reinterpret_cast<void*>(&lookup_n), reinterpret_cast<void**>(&original_n)},
        {"country.lookup_z", core::rva::loc_lookup_z, reinterpret_cast<void*>(&lookup_z), reinterpret_cast<void**>(&original_z)},
        {"country.exists", core::rva::loc_exists, reinterpret_cast<void*>(&exists), reinterpret_cast<void**>(&original_exists)},
    };
    if (const auto installed = core::install_all_at_once(base, hooks, std::size(hooks)); installed != InitCode::ok) {
        active.store(false);
        return installed;
    }
    active.store(true);
    OutputDebugStringA(initialized_message);
    return InitCode::ok;
}

}  // namespace capie::country

/**
 * @brief Read-only capability query for the editor bridge.
 * @return `country_capabilities_v1` while initialized, otherwise 0. Loading the DLL alone is not enough.
 */
extern "C" __declspec(dllexport) DWORD WINAPI RNCountryCapabilities() noexcept {
    return capie::country::active.load(std::memory_order_acquire) ? capie::country::country_capabilities_v1 : 0;
}

/**
 * @brief Initializes the country hooks (remote thread from the injector, outside DllMain).
 * @param unused Ignored.
 * @return 0 on success; see `capie::core::InitCode`.
 */
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void* unused) {
    static_cast<void>(unused);
    try {
        return capie::core::to_exit_code(capie::country::initialize());
    } catch (...) {
        return capie::core::to_exit_code(capie::core::InitCode::unexpected_exception);
    }
}

/**
 * @brief Keeps thread notifications (static CRT); no work in the loader lock.
 * @return TRUE.
 */
BOOL WINAPI DllMain(HINSTANCE /*instance*/, DWORD /*reason*/, LPVOID /*reserved*/) {
    // Game exit releases the module. Unloading hooks in a live process is unsupported.
    return TRUE;
}
