// Load this project's DLL into an already-running, exact pinned hoi4.exe process.
#include "image_guard.hpp"
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {
uintptr_t module_address(DWORD pid, const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    uintptr_t answer = 0;
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szModule, name) == 0) { answer = reinterpret_cast<uintptr_t>(entry.modBaseAddr); break; }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return answer;
}

enum class CallResult { completed, not_started, uncertain };
CallResult remote_call(HANDLE process, uintptr_t function, void* argument, DWORD& result) {
    HANDLE thread = CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument, 0, nullptr);
    if (!thread) return CallResult::not_started;
    // Do not free a remote argument while the remote thread could still use it.
    const DWORD wait = WaitForSingleObject(thread, 30000);
    const bool completed = wait == WAIT_OBJECT_0;
    if (completed && !GetExitCodeThread(thread, &result)) result = 99;
    CloseHandle(thread);
    return completed ? CallResult::completed : CallResult::uncertain;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::wcerr << L"Usage: rename_anything_injector.exe PID ABSOLUTE_DLL_PATH\n"; return 2; }
    wchar_t* end = nullptr;
    const auto pid_long = wcstoul(argv[1], &end, 10);
    if (!pid_long || *end) { std::wcerr << L"Invalid PID\n"; return 2; }
    const DWORD pid = static_cast<DWORD>(pid_long);
    const auto dll = std::filesystem::absolute(argv[2]);
    if (!std::filesystem::is_regular_file(dll) || dll.filename() != L"rename_anything_country.dll") {
        std::wcerr << L"Expected the built rename_anything_country.dll\n"; return 2;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) { std::wcerr << L"Cannot open process: " << GetLastError() << L"\n"; return 3; }
    wchar_t image[32768]{}; DWORD count = 32768;
    if (!QueryFullProcessImageNameW(process, 0, image, &count) ||
        _wcsicmp(std::filesystem::path(image).filename().c_str(), L"hoi4.exe") ||
        rn::sha256_file(image) != rn::exe_sha256) {
        std::wcerr << L"Refused: process image does not match pinned hoi4.exe 1.19.3.0 SHA-256\n";
        CloseHandle(process); return 10;
    }
    if (module_address(pid, dll.filename().c_str())) {
        std::wcerr << L"DLL already loaded; restart the game to rebuild/reload it\n";
        CloseHandle(process); return 11;
    }
    // Resolve the RVA of LoadLibraryW against the actual module that owns it
    // (on current Windows this can be KernelBase, rather than kernel32).
    const auto load_library = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!load_library || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(load_library), &owner)) {
        std::wcerr << L"Cannot resolve LoadLibraryW owner\n"; CloseHandle(process); return 12;
    }
    wchar_t owner_path[32768]{};
    GetModuleFileNameW(owner, owner_path, 32768);
    const uintptr_t remote_owner = module_address(pid, std::filesystem::path(owner_path).filename().c_str());
    const uintptr_t remote_load = remote_owner + reinterpret_cast<uintptr_t>(load_library) - reinterpret_cast<uintptr_t>(owner);
    const auto dll_text = dll.wstring();
    const size_t bytes = (dll_text.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD loaded = 0;
    SIZE_T written = 0;
    if (!remote_owner || !remote_path || !WriteProcessMemory(process, remote_path, dll_text.c_str(), bytes, &written) ||
        written != bytes) {
        std::wcerr << L"LoadLibrary remote call failed: " << GetLastError() << L"\n";
        if (remote_path) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process); return 12;
    }
    const auto load_status = remote_call(process, remote_load, remote_path, loaded);
    if (load_status != CallResult::completed) {
        // A timed-out thread might still dereference remote_path. Keep that one
        // allocation alive until game exit; never unload or try a second injection.
        if (load_status == CallResult::not_started) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        std::wcerr << L"Remote load did not complete within 30 seconds. State uncertain; restart game before retry.\n";
        CloseHandle(process); return 15;
    }
    VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    // GetExitCodeThread is only 32-bit; enumerate modules for the full x64 base.
    const uintptr_t remote_module = module_address(pid, dll.filename().c_str());
    HMODULE local_module = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    const auto init = local_module ? GetProcAddress(local_module, "RNInitialize") : nullptr;
    if (!remote_module || !init) {
        std::wcerr << L"DLL load/export resolution failed\n";
        if (local_module) FreeLibrary(local_module);
        CloseHandle(process); return 13;
    }
    const uintptr_t remote_init = remote_module + reinterpret_cast<uintptr_t>(init) - reinterpret_cast<uintptr_t>(local_module);
    DWORD status = 99;
    const auto initialized = remote_call(process, remote_init, nullptr, status);
    FreeLibrary(local_module);
    CloseHandle(process);
    if (initialized != CallResult::completed || status != 0) {
        std::wcerr << L"RNInitialize refused/failed; code=" << status << L". Restart game before retry.\n";
        return 14;
    }
    std::wcout << L"Country hooks initialized in PID " << pid << L"; game files unchanged.\n";
    return 0;
}
