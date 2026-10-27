/**
 * @file capie_core.cpp
 * @brief Composition root of `capie_core.dll`: country names, the name layer, status text and the capability export.
 *
 * This DLL hooks the three localisation lookups of the pinned `hoi4.exe` so that RN1 keys
 * show a country name and adjective, and so that the mod's status line reads "CAPIE 1.5
 * loaded" / "CAPIE 1.5 załadowane", and the public keys `CAPIE_STATUS`, `CAPIE_API`,
 * `CAPIE_BUILD` answer "CAPIE 1.5", "7" and "1.5.1" (`features/country/country_names.hpp`).
 *
 * Name layers (API levels 4 and 5, `features/country/name_layer*.hpp`): four more hooks
 * (`UpdateNames`, `BuildNameKey`, `SetVariable::Execute`, `CFaction::GetName`) show a country
 * name and a faction name stored in the country's (the faction leader's) own variables. The
 * country name goes in front of the cosmetic tag and the base name, without touching the
 * cosmetic tag, flag or colour; the faction name goes in front of the stored faction name,
 * which stays as it is. Their pins are checked separately; a failed pin leaves both layers off
 * (logged) and the localisation hooks on.
 *
 * Leader portrait layer (API level 7, `features/country/portrait_layer*.hpp`): one more hook
 * (`ResolvePortrait`, 139DF20) shows the civilian portrait chosen for a ruling country leader;
 * its cache is filled from the UpdateNames and set_variable hooks. Own pins; it needs the name
 * layer's pins as well.
 *
 * It does not write game files, saves, variables, cosmetic tags or `CCountry` fields. The game
 * updates its own name caches and map labels from the localisation answers.
 *
 * `RNInitialize` (remote thread from the injector): SHA-256 of the executable (10), PE
 * headers (11), `.text` (12), the three masked signatures (20), the layer pins (layer off on
 * failure), MinHook (30/31/32), then `active`. A copy loaded under another file name is refused
 * (40) and the module is pinned before the first hook (14). A repeated call after success returns
 * 0; a call while another one runs returns 33; after a failure it runs again (as the accepted
 * country DLL did). If activation fails after
 * enabling some hooks, every hook is disabled and the trampolines are kept until process exit,
 * because a callback may be in flight; residual hooks forward to the game while `active` is
 * false.
 *
 * `RNCountryCapabilities` returns a v4 value with the portrait layer (flag bit when the flag
 * layer is on too), `country_capabilities_v3` with the name and flag layers,
 * `country_capabilities_v2` with the name layers only,
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
#include "../features/country/flag_layer_hooks.hpp"
#include "../features/country/flag_layer_pins.hpp"
#include "../features/country/name_layer_hooks.hpp"
#include "../features/country/name_layer_pins.hpp"
#include "../features/country/portrait_layer_hooks.hpp"
#include "../features/country/portrait_layer_pins.hpp"

#include <atomic>
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
constexpr char initialized_message[] = "CAPIE 1.5: country hooks initialized (capie_core, pinned 1.19.4.0)\n";
/// @brief Size of a lifecycle line buffer.
constexpr std::size_t line_bytes = 200;
/// @brief Number of localisation hooks (always installed).
constexpr std::size_t localisation_hooks = 3;
/// @brief Number of name-layer hooks (country and faction names).
constexpr std::size_t name_layer_hooks = 4;
/// @brief Number of flag-layer hooks.
constexpr std::size_t flag_layer_hooks = 3;
/// @brief Most hooks installed (localisation, name, flag and portrait layers).
constexpr std::size_t max_hooks = localisation_hooks + name_layer_hooks + flag_layer_hooks + 1;

/// @brief Memory policy of the name layer.
const core::ProcessMemory layer_memory{};
/// @brief The name layer of this process.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): hook state is a process singleton
layer::NameLayer<core::ProcessMemory> name_layer{layer_memory};
/// @brief Memory policy of the flag layer (reads plus the atlas slot-key write).
const flag::FlagMemory flag_memory{};
/// @brief The flag layer of this process.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): hook state is a process singleton
flag::FlagLayer<flag::FlagMemory> flag_layer{flag_memory};
/// @brief The leader portrait layer of this process.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): hook state is a process singleton
portrait::PortraitLayer<core::ProcessMemory> portrait_layer{layer_memory};

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
    const auto result = name_layer.update_names(country);
    static_cast<void>(flag_layer.update(country));  // after the original: the cosmetic tag is current
    static_cast<void>(portrait_layer.update(country));
    return result;
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
    if (flag_layer.enabled() &&
        engine::string_equals(layer_memory, effect + core::off::set_variable_name, flag::commit_variable)) {
        static_cast<void>(flag_layer.refresh_all());
    }
    if (portrait_layer.enabled() &&
        engine::string_equals(layer_memory, effect + core::off::set_variable_name, portrait::commit_variable)) {
        static_cast<void>(portrait_layer.refresh_all());
    }
}

/**
 * @brief Hook body of `UpdateFlagAtlas`.
 * @param atlas Atlas.
 * @param gfx Graphics object.
 * @return The original's result.
 */
