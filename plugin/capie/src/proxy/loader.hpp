/**
 * @file loader.hpp
 * @brief Opt-in, local-process CAPIE loading, outside the Windows loader lock.
 *
 * Trust is compiled into the proxy, never read from the mutable installation manifest.
 * File pins and resolved paths use the launcher's existing verification implementation.
 * Successfully mapped modules and their file pins live until process exit: RNInitialize
 * may retain hook trampolines even when initialization reports failure.
 *
 * Crash-loop breaker (crash_guard.hpp): `proxy-init.pending` is written right before the
 * first RNInitialize and deleted once both initializers returned. A sentinel still present
 * at the next start means the previous game ended inside an initializer; that start loads
 * no CAPIE (only the Windows forwards stay), logs it and deletes the sentinel, so the start
 * after it tries again.
 */
#pragma once
#include "../launcher/crash_guard.hpp"
#include "../launcher/file_pin.hpp"
#include "../launcher/game_location.hpp"

#include <shellapi.h>

#include <array>
#include <memory>
#include <string_view>

namespace capie::proxy {

inline constexpr char activation_marker[] = "CAPIE_PROXY_ENABLED_V1\n";
inline constexpr wchar_t activation_file[] = L"proxy.enabled";
inline constexpr wchar_t log_file[] = L"proxy.log";
/// @brief Crash-loop sentinel of the proxy in the app data folder.
inline constexpr wchar_t init_sentinel_file[] = L"proxy-init.pending";

/// @brief Case-sensitive, whole Windows argument only; argv[0] never activates CAPIE.
inline bool has_activation_flag(const wchar_t* command_line) noexcept {
    int count = 0;
    auto** args = CommandLineToArgvW(command_line, &count);
    if (args == nullptr) {
        return false;
    }
    bool found = false;
    for (int index = 1; index < count; ++index) {
        found = found || std::wstring_view(args[index]) == L"-capie";
    }
    LocalFree(args);
    return found;
}

/// @brief A missing, malformed, oversized or unreadable marker means disabled.
inline bool config_enabled(const std::wstring& app_data) noexcept {
    try {
        const launcher::Handle file(CreateFileW((app_data + L"\\" + activation_file).c_str(), GENERIC_READ,
                                                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                                nullptr));
        LARGE_INTEGER size{};
        std::array<char, 64> bytes{};
        DWORD actual = 0;
        if (!file || GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart <= 0 ||
            size.QuadPart >= static_cast<LONGLONG>(bytes.size()) ||
            ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr) == 0 ||
            actual != static_cast<DWORD>(size.QuadPart)) {
            return false;
        }
        const std::string_view text(bytes.data(), actual);
        return text == activation_marker || text == "CAPIE_PROXY_ENABLED_V1\r\n";
    } catch (...) {
        return false;
    }
}

/// @brief Bounded local diagnostics; an unavailable log never affects the game.
inline void write_log(const std::wstring& app_data, std::string_view message) noexcept {
    try {
        static_cast<void>(CreateDirectoryW(app_data.c_str(), nullptr));
        const auto path = app_data + L"\\" + log_file;
        const launcher::Handle file(CreateFileW(path.c_str(), FILE_APPEND_DATA | GENERIC_READ,
                                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                                FILE_ATTRIBUTE_NORMAL, nullptr));
        LARGE_INTEGER size{};
        if (!file || GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart > 1024 * 1024) {
            return;
        }
        const auto line = "pid=" + std::to_string(GetCurrentProcessId()) + " " + std::string(message) + "\r\n";
        DWORD written = 0;
        static_cast<void>(WriteFile(file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr));
    } catch (...) {
    }
}

struct Policy {
    std::wstring game_path;
    std::wstring current_dir;
    std::string game_hash;
    std::array<std::string, 2> dll_hashes;
    std::wstring sentinel;  ///< crash-loop sentinel path; empty disables the breaker
};

struct VerifiedPayload {
    launcher::PinnedFile game;
    std::array<launcher::PinnedFile, 2> dlls;
};

/// @brief Verify the entire payload before mapping either DLL.
inline VerifiedPayload verify_payload(const Policy& policy) {
    VerifiedPayload verified;
    verified.game = launcher::verify_file(policy.game_path, policy.game_hash, false,
                                           launcher::ExitCode::unsupported_game);
    constexpr std::array names{core::core_dll_name, core::editor_dll_name};
    for (std::size_t index = 0; index < names.size(); ++index) {
        verified.dlls[index] = launcher::verify_file(policy.current_dir + L"\\runtime\\" + names[index],
                                                    policy.dll_hashes[index], true,
                                                    launcher::ExitCode::invalid_bundle);
    }
    return verified;
}

