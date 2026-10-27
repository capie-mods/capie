/**
 * @file capie_injector.cpp
 * @brief `capie_injector.exe PID ABSOLUTE_DLL_PATH`: loads one CAPIE DLL into an already running, pinned hoi4.exe.
 *
 * Accepted DLL file names: `capie_core.dll` and `capie_editor.dll` (one injector for both;
 * the accepted binaries used one injector build per DLL name). The launcher runs it twice,
 * core first, then editor.
 *
 * Steps: refuse any other file name; open the process with the minimum rights; refuse
 * unless its image is `hoi4.exe` with the pinned SHA-256; refuse if a module with the same
 * file name is already loaded; resolve `LoadLibraryW` in the module that really owns it
 * (KernelBase on current Windows); write the path, run `LoadLibraryW` on a remote thread,
 * wait at most 30 s; find the loaded base, resolve `RNInitialize` locally (mapped without
 * running code) and call it remotely; report its code.
 *
 * It never waits indefinitely, never retries and never unloads. A timed-out remote call
 * leaves its argument allocation in place (the thread could still read it) and asks for a
 * game restart. Exit codes: 0 ok, 2 usage, 3 open process, 10 wrong image, 11 already
 * loaded, 12 remote LoadLibrary setup, 13 load/export resolution, 14 RNInitialize refused,
 * 15 timeout.
 */
#include "../core/game_build.hpp"
#include "../core/sha256.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <iostream>

namespace {

/// @brief DLL file names this injector accepts.
constexpr const wchar_t* accepted_dll_names[] = {capie::core::core_dll_name, capie::core::editor_dll_name};
/// @brief Remote call timeout.
constexpr DWORD remote_wait_ms = 30000;
/// @brief Path buffer length.
constexpr DWORD max_path_chars = 32768;
/// @brief Exit code used when `GetExitCodeThread` fails.
constexpr DWORD unknown_exit_code = 99;

/// @brief Exit code: success.
constexpr int exit_ok = 0;
/// @brief Exit code: bad command line or DLL name.
constexpr int exit_usage = 2;
/// @brief Exit code: target process cannot be opened.
constexpr int exit_open_process = 3;
/// @brief Exit code: target is not the pinned hoi4.exe.
constexpr int exit_wrong_image = 10;
/// @brief Exit code: a module with this file name is already loaded.
constexpr int exit_already_loaded = 11;
/// @brief Exit code: remote LoadLibraryW could not be prepared.
constexpr int exit_remote_setup = 12;
/// @brief Exit code: loaded module or RNInitialize not found.
constexpr int exit_load_resolution = 13;
/// @brief Exit code: RNInitialize refused or failed.
constexpr int exit_initialize_refused = 14;
/// @brief Exit code: a remote call timed out (state uncertain).
constexpr int exit_timeout = 15;

/**
 * @brief Base address of a module in another process.
 * @param pid Process id.
 * @param name Module file name (case-insensitive).
 * @return Base address, or 0 when not loaded or not enumerable.
 */
uintptr_t module_address(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uintptr_t answer = 0;
    for (bool more = Module32FirstW(snapshot, &entry) != 0; more; more = Module32NextW(snapshot, &entry) != 0) {
        if (_wcsicmp(entry.szModule, name) == 0) {
            answer = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            break;
        }
    }
    CloseHandle(snapshot);
    return answer;
}

/// @brief Outcome of a remote call.
enum class CallResult { completed, not_started, uncertain };

/**
 * @brief Runs `function(argument)` on a remote thread and waits up to 30 s.
 * @param process Target process.
 * @param function Remote function address.
 * @param argument Remote argument.
 * @param result Receives the thread exit code when completed.
 * @return Outcome; `uncertain` means the thread may still be running.
 */
CallResult remote_call(HANDLE process, uintptr_t function, void* argument, DWORD& result) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function),
                                       argument, 0, nullptr);
    if (thread == nullptr) {
        return CallResult::not_started;
    }
    // Do not free a remote argument while the remote thread could still use it.
    const DWORD wait = WaitForSingleObject(thread, remote_wait_ms);
    const bool completed = wait == WAIT_OBJECT_0;
    if (completed && GetExitCodeThread(thread, &result) == 0) {
        result = unknown_exit_code;
    }
    CloseHandle(thread);
    return completed ? CallResult::completed : CallResult::uncertain;
}

/**
 * @brief Whether a file name is one of the accepted CAPIE DLLs.
 * @param name File name.
 * @return `true` for an exact (case-sensitive, as before) match.
 */
bool accepted_name(const std::filesystem::path& name) {
    return std::ranges::any_of(accepted_dll_names, [&name](const wchar_t* accepted) { return name == accepted; });
}

}  // namespace

/**
 * @brief Entry point.
 * @param argc Argument count (must be 3).
 * @param argv PID and absolute DLL path.
 * @return Exit code (see file comment).
 */
