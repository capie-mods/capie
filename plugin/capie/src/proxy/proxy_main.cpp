/**
 * @file proxy_main.cpp
 * @brief System32 version.dll forwarding and asynchronous opt-in startup.
 *
 * DllMain creates a worker and never waits. Windows serializes DLL notifications,
 * so its entry runs after process attach releases the loader lock. All filesystem,
 * hash, library and hook work belongs to that worker. Scheduling is asynchronous:
 * this does not promise hooks are installed before the game's first localization.
 * This startup-import proxy is kept in memory until process exit; hot unloading is
 * unsupported. There are no process opens, remote threads or injection helpers.
 */
#include "loader.hpp"
#include "proxy_manifest.generated.hpp"

#include <array>

namespace {

INIT_ONCE forwarding_once = INIT_ONCE_STATIC_INIT;
std::array<FARPROC, 17> system_exports{};
HMODULE own_module = nullptr;

constexpr std::array export_names = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW", "VerLanguageNameA",
    "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};

BOOL CALLBACK resolve_system(PINIT_ONCE, PVOID, PVOID*) noexcept {
    std::array<wchar_t, capie::launcher::long_path_chars> directory{};
    const UINT count = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (count == 0 || count + 13 >= directory.size()) {
        return TRUE;
    }
    // An absolute system path prevents recursion through our own version.dll,
    // current-directory DLL searches and dependency search outside System32.
    constexpr wchar_t suffix[] = L"\\version.dll";
    for (std::size_t index = 0; index < std::size(suffix); ++index) {
        directory[count + index] = suffix[index];
    }
    const auto real = LoadLibraryExW(directory.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (real != nullptr && real != own_module) {
        for (std::size_t index = 0; index < export_names.size(); ++index) {
            system_exports[index] = GetProcAddress(real, export_names[index]);
        }
    }
    return TRUE;
}

DWORD WINAPI unavailable_export() noexcept {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

DWORD WINAPI startup(void*) noexcept {
    try {
        HMODULE pinned = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&own_module), &pinned) == 0) {
            return 0;
        }
        std::array<wchar_t, capie::launcher::long_path_chars> executable{};
        const DWORD count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (count == 0 || count >= executable.size()) {
            return 0;
        }
        const std::wstring path(executable.data(), count);
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos ||
            _wcsicmp(path.c_str() + slash + 1, capie::core::game_exe_name) != 0) {
            return 0;
        }
        const auto app_data = capie::launcher::app_data_folder(false);
        const bool flag = capie::proxy::has_activation_flag(GetCommandLineW());
        if (!flag && !capie::proxy::config_enabled(app_data)) {
            return 0;
        }
        capie::proxy::write_log(app_data, flag ? "activation=flag; verifying game and bundle"
                                               : "activation=config; verifying game and bundle");
        const capie::proxy::Policy policy{path, app_data + L"\\current", capie::core::game_exe_sha256,
                                           {capie::proxy::manifest::dll_hashes[0],
                                            capie::proxy::manifest::dll_hashes[1]},
                                           app_data + L"\\" + capie::proxy::init_sentinel_file};
        const auto outcome = capie::proxy::load_payload(policy);
        capie::proxy::write_log(app_data, std::string(capie::proxy::result_name(outcome.result)) +
                                           "; code=" + std::to_string(outcome.init_code));
    } catch (...) {
        OutputDebugStringA("CAPIE proxy: startup refused after an internal error\n");
    }
    return 0;
}

} // namespace

// Called by x64 MASM stubs, which preserve every register and stack argument.
extern "C" FARPROC WINAPI CapieResolveExport(DWORD index) noexcept {
    const DWORD previous_error = GetLastError();
    static_cast<void>(InitOnceExecuteOnce(&forwarding_once, resolve_system, nullptr, nullptr));
    const auto address = index < system_exports.size() ? system_exports[index] : nullptr;
    if (address != nullptr) {
        SetLastError(previous_error);
        return address;
    }
    return reinterpret_cast<FARPROC>(&unavailable_export);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        own_module = instance;
        // No DisableThreadLibraryCalls: /MT needs CRT thread notifications.
        // Do not wait or perform any initialization while holding the loader lock.
        const HANDLE worker = CreateThread(nullptr, 0, startup, nullptr, 0, nullptr);
        if (worker != nullptr) {
            CloseHandle(worker);
        }
    }
    return TRUE; // Optional CAPIE work must never refuse the game's DLL import.
}
