// INACTIVE caption refresh candidate DLL shell.
// Default build: verifies exact EXE SHA256, PE layout, all fingerprints, vtable slot
// pins and token DWORDs, writes one log line and returns 41 (inactive by design).
// No hook, no native call, no game object access. Hook arming exists only behind
// RN_CAPTION_REFRESH_ARM_HOOKS, which the build compiles as a /c syntax check and
// never links. Authority guard remains OPEN (authority_proven() == false), so even an
// armed build cannot reach B6E210 until a separately reviewed decoder exists.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../../src/image_guard.hpp"
#include "caption_refresh_core.hpp"
#ifdef RN_CAPTION_REFRESH_ARM_HOOKS
#include <MinHook.h>
#endif
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <limits>

namespace {
using namespace rn::caption;
HMODULE own_module = nullptr;
uptr image_base = 0;
HANDLE log_file = INVALID_HANDLE_VALUE;
std::atomic<unsigned long long> log_bytes{0};
constexpr unsigned long long log_cap = 1u << 20;
std::atomic<unsigned> init_state{0};
std::atomic<bool> active{false};
constexpr bool authority_proven_constant = false;  // OPEN: see README / report

bool readable(const void* source, std::size_t size) noexcept {
    if (size == 0) return true;
    auto cursor = reinterpret_cast<std::uintptr_t>(source);
    if (!cursor || size > (std::numeric_limits<std::uintptr_t>::max)() - cursor) return false;
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READ &&
            access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY)
            return false;
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - start) return false;
        const auto next = start + region.RegionSize;
        if (next <= cursor) return false;
        cursor = next < end ? next : end;
    }
    return true;
}
int memory_exception(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
                                                                                  : EXCEPTION_CONTINUE_SEARCH;
}
bool guarded_copy(void* target, const void* source, std::size_t size) noexcept {
    if (!readable(source, size)) return false;
    __try { if (size) std::memcpy(target, source, size); return true; }
    __except (memory_exception(GetExceptionCode())) { return false; }
}
void write_log(const char* text) noexcept {
    if (log_file == INVALID_HANDLE_VALUE) return;
    const auto size = std::strlen(text);
    if (log_bytes.fetch_add(size) + size > log_cap) return;  // bounded; loss != absence of event
    DWORD written = 0;
    WriteFile(log_file, text, static_cast<DWORD>(size), &written, nullptr);
}

struct ProcessMemory {
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        return guarded_copy(out, reinterpret_cast<const void*>(address), size);
    }
};

struct MsvcString { union { unsigned char small[16]; const char* pointer; } data; std::uint64_t size, capacity; };
static_assert(sizeof(MsvcString) == 0x20);
using FindWidget = void*(__fastcall*)(void*, const MsvcString*, bool, bool);
using RequestRefresh = void(__fastcall*)(void*, int);
FindWidget find_instant_fn = nullptr;
RequestRefresh request_refresh_fn = nullptr;

void* guarded_find(void* root, const char* name) noexcept {
    MsvcString text{};
    const auto size = std::strlen(name);
    text.size = size;
    if (size <= 15) { std::memcpy(text.data.small, name, size); text.capacity = 15; }
    else { text.data.pointer = name; text.capacity = size; }
    __try { return find_instant_fn ? find_instant_fn(root, &text, false, false) : nullptr; }
    __except (memory_exception(GetExceptionCode())) { return nullptr; }
}
bool guarded_request(void* gui, int index) noexcept {
    if (!request_refresh_fn || index != politics_view_index) return false;
    __try { request_refresh_fn(gui, index); return true; }
    __except (memory_exception(GetExceptionCode())) { return false; }
}

