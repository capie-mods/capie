/**
 * @file capie_core.cpp
 * @brief Composition root of `capie_core.dll`: country names, the name layer, status text and the capability export.
 *
 * This DLL hooks the three localisation lookups of the pinned `hoi4.exe` so that RN1 keys
 * show a country name and adjective, and so that the mod's status line reads "CAPIE 1.2
 * loaded" / "CAPIE 1.2 załadowane", and the public keys `CAPIE_STATUS`, `CAPIE_API`,
 * `CAPIE_BUILD` answer "CAPIE 1.2", "4" and "1.2.0" (`features/country/country_names.hpp`).
 *
 * Name layer (API level 4, `features/country/name_layer*.hpp`): three more hooks
 * (`UpdateNames`, `BuildNameKey`, `SetVariable::Execute`) show a name stored in the country's
 * own variables, in front of the cosmetic tag and the base name, without touching the cosmetic
 * tag, flag or colour. Its pins are checked separately; a failed pin leaves the layer off
 * (logged) and the localisation hooks on.
 *
 * It does not write game files, saves, variables, cosmetic tags or `CCountry` fields. The game
 * updates its own name caches and map labels from the localisation answers.
 *
 * `RNInitialize` (remote thread from the injector): SHA-256 of the executable (10), PE
 * headers (11), `.text` (12), the three masked signatures (20), the layer pins (layer off on
 * failure), MinHook (30/31/32), then `active`. A repeated call after success returns 0; after
 * a failure it runs again (as the accepted country DLL did). If activation fails after
 * enabling some hooks, every hook is disabled and the trampolines are kept until process exit,
 * because a callback may be in flight; residual hooks forward to the game while `active` is
 * false.
 *
 * `RNCountryCapabilities` returns `country_capabilities_v2` while active with the layer,
 * `country_capabilities_v1` while active without it. The editor bridge queries it before
 * every country Apply and requires v2.
 *
 * Log: `capie_core.log` next to this DLL (lifecycle in every flavor, layer diagnostics in the
 * debug flavor; counts only, never the text of a name). A missing log does not stop CAPIE.
 */
#include "../core/game_build.hpp"
#include "../core/hook_set.hpp"
#include "../core/init_codes.hpp"
#include "../core/pe_image.hpp"
#include "../core/sha256.hpp"
#include "../core/signature_table.hpp"
#include "../features/country/capability.hpp"
#include "../features/country/country_names.hpp"
#include "../features/country/name_layer_hooks.hpp"
#include "../features/country/name_layer_pins.hpp"

#include <MinHook.h>

#include <cstdio>
#include <cwchar>
#include <iterator>

namespace capie::country {
namespace {

using core::InitCode;

/// @brief Longest path buffer for the executable path.
constexpr DWORD max_path_chars = 32768;
/// @brief Debugger message after a successful initialization.
constexpr char initialized_message[] = "CAPIE 1.2: country hooks initialized (capie_core, pinned 1.19.3.0)\n";
/// @brief Size of a lifecycle line buffer.
constexpr std::size_t line_bytes = 200;
/// @brief Number of localisation hooks (always installed).
constexpr std::size_t localisation_hooks = 3;

/// @brief Memory policy of the name layer.
const core::ProcessMemory layer_memory{};
/// @brief The name layer of this process.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): hook state is a process singleton
layer::NameLayer<core::ProcessMemory> name_layer{layer_memory};

/**
 * @brief Writes one log line (appends CRLF) if its channel is enabled.
 * @param channel Channel.
 * @param line Line without line break.
 */
void write_line(core::logging::Channel channel, const char* line) noexcept {
    if (!core::logging::enabled(channel)) {
        return;
    }
    char buffer[line_bytes + 3]{};
    const int count = std::snprintf(buffer, sizeof(buffer), "%s\r\n", line);
    if (count > 0) {
        core::logging::write_log(buffer, core::logging::stored_bytes(count, sizeof(buffer)));
    }
}

/**
 * @brief Hook body of `CCountry_UpdateNames`.
 * @param country Country object.
 * @return The original's result.
 */
std::uint64_t update_names_hook(core::uptr country) noexcept {
    return name_layer.update_names(country);
}

/**
 * @brief Hook body of `CCountry_BuildNameKey`; the tag may be replaced by the layer key.
 * @param out Result string.
 * @param tag Tag string.
 * @param a3 Passed through.
 * @param a4 Passed through.
 * @param a5 Passed through.
 * @param a6 Passed through.
 * @param a7 Passed through.
 * @param a8 Passed through.
 * @return The original's result.
 */
void* build_name_key_hook(void* out, const void* tag, void* a3, void* a4, void* a5, void* a6, void* a7,
                          char a8) noexcept {
    return name_layer.build_name_key(out, tag, a3, a4, a5, a6, a7, a8);
}

/**
 * @brief Hook body of `CSetVariableEffect::Execute`.
 * @param effect Effect object.
 * @param context Execution context.
 */
void set_variable_hook(core::uptr effect, core::uptr context) noexcept {
    name_layer.set_variable(effect, context);
}

/**
 * @brief Opens `capie_core.log` next to this DLL (best effort; CAPIE runs without it).
 */
void open_log() noexcept {
    if (core::logging::log_file != INVALID_HANDLE_VALUE) {
        return;
    }
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&open_log), &self) == 0) {
        return;
    }
    wchar_t path[max_path_chars]{};
    if (GetModuleFileNameW(self, path, max_path_chars) == 0) {
        return;
    }
    auto* slash = std::wcsrchr(path, L'\\');
    if (slash == nullptr ||
        wcscpy_s(slash + 1, max_path_chars - static_cast<std::size_t>(slash + 1 - path), core::core_log_name) != 0) {
        return;
    }
    core::logging::log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