int wmain(int argc, wchar_t** argv) {  // NOLINT(misc-use-internal-linkage): process entry point
    if (argc != 3) {
        std::wcerr << L"Usage: capie_injector.exe PID ABSOLUTE_DLL_PATH\n";
        return exit_usage;
    }
    wchar_t* end = nullptr;
    const auto pid_long = std::wcstoul(argv[1], &end, 10);
    if (pid_long == 0 || *end != L'\0') {
        std::wcerr << L"Invalid PID\n";
        return exit_usage;
    }
    const auto pid = static_cast<DWORD>(pid_long);
    const auto dll = std::filesystem::absolute(argv[2]);
    if (!std::filesystem::is_regular_file(dll) || !accepted_name(dll.filename())) {
        std::wcerr << L"Expected the built " << capie::core::core_dll_name << L" or " << capie::core::editor_dll_name
                   << L"\n";
        return exit_usage;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (process == nullptr) {
        std::wcerr << L"Cannot open process: " << GetLastError() << L"\n";
        return exit_open_process;
    }
    wchar_t image[max_path_chars]{};
    DWORD count = max_path_chars;
    if (QueryFullProcessImageNameW(process, 0, image, &count) == 0 ||
        _wcsicmp(std::filesystem::path(image).filename().c_str(), capie::core::game_exe_name) != 0 ||
        capie::core::sha256_file(image) != capie::core::game_exe_sha256) {
        std::wcerr << L"Refused: process image does not match pinned hoi4.exe 1.19.4.0 SHA-256\n";
        CloseHandle(process);
        return exit_wrong_image;
    }
    if (module_address(pid, dll.filename().c_str()) != 0) {
        std::wcerr << L"DLL already loaded; restart the game to rebuild/reload it\n";
        CloseHandle(process);
        return exit_already_loaded;
    }
    // Resolve the RVA of LoadLibraryW against the actual module that owns it (on current
    // Windows this can be KernelBase rather than kernel32).
    const auto load_library = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (load_library == nullptr ||
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(load_library), &owner) == 0) {
        std::wcerr << L"Cannot resolve LoadLibraryW owner\n";
        CloseHandle(process);
        return exit_remote_setup;
    }
    wchar_t owner_path[max_path_chars]{};
    GetModuleFileNameW(owner, owner_path, max_path_chars);
    const uintptr_t remote_owner = module_address(pid, std::filesystem::path(owner_path).filename().c_str());
    const uintptr_t remote_load =
        remote_owner + reinterpret_cast<uintptr_t>(load_library) - reinterpret_cast<uintptr_t>(owner);
    const auto dll_text = dll.wstring();
    const size_t bytes = (dll_text.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD loaded = 0;
    SIZE_T written = 0;
    if (remote_owner == 0 || remote_path == nullptr ||
        WriteProcessMemory(process, remote_path, dll_text.c_str(), bytes, &written) == 0 || written != bytes) {
        std::wcerr << L"LoadLibrary remote call failed: " << GetLastError() << L"\n";
        if (remote_path != nullptr) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        }
        CloseHandle(process);
        return exit_remote_setup;
    }
    const auto load_status = remote_call(process, remote_load, remote_path, loaded);
    if (load_status != CallResult::completed) {
        // A timed-out thread might still dereference remote_path. Keep that one allocation alive
        // until game exit; never unload or try a second injection.
        if (load_status == CallResult::not_started) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        }
        std::wcerr << L"Remote load did not complete within 30 seconds. State uncertain; restart game before retry.\n";
        CloseHandle(process);
        return exit_timeout;
    }
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    // GetExitCodeThread is only 32-bit; enumerate modules for the full x64 base.
    const uintptr_t remote_module = module_address(pid, dll.filename().c_str());
    HMODULE local_module = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    const auto init = local_module != nullptr ? GetProcAddress(local_module, "RNInitialize") : nullptr;
    if (remote_module == 0 || init == nullptr) {
        std::wcerr << L"DLL load/export resolution failed\n";
        if (local_module != nullptr) {
            FreeLibrary(local_module);
        }
        CloseHandle(process);
        return exit_load_resolution;
    }
    const uintptr_t remote_init =
        remote_module + reinterpret_cast<uintptr_t>(init) - reinterpret_cast<uintptr_t>(local_module);
    DWORD status = unknown_exit_code;
    const auto initialized = remote_call(process, remote_init, nullptr, status);
    FreeLibrary(local_module);
    CloseHandle(process);
    if (initialized != CallResult::completed || status != 0) {
        std::wcerr << L"RNInitialize refused/failed; code=" << status << L". Restart game before retry.\n";
        return exit_initialize_refused;
    }
    std::wcout << dll.filename().wstring() << L" initialized in PID " << pid << L"; game files unchanged.\n";
    return exit_ok;
}