std::uint64_t flag_atlas_hook(core::uptr atlas, core::uptr gfx) noexcept {
    return flag_layer.atlas_update(atlas, gfx);
}

/**
 * @brief Hook body of `LoadCountryFlag` (8 arguments, forwarded unchanged).
 * @return The original's status.
 */
int load_flag_hook(core::uptr atlas, core::uptr country, void* dst, int a4, int a5, int a6, int a7, void* a8) noexcept {
    return flag_layer.load_flag(atlas, country, dst, a4, a5, a6, a7, a8);
}

/**
 * @brief Hook body of `BuildFlagCandidates`.
 * @param out Candidate vector.
 * @param tag Tag string.
 * @param cosmetic Cosmetic tag string.
 * @param group Ideology group.
 * @return The original's result.
 */
void* flag_candidates_hook(void* out, const void* tag, const void* cosmetic, void* group) noexcept {
    return flag_layer.candidates(out, tag, cosmetic, group);
}

/**
 * @brief Hook body of `ResolvePortrait`.
 * @param map Portrait map.
 * @param out Caller's entry buffer.
 * @param type Portrait type.
 * @param size Portrait size.
 * @param character Character.
 * @param fallback Caller's fallback name.
 * @return `out`.
 */
void* portrait_resolve_hook(void* map, void* out, std::int32_t type, std::int32_t size, core::uptr character,
                            const void* fallback) noexcept {
    return portrait_layer.resolve(map, out, type, size, character, fallback);
}

/**
 * @brief Hook body of `CFaction::GetName`.
 * @param faction Faction object.
 * @param out Result string.
 * @return `out`.
 */