/**
 * @brief Body of `RNInitialize`.
 * @return Result code.
 */
InitCode initialize() {
    if (active.load()) {
        return InitCode::ok;
    }
    wchar_t path[max_path_chars]{};
    if (GetModuleFileNameW(nullptr, path, max_path_chars) == 0 || core::sha256_file(path) != core::game_exe_sha256) {
        return InitCode::unsupported_executable;
    }
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    core::TextSection text{};
    if (const auto located = core::locate_text(base, text); located != InitCode::ok) {
        return located;
    }
    if (!core::verify_all(text, core::country_signatures)) {
        return InitCode::signature_mismatch;
    }
    open_log();
    const char* const failed_pin = layer::pins::first_failed_pin(text);
    const bool layer_ok = failed_pin == nullptr;
    const core::HookSpec hooks[] = {
        {"country.lookup_n", core::rva::loc_lookup_n, reinterpret_cast<void*>(&lookup_n),
         reinterpret_cast<void**>(&original_n)},
        {"country.lookup_z", core::rva::loc_lookup_z, reinterpret_cast<void*>(&lookup_z),
         reinterpret_cast<void**>(&original_z)},
        {"country.exists", core::rva::loc_exists, reinterpret_cast<void*>(&exists),
         reinterpret_cast<void**>(&original_exists)},
        {"name.update_names", core::rva::update_names, reinterpret_cast<void*>(&update_names_hook),
         reinterpret_cast<void**>(&name_layer.originals().update)},
        {"name.build_name_key", core::rva::build_name_key, reinterpret_cast<void*>(&build_name_key_hook),
         reinterpret_cast<void**>(&name_layer.originals().build)},
        {"name.set_variable_execute", core::rva::set_variable_execute, reinterpret_cast<void*>(&set_variable_hook),
         reinterpret_cast<void**>(&name_layer.originals().set_variable)},
    };
    const std::size_t hook_count = layer_ok ? std::size(hooks) : localisation_hooks;
    char line[line_bytes]{};
    if (const auto installed = core::install_all_at_once(base, hooks, hook_count); installed != InitCode::ok) {
        active.store(false);
        static_cast<void>(std::snprintf(line, sizeof(line), "CAPIE_CORE init failed code=%u",
                                        static_cast<unsigned>(core::to_exit_code(installed))));
        write_line(core::logging::Channel::error, line);
        return installed;
    }
    if (layer_ok) {
        name_layer.activate(reinterpret_cast<core::uptr>(base), &write_line);
    }
    active.store(true);
    static_cast<void>(
        std::snprintf(line, sizeof(line), "CAPIE_CORE %s initialized: %zu hooks, name layer %s %s, flavor=%s",
                      core::capie_version, hook_count, name_layer.enabled() ? "on" : "off", layer_ok ? "" : failed_pin,
                      core::logging::debug_diagnostics ? "debug" : "release"));
    write_line(core::logging::Channel::lifecycle, line);
    OutputDebugStringA(initialized_message);
    return InitCode::ok;
}

}  // namespace
}  // namespace capie::country

/**
 * @brief Read-only capability query for the editor bridge.
 * @return `country_capabilities_v2` while initialized with the name layer, `country_capabilities_v1`
 *         without it, otherwise 0. Loading the DLL alone is not enough.
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
extern "C" __declspec(dllexport) DWORD WINAPI RNCountryCapabilities() noexcept {
    if (!capie::country::active.load(std::memory_order_acquire)) {
        return 0;
    }
    return capie::country::name_layer.enabled() ? capie::country::country_capabilities_v2
                                                : capie::country::country_capabilities_v1;
}

/**
 * @brief Initializes the country hooks (remote thread from the injector, outside DllMain).
 * @param unused Ignored.
 * @return 0 on success; see `capie::core::InitCode`.
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
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
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage): loader entry point, external by definition
BOOL WINAPI DllMain(HINSTANCE /*instance*/, DWORD /*reason*/, LPVOID /*reserved*/) {
    // Game exit releases the module. Unloading hooks in a live process is unsupported.
    return TRUE;
}