struct ProcessEnv {
    ProcessMemory memory{};
    const ProcessMemory& mem() const noexcept { return memory; }
    uptr image() const noexcept { return image_base; }
    bool carrier(uptr root, const char* name, char* out, std::size_t cap, std::size_t& size) noexcept {
        // Same proven instantTextBox layout as the render bridge: VT 2B468C8, text at +110.
        void* widget = guarded_find(reinterpret_cast<void*>(root), name);
        uptr vt = 0;
        if (!widget || !rd(memory, reinterpret_cast<uptr>(widget), 0, vt) || vt != image_base + rva::vt_instant_text) return false;
        MsvcString text{};
        if (!rd(memory, reinterpret_cast<uptr>(widget), 0x110, text) || text.size > cap || text.size > text.capacity ||
            text.capacity > 1048576)
            return false;
        const void* bytes = text.capacity <= 15 ? static_cast<const void*>(text.data.small) : static_cast<const void*>(text.data.pointer);
        size = static_cast<std::size_t>(text.size);
        return guarded_copy(out, bytes, size);
    }
    bool call_refresh(uptr gui, int index) noexcept { return guarded_request(reinterpret_cast<void*>(gui), index); }
    bool authority_proven() const noexcept { return authority_proven_constant; }
    void log(const char* line) noexcept { write_log(line); }
};
ProcessEnv process_env{};
Service<ProcessEnv> service(process_env);

bool unique_at(const unsigned char* base, const IMAGE_SECTION_HEADER* text, uptr target, const unsigned char* bytes,
               std::size_t size) noexcept {
    if (target < text->VirtualAddress || target - text->VirtualAddress + size > text->Misc.VirtualSize) return false;
    const auto* data = base + text->VirtualAddress;
    std::size_t count = 0;
    for (std::size_t i = 0; i + size <= text->Misc.VirtualSize; ++i) {
        if (data[i] == bytes[0] && std::memcmp(data + i, bytes, size) == 0) {
            if (i + text->VirtualAddress != target || ++count > 1) return false;
        }
    }
    return count == 1;
}

// Hook-target fingerprints copied byte-for-byte from the reviewed render bridge.
constexpr unsigned char tick_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x20,0x01,0x00,0x00,0x44,0x0f,0xb6,0xf2,0x48,0x8b,0xd9,0x48,0x8d,0x51,0x28,0x33,0xff,0x48,0x85,0xc9,0x48,0x0f,0x44,0xd7,0x41,0xb9,0x02,0x00,0x00,0x00};
constexpr unsigned char bind_bytes[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0xf9,0xc7,0x40,0x08,0x00,0x00,0x00,0x00,0x48,0x8d,0xb1,0xe8,0x00,0x00,0x00,0x4c,0x8b,0x2e,0x41,0x8b,0x45,0x2c};
constexpr unsigned char find_instant_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0xe0,0x11,0x00,0x00};

