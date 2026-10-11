// HOI4 1.19.3.0 only. Reversible in-memory localization hooks; no game-file writes.
#include "image_guard.hpp"
#include "protocol.hpp"
#include "country_capability.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
using LookupN = const char* (*)(const char*, int);
using LookupZ = const char* (*)(const char*);
using Exists = bool (*)(const char*);
LookupN original_n = nullptr;
LookupZ original_z = nullptr;
Exists original_exists = nullptr;
std::atomic<bool> active = false;
std::mutex cache_mutex;
// Interned strings never move or expire while hooks are installed.
std::unordered_map<std::string, std::unique_ptr<const std::string>> cache;

const char* override_value(std::string_view key) {
    if (!active.load()) return nullptr;
    if (key == "RN_PLUGIN_STATUS") return "DLL: ACTIVE (RN1)";
    auto decoded = rn::decode_country_key(key);
    if (!decoded) return nullptr;
    const auto& value = decoded->adjective_key ? decoded->adjective : decoded->name;
    const std::lock_guard lock(cache_mutex);
    auto found = cache.find(value);
    if (found == cache.end()) found = cache.emplace(value, std::make_unique<const std::string>(value)).first;
    return found->second->c_str();
}

const char* lookup_n(const char* key, int length) noexcept {
    try {
        if (key && length >= 0 && length <= 360) {
            if (const auto result = override_value(std::string_view(key, length))) return result;
        }
    } catch (...) { /* fallback to the original game path on allocation failure */ }
    return original_n(key, length);
}
const char* lookup_z(const char* key) noexcept {
    try {
        if (key && (std::strncmp(key, "RN1|", 4) == 0 || std::strcmp(key, "RN_PLUGIN_STATUS") == 0)) {
            const auto n = strnlen_s(key, 361);
            if (n <= 360) if (const auto result = override_value(std::string_view(key, n))) return result;
        }
    } catch (...) {}
    return original_z(key);
}
bool exists(const char* key) noexcept {
    try {
        if (active.load() && key && std::strncmp(key, "RN1|", 4) == 0) {
            const auto n = strnlen_s(key, 361);
            if (n <= 360 && rn::decode_country_key(std::string_view(key, n))) return true;
        }
    } catch (...) {}
    return original_exists(key);
}

struct HookSpec { size_t rva; std::vector<int> pattern; void* replacement; void** original; };

bool unique_signature(const unsigned char* base, const IMAGE_SECTION_HEADER* text,
                      const HookSpec& hook) {
    const auto* start = base + text->VirtualAddress;
    const size_t size = text->Misc.VirtualSize;
    size_t matches = 0;
    for (size_t off = 0; off + hook.pattern.size() <= size; ++off) {
        bool match = true;
        for (size_t i = 0; i < hook.pattern.size(); ++i) {
            if (hook.pattern[i] >= 0 && start[off+i] != hook.pattern[i]) { match = false; break; }
        }
        if (match) {
            if (static_cast<size_t>(text->VirtualAddress) + off != hook.rva || ++matches > 1) return false;
        }
    }
    return matches == 1;
}
}

// Read-only capability query for a future inline country editor. Loading the DLL
// alone is insufficient; failed/unstarted initialization always reports zero.
extern "C" __declspec(dllexport) DWORD WINAPI RNCountryCapabilities() noexcept {
    return active.load(std::memory_order_acquire) ? rn::country_capabilities_v1 : 0;
}

// The injector invokes this explicitly outside DllMain. Return values are in README.
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    if (active.load()) return 0;
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768) || rn::sha256_file(path) != rn::exe_sha256) return 10;
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return 11;
    const IMAGE_SECTION_HEADER* text = nullptr;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (std::memcmp(section[i].Name, ".text\0\0\0", 8) == 0) text = section + i;
    if (!text) return 12;
    const HookSpec hooks[] = {
        {0x239ee20, {0x48,0x8b,0x05,-1,-1,-1,-1,0x49,0xb8,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb},
         reinterpret_cast<void*>(&lookup_n), reinterpret_cast<void**>(&original_n)},
        {0x239ed40, {0x48,0x8b,0x05,-1,-1,-1,-1,0x48,0xba,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb,0x4c,0x8b,0x00,0x0f,0xb6,0x01,0x84,0xc0,0x74,0x2a,0x49,0xb9,0xb3,0x01,0x00,0x00,0x00,0x01,0x00,0x00},
         reinterpret_cast<void*>(&lookup_z), reinterpret_cast<void**>(&original_z)},
        {0x239d0f0, {0x48,0x83,0xec,0x28,0x48,0x8b,0x05,-1,-1,-1,-1,0x48,0xba,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb},
         reinterpret_cast<void*>(&exists), reinterpret_cast<void**>(&original_exists)}
    };
    for (const auto& hook : hooks) if (!unique_signature(base, text, hook)) return 20;
    if (MH_Initialize() != MH_OK) return 30;
    for (const auto& hook : hooks) {
        if (MH_CreateHook(base + hook.rva, hook.replacement, hook.original) != MH_OK) {
            MH_Uninitialize(); return 31;
        }
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        // Activation can fail after enabling an earlier hook. A callback could
        // already be in flight, so retain its trampoline until process exit.
        // Residual hooks only forward to vanilla while active is false.
        active.store(false);
        MH_DisableHook(MH_ALL_HOOKS);
        return 32;
    }
    active.store(true);
    OutputDebugStringA("Rename Anything: RN1 country localization hooks initialized (pinned 1.19.3.0)\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    // Keep thread notifications: this DLL links the CRT statically with /MT.
    // Game exit releases the module. Unloading hooks in a live process is unsupported.
    return TRUE;
}