enum class LoadResult { loaded, unsupported_game, invalid_bundle, co_load_refused, load_failed,
                        initializer_missing, core_init_failed, editor_init_failed, crash_guard_skipped,
                        unexpected };

struct Outcome {
    LoadResult result = LoadResult::unexpected;
    DWORD init_code = 0;
};

/// @brief A retained session owns pins and DLL references until process termination.
struct Session {
    VerifiedPayload files;
    std::array<HMODULE, 2> modules{};
};

/// @brief Called only on the worker, never from DllMain or a forwarded API.
inline Outcome load_payload(const Policy& policy) noexcept {
    try {
        if (launcher::consume_sentinel(policy.sentinel)) {
            return {LoadResult::crash_guard_skipped};  // the previous start ended inside RNInitialize
        }
        auto session = std::make_unique<Session>();
        session->files = verify_payload(policy);
        constexpr std::array names{core::core_dll_name, core::editor_dll_name};
        for (const auto* name : names) {
            if (GetModuleHandleW(name) != nullptr) {
                return {LoadResult::co_load_refused};
            }
        }
        // From this point a module can have run DllMain. Retain its reference and pins
        // even on refusal: hook libraries explicitly do not support hot unloading.
        auto* retained = session.release();
        using Initialize = DWORD(WINAPI*)(void*);
        std::array<Initialize, 2> initializers{};
        for (std::size_t index = 0; index < retained->modules.size(); ++index) {
            const auto& file = retained->files.dlls[index];
            if (!retained->files.game.still_same_path() || !file.still_same_path()) {
                return {LoadResult::invalid_bundle};
            }
            auto module = LoadLibraryExW(file.path.c_str(), nullptr,
                                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            retained->modules[index] = module;
            if (module == nullptr) {
                return {LoadResult::load_failed, GetLastError()};
            }
            std::array<wchar_t, launcher::long_path_chars> mapped_path{};
            const DWORD length = GetModuleFileNameW(module, mapped_path.data(),
                                                     static_cast<DWORD>(mapped_path.size()));
            if (length == 0 || length >= mapped_path.size()) {
                return {LoadResult::co_load_refused};
            }
            auto mapped = launcher::PinnedFile::open({mapped_path.data(), length});
            if (mapped.identity != file.identity || mapped.path != file.path) {
                return {LoadResult::co_load_refused};
            }
            initializers[index] = reinterpret_cast<Initialize>(GetProcAddress(module, "RNInitialize"));
            if (initializers[index] == nullptr) {
                return {LoadResult::initializer_missing};
            }
        }
        // Both modules, paths and ABI exports are checked before enabling any hooks.
        // No game-readiness heuristic: these are ordinary in-process initializers.
        // A crash inside them leaves the sentinel behind for the next start.
        static_cast<void>(launcher::arm_sentinel(policy.sentinel, "proxy RNInitialize in hoi4.exe"));
        const DWORD core_code = initializers[0](nullptr);
        if (core_code != 0) {
            launcher::disarm_sentinel(policy.sentinel);
            return {LoadResult::core_init_failed, core_code};
        }
        const DWORD editor_code = initializers[1](nullptr);
        launcher::disarm_sentinel(policy.sentinel);
        if (editor_code != 0) {
            return {LoadResult::editor_init_failed, editor_code};
        }
        return {LoadResult::loaded};
    } catch (const launcher::LauncherError& error) {
        return {error.code() == launcher::ExitCode::unsupported_game ? LoadResult::unsupported_game
                                                                   : LoadResult::invalid_bundle};
    } catch (...) {
        return {LoadResult::unexpected};
    }
}

inline const char* result_name(LoadResult result) noexcept {
    switch (result) {
    case LoadResult::loaded: return "loaded core=0 editor=0";
    case LoadResult::unsupported_game: return "unsupported_game; CAPIE not loaded";
    case LoadResult::invalid_bundle: return "invalid_bundle; CAPIE not initialized";
    case LoadResult::co_load_refused: return "co_load_refused; CAPIE not initialized";
    case LoadResult::load_failed: return "load_failed; CAPIE not initialized";
    case LoadResult::initializer_missing: return "initializer_missing; CAPIE not initialized";
    case LoadResult::core_init_failed: return "core_init_failed; editor not initialized";
    case LoadResult::editor_init_failed: return "editor_init_failed; core remains active; restart required";
    case LoadResult::crash_guard_skipped:
        return "crash_guard_skipped; the previous start ended during CAPIE initialization; CAPIE skipped once";
    default: return "unexpected; CAPIE initialization refused";
    }
}

} // namespace capie::proxy