#ifdef RN_CAPTION_REFRESH_ARM_HOOKS
using Tick = void(__fastcall*)(void*, char);
using Bind = void(__fastcall*)(void*);
Tick original_tick = nullptr;
Bind original_bind = nullptr;
void __fastcall tick_observed(void* window, char force) {
    forward_then_observe(original_tick, [](void* w, char) { service.on_tick(reinterpret_cast<uptr>(w)); },
                         active.load(std::memory_order_acquire), window, force);
}
void __fastcall bind_observed(void* window) {
    forward_then_observe(original_bind, [](void* w) { service.on_bind(reinterpret_cast<uptr>(w)); },
                         active.load(std::memory_order_acquire), window);
}
#endif
}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    unsigned expected = 0;
    if (!init_state.compare_exchange_strong(expected, 1)) return active.load() ? 0 : 33;
    try {
        // Never co-load with any editor bridge variant hooking the same Tick/Bind targets.
        // Reverse load order is NOT protected (older DLLs do not know this basename): the
        // operator must never load any of them after this module either.
        if (GetModuleHandleW(L"rename_anything_editor_observer.dll") ||
            GetModuleHandleW(L"rename_anything_editor_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_render_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_debug_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_focus_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_key_logger.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_key_logger_v2.dll") ||
            GetModuleHandleW(L"rename_anything_inline_prefill.dll") ||
            GetModuleHandleW(L"rename_anything_inline_leader_bridge.dll"))
            return 40;
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768) || rn::sha256_file(executable) != exe_sha256) return 10;
        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 65536) return 11;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.NumberOfSections > 96)
            return 11;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (!readable(sections, nt->FileHeader.NumberOfSections * sizeof(*sections))) return 11;
        const IMAGE_SECTION_HEADER* text = nullptr;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (std::memcmp(sections[i].Name, ".text\0\0\0", 8) == 0) text = sections + i;
        if (!text || text->VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            text->Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - text->VirtualAddress ||
            !readable(base + text->VirtualAddress, text->Misc.VirtualSize))
            return 12;
        for (const auto& fp : fingerprints) {
            unsigned char bytes[128]{};
            const auto size = decode_hex(fp.hex, bytes, sizeof(bytes));
            if (!size || !unique_at(base, text, fp.rva, bytes, size)) return 20;
        }
        if (!unique_at(base, text, rva::window_tick, tick_bytes, sizeof(tick_bytes)) ||
            !unique_at(base, text, rva::window_bind, bind_bytes, sizeof(bind_bytes)) ||
            !unique_at(base, text, rva::find_instant, find_instant_bytes, sizeof(find_instant_bytes)))
            return 20;
        const auto image = reinterpret_cast<uptr>(base);
        ProcessMemory memory{};
        for (const auto& pin : slot_pins) {
            uptr slot = 0;
            if (!rd(memory, image + pin.vtable, pin.slot, slot) || slot != image + pin.target) return 21;
        }
        std::uint32_t politics = 0, context = 0;
        if (!rd(memory, image, rva::token_politics_tab, politics) || politics != token_politics_tab_value ||
            !rd(memory, image, rva::token_player_context, context) || context != token_player_context_value)
            return 21;
        wchar_t log_path[32768]{};
        if (!GetModuleFileNameW(own_module, log_path, 32768)) return 13;
        auto* slash = std::wcsrchr(log_path, L'\\');
        if (!slash || wcscpy_s(slash + 1, 32768 - static_cast<std::size_t>(slash + 1 - log_path), L"caption_refresh_candidate.log") != 0)
            return 13;
        log_file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_file == INVALID_HANDLE_VALUE) return 13;
        image_base = image;
#ifndef RN_CAPTION_REFRESH_ARM_HOOKS
        write_log("RN_CAPTION initialized: exact image + 28 unique fingerprints + 4 slot pins + 2 tokens; INACTIVE build, 0 hooks, no native call\r\n");
        return 41;
#else
        find_instant_fn = reinterpret_cast<FindWidget>(image + rva::find_instant);
        request_refresh_fn = reinterpret_cast<RequestRefresh>(image + rva::request_view_refresh);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&RNInitialize), &pinned))
            return 14;
        if (MH_Initialize() != MH_OK) return 30;
        auto* tick_target = reinterpret_cast<void*>(image + rva::window_tick);
        auto* bind_target = reinterpret_cast<void*>(image + rva::window_bind);
        if (MH_CreateHook(tick_target, reinterpret_cast<void*>(&tick_observed), reinterpret_cast<void**>(&original_tick)) != MH_OK ||
            MH_CreateHook(bind_target, reinterpret_cast<void*>(&bind_observed), reinterpret_cast<void**>(&original_bind)) != MH_OK) {
            MH_Uninitialize();
            return 31;
        }
        if (MH_EnableHook(bind_target) != MH_OK || MH_EnableHook(tick_target) != MH_OK) {
            MH_DisableHook(bind_target);
            MH_DisableHook(tick_target);
            write_log("RN_CAPTION hook enable failed; restart required\r\n");
            return 32;
        }
        active.store(true, std::memory_order_release);
        write_log("RN_CAPTION ARMED build (review-only): 2 hooks; authority OPEN => no native refresh call\r\n");
        return 0;
#endif
    } catch (...) {
        return 99;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) own_module = instance;
    return TRUE;
}