void* faction_name_hook(core::uptr faction, void* out) noexcept {
    return name_layer.faction_name(faction, out);
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
    // Refuse a copy loaded under another name (a second set of hooks on the same functions).
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&open_log), &self) == 0 ||
        GetModuleHandleW(core::core_dll_name) != self) {
        return InitCode::co_load_refused;
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
    const char* const failed_flag_pin = flag::pins::first_failed_pin(text);
    const bool flag_ok = layer_ok && failed_flag_pin == nullptr;
    const char* const failed_portrait_pin = portrait::pins::first_failed_pin(text);
    const bool portrait_ok = layer_ok && failed_portrait_pin == nullptr;
    const core::HookSpec all_hooks[] = {
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
        {"faction.get_name", core::rva::faction_get_name, reinterpret_cast<void*>(&faction_name_hook),
         reinterpret_cast<void**>(&name_layer.originals().faction_name)},
        {"flag.atlas_update", core::rva::flag_atlas_update, reinterpret_cast<void*>(&flag_atlas_hook),
         reinterpret_cast<void**>(&flag_layer.originals().atlas_update)},
        {"flag.load_country_flag", core::rva::load_country_flag, reinterpret_cast<void*>(&load_flag_hook),
         reinterpret_cast<void**>(&flag_layer.originals().load_flag)},
        {"flag.build_candidates", core::rva::build_flag_candidates, reinterpret_cast<void*>(&flag_candidates_hook),
         reinterpret_cast<void**>(&flag_layer.originals().candidates)},
        {"portrait.resolve", core::rva::portrait_resolve, reinterpret_cast<void*>(&portrait_resolve_hook),
         reinterpret_cast<void**>(&portrait_layer.originals().resolve)},
    };
    static_assert(std::size(all_hooks) == max_hooks);
    // Localisation hooks always; the name layers when their pins match; the flag and portrait
    // layers only on top of the name layers, each with its own pins.
    core::HookSpec hooks[max_hooks]{};
    std::size_t hook_count = 0;
    for (std::size_t index = 0; index < max_hooks; ++index) {
        const bool name_hook = index >= localisation_hooks && index < localisation_hooks + name_layer_hooks;
        const bool flag_hook = index >= localisation_hooks + name_layer_hooks &&
                               index < localisation_hooks + name_layer_hooks + flag_layer_hooks;
        const bool portrait_hook = index == max_hooks - 1;
        if ((name_hook && !layer_ok) || (flag_hook && !flag_ok) || (portrait_hook && !portrait_ok)) {
            continue;
        }
        hooks[hook_count++] = all_hooks[index];
    }
    char line[line_bytes]{};
    // Trampolines must outlive any unload attempt: pin the module before the first hook.
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&open_log), &pinned) == 0) {
        write_line(core::logging::Channel::error, "CAPIE_CORE init failed code=14 (module pin)");
        return InitCode::module_pin_failed;
    }
    if (const auto installed = core::install_all_at_once(base, hooks, hook_count); installed != InitCode::ok) {
        active.store(false);
        static_cast<void>(std::snprintf(line, sizeof(line), "CAPIE_CORE init failed code=%u",
                                        static_cast<unsigned>(core::to_exit_code(installed))));
        write_line(core::logging::Channel::error, line);
        return installed;
    }
    if (layer_ok) {
        name_layer.activate(reinterpret_cast<core::uptr>(base), &write_line,
                            reinterpret_cast<layer::StringCopyFn>(base + core::rva::string_copy));
    }
    if (flag_ok) {
        flag_layer.activate(reinterpret_cast<core::uptr>(base), &write_line);
    }
    if (portrait_ok) {
        portrait_layer.originals().valid = reinterpret_cast<portrait::ValidFn>(base + core::rva::portrait_valid);
        portrait_layer.originals().copy = reinterpret_cast<portrait::CopyFn>(base + core::rva::string_copy);
        portrait_layer.activate(reinterpret_cast<core::uptr>(base), &write_line);
    }
    active.store(true);
    static_cast<void>(std::snprintf(
        line, sizeof(line),
        "CAPIE_CORE %s initialized: %zu hooks, name layer %s %s, flag layer %s %s, portrait layer %s %s, flavor=%s",
        core::capie_version, hook_count, name_layer.enabled() ? "on" : "off", layer_ok ? "" : failed_pin,
        flag_layer.enabled() ? "on" : "off", failed_flag_pin != nullptr ? failed_flag_pin : "",
        portrait_layer.enabled() ? "on" : "off", failed_portrait_pin != nullptr ? failed_portrait_pin : "",
        core::logging::debug_diagnostics ? "debug" : "release"));
    write_line(core::logging::Channel::lifecycle, line);
    OutputDebugStringA(initialized_message);
    return InitCode::ok;
}

}  // namespace
}  // namespace capie::country

/**
 * @brief Read-only capability query for the editor bridge.
 * @return `capabilities_of` the active layers while initialized (v1 without the name layers, v2,
 *         v3 or a v4 value), otherwise 0. Loading the DLL alone is not enough.
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
extern "C" __declspec(dllexport) DWORD WINAPI RNCountryCapabilities() noexcept {
    if (!capie::country::active.load(std::memory_order_acquire)) {
        return 0;
    }
    return capie::country::capabilities_of(capie::country::name_layer.enabled(), capie::country::flag_layer.enabled(),
                                           capie::country::portrait_layer.enabled());
}

/**
 * @brief Initializes the country hooks (remote thread from the injector, outside DllMain).
 * @param unused Ignored.
 * @return 0 on success; see `capie::core::InitCode`.
 */
// NOLINTNEXTLINE(readability-identifier-naming): exported ABI name
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void* unused) {
    static_cast<void>(unused);
    using capie::core::InitCode;
    using capie::core::to_exit_code;
    // 0 idle, 1 running, 2 done. Two concurrent calls never initialize at the same time; a failed
    // attempt returns to idle so a later call may retry (the documented behaviour of this DLL).
    static std::atomic<unsigned> state{0};
    unsigned expected = 0;
    if (!state.compare_exchange_strong(expected, 1)) {
        return expected == 2 ? to_exit_code(InitCode::ok) : to_exit_code(InitCode::retry_refused);
    }
    InitCode result = InitCode::unexpected_exception;
    try {
        result = capie::country::initialize();
    } catch (...) {
        result = InitCode::unexpected_exception;
    }
    state.store(result == InitCode::ok ? 2U : 0U);
    return to_exit_code(result);
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
