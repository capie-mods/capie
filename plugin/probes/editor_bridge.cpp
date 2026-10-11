// Disposable native command-transport QA bridge, exact pinned executable only.
// Build separately; never load together with editor_observer's dispatch detour.
#include "../src/image_guard.hpp"
#include "../src/editor_input.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <type_traits>

namespace {
constexpr std::uintptr_t dispatch_rva = 0xB867D0;
constexpr std::uintptr_t find_edit_rva = 0x22BAD40;
constexpr std::uintptr_t ctor_rva = 0x1144840;
constexpr std::size_t max_codepoints = 128;
constexpr std::size_t max_original_records = 256;
constexpr std::size_t transport_records = 34;
constexpr std::int64_t fixed_point = 100000;
using Dispatch = void(__fastcall*)(void*, void*);

// Borrowed, read-only native string argument. No C++ string crosses the ABI.
struct MsvcString {
    union { unsigned char small[16]; const char* pointer; } data;
    std::uint64_t size;
    std::uint64_t capacity;
};
static_assert(sizeof(MsvcString) == 0x20);
static_assert(offsetof(MsvcString, size) == 0x10 && offsetof(MsvcString, capacity) == 0x18);
using FindEdit = void*(__fastcall*)(void*, const MsvcString*, bool, bool);

struct Record { MsvcString key; std::int64_t raw_value; };
struct TempVector { Record* data; std::int32_t capacity; std::int32_t count; void* allocator; };
static_assert(sizeof(Record) == 0x28 && offsetof(Record, raw_value) == 0x20);
static_assert(sizeof(TempVector) == 0x18 && offsetof(TempVector, count) == 0xC &&
              offsetof(TempVector, allocator) == 0x10);
static_assert(std::is_trivially_copyable_v<Record> && std::is_trivially_copyable_v<TempVector>);
using Constructor = void*(__fastcall*)(void*, const MsvcString*, const MsvcString*, const void*, const TempVector*);

struct InputSnapshot {
    std::uint32_t points[max_codepoints]{};
    std::uint64_t size{};
    std::uint64_t capacity{};
};
struct Origin {
    void* context{};
    const MsvcString* model_name{};
    const MsvcString* selector{};
    const void* scope{};
    const TempVector* source{};
    Record records[max_original_records + transport_records]{};
    TempVector view{};
    unsigned ctor_calls{};
    unsigned long id{};
};
thread_local Origin* origin = nullptr;
struct OriginFrame {
    Origin* previous;
    explicit OriginFrame(Origin* next) noexcept : previous(origin) { origin = next; }
    ~OriginFrame() { origin = previous; }
    OriginFrame(const OriginFrame&) = delete;
    OriginFrame& operator=(const OriginFrame&) = delete;
};

HMODULE own_module = nullptr;
std::uintptr_t image_base = 0;
Dispatch original_dispatch = nullptr;
Constructor original_ctor = nullptr;
FindEdit find_edit = nullptr;
HANDLE log_file = INVALID_HANDLE_VALUE;
std::atomic<bool> active{false};
std::atomic<unsigned> init_state{0};
std::atomic<unsigned long> snapshot_id{0};

bool readable(const void* source, std::size_t size) noexcept {
    if (size == 0) return true;
    auto cursor = reinterpret_cast<std::uintptr_t>(source);
    if (!cursor || size > (std::numeric_limits<std::uintptr_t>::max)() - cursor) return false;
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - start) return false;
        const auto next = start + region.RegionSize;
        if (next <= cursor) return false;
        cursor = next < end ? next : end;
    }
    return true;
}

int memory_exception(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// SEH is restricted to POD helpers; no C++ destructors are bypassed.
bool guarded_copy(void* target, const void* source, std::size_t size) noexcept {
    if (!readable(source, size)) return false;
    __try { if (size) std::memcpy(target, source, size); return true; }
    __except (memory_exception(GetExceptionCode())) { return false; }
}

template<class T> bool member(const void* object, std::size_t offset, T& value) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    if (!address || offset > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
    return guarded_copy(&value, reinterpret_cast<const void*>(address + offset), sizeof(T));
}

template<std::size_t N> bool name_equals(const void* native_string, const char (&expected)[N]) noexcept {
    MsvcString text{};
    if (!guarded_copy(&text, native_string, sizeof(text)) || text.size != N - 1 ||
        text.size > text.capacity || text.capacity > 4096) return false;
    unsigned char bytes[N]{};
    const void* data = text.capacity <= 15 ? static_cast<const void*>(text.data.small)
                                         : static_cast<const void*>(text.data.pointer);
    return guarded_copy(bytes, data, N - 1) && std::memcmp(bytes, expected, N - 1) == 0;
}

void write_log(const char* text) noexcept {
    if (log_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(log_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void rejected(unsigned long id, const char* reason) noexcept {
    char line[256]{};
    std::snprintf(line, sizeof(line), "RN_BRIDGE snapshot=%lu guard=%s\r\n", id, reason);
    write_log(line);
}

bool bounded_list(const void* container, std::size_t offset) noexcept {
    void* data = nullptr;
    int capacity = 0, count = 0;
    return member(container, offset, data) && member(container, offset + 8, capacity) &&
           member(container, offset + 12, count) && count >= 0 && count <= 4096 &&
           capacity >= count && capacity <= 65536 &&
           readable(data, static_cast<std::size_t>(count) * sizeof(void*));
}

void* guarded_find(void* container, const MsvcString* name) noexcept {
    __try { return find_edit(container, name, false, false); }
    __except (memory_exception(GetExceptionCode())) { return nullptr; }
}

// JSON-style escaping keeps each observation on one line. Native UTF32 length is
// distinct from UTF8 byte length. Buffer covers 128 * 6 escaped bytes plus NUL.
bool encode_utf8(const std::uint32_t* points, std::size_t count, char* out, std::size_t& size) noexcept {
    size = 0;
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < count; ++i) {
        const auto cp = points[i];
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (cp == '"' || cp == '\\') { out[size++] = '\\'; out[size++] = static_cast<char>(cp); }
        else if (cp < 0x20 || cp == 0x7f) {
            out[size++] = '\\'; out[size++] = 'u'; out[size++] = '0'; out[size++] = '0';
            out[size++] = digits[(cp >> 4) & 15]; out[size++] = digits[cp & 15];
        } else if (cp < 0x80) { out[size++] = static_cast<char>(cp); }
        else if (cp < 0x800) {
            out[size++] = static_cast<char>(0xc0 | (cp >> 6));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[size++] = static_cast<char>(0xe0 | (cp >> 12));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else {
            out[size++] = static_cast<char>(0xf0 | (cp >> 18));
            out[size++] = static_cast<char>(0x80 | ((cp >> 12) & 63));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        }
    }
    out[size] = '\0';
    return true;
}

bool capture_native(void* context, InputSnapshot& snapshot, unsigned long id) noexcept {
    std::uintptr_t context_vt = 0, template_vt = 0;
    void* model = nullptr;
    if (!member(context, 0, context_vt) || context_vt != image_base + 0x294E568 ||
        !member(context, 0x20, model) || !member(model, 0, template_vt) ||
        template_vt != image_base + 0x29DB558) return false;
    const auto model_address = reinterpret_cast<std::uintptr_t>(model);
    if (model_address > (std::numeric_limits<std::uintptr_t>::max)() - 8 ||
        !name_equals(reinterpret_cast<void*>(model_address + 8), "rn_native_probe_gui")) return false;

    void* container = nullptr;
    void* scripted_window = nullptr;
    void* window_container = nullptr;
    std::uintptr_t container_vt = 0;
    if (!member(context, 0x28, container) || !member(context, 0x10, scripted_window) ||
        !member(scripted_window, 0xF8, window_container) || container != window_container ||
        !member(container, 0, container_vt) || container_vt != image_base + 0x2B45278 ||
        !readable(container, 0x1360) || !bounded_list(container, 0x1240) ||
        !bounded_list(container, 0x1330)) { rejected(id, "root-container"); return false; }

    constexpr char edit_name[] = "rn_native_edit_unique";
    MsvcString name{};
    name.data.pointer = edit_name;
    name.size = sizeof(edit_name) - 1;
    name.capacity = sizeof(edit_name) - 1;
    void* edit = guarded_find(container, &name);
    std::uintptr_t edit_vt = 0, buffer_vt = 0;
    if (!member(edit, 0, edit_vt) || edit_vt != image_base + 0x2B4A940 ||
        !member(edit, 0x80, buffer_vt) || buffer_vt != image_base + 0x2B4ABD0) {
        rejected(id, "editbox-type-or-missing"); return false;
    }

    std::uint64_t size = 0, capacity = 0;
    if (!member(edit, 0xC0, size) || !member(edit, 0xC8, capacity) ||
        size > capacity || size > max_codepoints || capacity > 65536) {
        rejected(id, "utf32-size-capacity"); return false;
    }
    const void* text = reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(edit) + 0xB0);
    if (capacity > 3 && !member(edit, 0xB0, text)) { rejected(id, "utf32-pointer"); return false; }
    auto* points = snapshot.points;
    if (!guarded_copy(points, text, static_cast<std::size_t>(size) * sizeof(points[0]))) {
        rejected(id, "utf32-pages"); return false;
    }
    snapshot.size = size;
    snapshot.capacity = capacity;
    char utf8[max_codepoints * 6 + 1]{};
    std::size_t escaped_bytes = 0;
    const bool valid_utf8 = encode_utf8(points, static_cast<std::size_t>(size), utf8, escaped_bytes);
    if (!valid_utf8) {
        constexpr char invalid[] = "<invalid Unicode scalar>";
        std::memcpy(utf8, invalid, sizeof(invalid));
        escaped_bytes = std::strlen(utf8);
    }
    // Invalid uint32 values can print eight hex digits; they still become an
    // all-zero rejected transport through encode_field, with bounded diagnostics.
    char codes[max_codepoints * 9 + 1]{};
    std::size_t code_bytes = 0, utf8_bytes = 0;
    for (std::size_t i = 0; i < size; ++i) {
        code_bytes += static_cast<std::size_t>(std::snprintf(codes + code_bytes, sizeof(codes) - code_bytes,
            "%s%04X", i ? " " : "", static_cast<unsigned>(points[i])));
        const auto cp = points[i];
        if (valid_utf8) utf8_bytes += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    }
    char line[3072]{};
    std::snprintf(line, sizeof(line),
        "RN_BRIDGE snapshot=%lu pid=%lu utf32_len=%llu utf32_capacity=%llu utf8_valid=%u utf8_bytes=%zu "
        "escaped_bytes=%zu text=\"%s\" codepoints=[%s]; read-only snapshot\r\n",
        id, GetCurrentProcessId(), static_cast<unsigned long long>(size),
        static_cast<unsigned long long>(capacity), valid_utf8 ? 1u : 0u, utf8_bytes, escaped_bytes, utf8, codes);
    write_log(line);
    return true;
}

bool own_commit(void* context, const MsvcString* selector, const MsvcString*& model_name) noexcept {
    std::uintptr_t context_vt = 0, template_vt = 0;
    void* model = nullptr;
    if (!member(context, 0, context_vt) || context_vt != image_base + 0x294E568 ||
        !member(context, 0x20, model) || !member(model, 0, template_vt) ||
        template_vt != image_base + 0x29DB558) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(model);
    if (address > (std::numeric_limits<std::uintptr_t>::max)() - 8) return false;
    model_name = reinterpret_cast<const MsvcString*>(address + 8);
    return name_equals(model_name, "rn_native_probe_gui") &&
           name_equals(selector, "rn_native_probe_commit_click");
}

bool record_key(const Record& record, char (&out)[129]) noexcept {
    const auto& key = record.key;
    if (key.size > 128 || key.size > key.capacity || key.capacity > 1048576) return false;
    const void* data = key.capacity <= 15 ? static_cast<const void*>(key.data.small)
                                         : static_cast<const void*>(key.data.pointer);
    return guarded_copy(out, data, static_cast<std::size_t>(key.size) + 1) && out[key.size] == '\0' &&
           std::memchr(out, '\0', static_cast<std::size_t>(key.size)) == nullptr;
}

bool reserved_key(const char* key) noexcept {
    if (std::strcmp(key, "rn_native_ok") == 0 || std::strcmp(key, "rn_native_len") == 0) return true;
    for (unsigned i = 0; i < 32; ++i) {
        char expected[16]{};
        std::snprintf(expected, sizeof(expected), "rn_native_%u", i);
        if (std::strcmp(key, expected) == 0) return true;
    }
    return false;
}

void transport_record(Record& record, const char* key, std::int64_t value) noexcept {
    // Every call uses one of the known <=15-byte names and a bounded integer.
    record = {};
    const auto size = std::strlen(key);
    std::memcpy(record.key.data.small, key, size);
    record.key.size = size;
    record.key.capacity = 15;
    record.raw_value = value * fixed_point;
}

bool prepare_transport(Origin& current, const InputSnapshot& snapshot) noexcept {
    const auto context_address = reinterpret_cast<std::uintptr_t>(current.context);
    if (context_address > (std::numeric_limits<std::uintptr_t>::max)() - 0x470) return false;
    current.scope = reinterpret_cast<const void*>(context_address + 0x3C0);
    current.source = reinterpret_cast<const TempVector*>(context_address + 0x470);
    TempVector source{};
    if (!guarded_copy(&source, current.source, sizeof(source)) || source.count < 0 ||
        source.count > static_cast<int>(max_original_records) || source.capacity < source.count ||
        source.capacity > 65536 || !readable(source.data, static_cast<std::size_t>(source.count) * sizeof(Record))) {
        rejected(current.id, "source-vector"); return false;
    }
    std::size_t kept = 0, sanitized = 0;
    for (int i = 0; i < source.count; ++i) {
        Record record{};
        char key[129]{};
        if (!guarded_copy(&record, source.data + i, sizeof(record)) || !record_key(record, key)) {
            rejected(current.id, "source-record-or-key"); return false;
        }
        if (reserved_key(key)) { ++sanitized; continue; }
        current.records[kept++] = record; // Trivial bytes; borrowed keys are never destroyed here.
    }

    char32_t input[max_codepoints]{};
    for (std::size_t i = 0; i < snapshot.size; ++i) input[i] = static_cast<char32_t>(snapshot.points[i]);
    const auto encoded = rn::editor::encode_field(std::u32string_view(input, static_cast<std::size_t>(snapshot.size)));
    rn::editor::FieldCodes field{};
    if (encoded.value) field = *encoded.value;
    const auto base_count = kept;
    transport_record(current.records[kept++], "rn_native_ok", encoded.value ? 1 : 0);
    transport_record(current.records[kept++], "rn_native_len", static_cast<std::int64_t>(field.length));
    for (unsigned i = 0; i < 32; ++i) {
        char key[16]{};
        std::snprintf(key, sizeof(key), "rn_native_%u", i);
        transport_record(current.records[kept++], key, field.codes[i]);
    }
    current.view = {current.records, static_cast<std::int32_t>(kept), static_cast<std::int32_t>(kept), source.allocator};
    char line[384]{};
    std::snprintf(line, sizeof(line),
        "RN_BRIDGE snapshot=%lu prepared ok=%u len=%zu input_error=%u error_index=%zu "
        "original=%d kept=%zu sanitized=%zu appended=%zu total=%d\r\n",
        current.id, encoded.value ? 1u : 0u, field.length, static_cast<unsigned>(encoded.error),
        encoded.error_index, source.count, base_count, sanitized, transport_records, current.view.count);
    write_log(line);
    return true;
}

void* __fastcall constructor_bridged(void* self, const MsvcString* gui, const MsvcString* selector,
                                     const void* scope, const TempVector* source) {
    Origin* current = origin;
    if (!active.load(std::memory_order_acquire) || !current || gui != current->model_name ||
        selector != current->selector || scope != current->scope || source != current->source ||
        !name_equals(gui, "rn_native_probe_gui") || !name_equals(selector, "rn_native_probe_commit_click")) {
        return original_ctor(self, gui, selector, scope, source);
    }
    ++current->ctor_calls;
    // Exactly one original call; only the const input view changes. The engine
    // deep-copies every key/value. No input or widget pointer enters the command.
    void* result = original_ctor(self, gui, selector, scope, &current->view);
    TempVector copied{};
    const bool independent = member(result, 0x28, copied) && copied.count == current->view.count &&
        copied.data != current->view.data &&
        readable(copied.data, static_cast<std::size_t>(copied.count) * sizeof(Record));
    char line[256]{};
    std::snprintf(line, sizeof(line),
        "RN_BRIDGE snapshot=%lu ctor_calls=%u result_is_this=%u independent_record_storage=%u records=%d\r\n",
        current->id, current->ctor_calls, result == self ? 1u : 0u, independent ? 1u : 0u, copied.count);
    write_log(line);
    // A started native ctor cannot safely be canceled or retried. Return its
    // original result unchanged, including on an unexpected diagnostic result.
    return result;
}

void __fastcall dispatch_observed(void* context, void* opaque_selector) {
    const auto* selector = static_cast<const MsvcString*>(opaque_selector);
    const MsvcString* model_name = nullptr;
    if (!active.load(std::memory_order_acquire) || !own_commit(context, selector, model_name)) {
        original_dispatch(context, opaque_selector); return;
    }
    Origin current{};
    current.context = context;
    current.model_name = model_name;
    current.selector = selector;
    current.id = snapshot_id.fetch_add(1) + 1;
    InputSnapshot snapshot{};
    if (!original_ctor || !capture_native(context, snapshot, current.id) || !prepare_transport(current, snapshot)) {
        rejected(current.id, "own-commit-dropped-before-dispatch"); return;
    }
    {
        OriginFrame frame(&current);
        original_dispatch(context, opaque_selector);
    }
    if (current.ctor_calls != 1) rejected(current.id, "unexpected-ctor-count-no-retry");
}

template<std::size_t N> bool unique_at(const unsigned char* base, const IMAGE_SECTION_HEADER* text,
                                     std::size_t rva, const unsigned char (&bytes)[N]) noexcept {
    if (rva < text->VirtualAddress || rva - text->VirtualAddress + N > text->Misc.VirtualSize) return false;
    const auto* data = base + text->VirtualAddress;
    std::size_t count = 0;
    for (std::size_t i = 0; i + N <= text->Misc.VirtualSize; ++i) {
        if (std::memcmp(data + i, bytes, N) == 0) {
            if (i + text->VirtualAddress != rva || ++count > 1) return false;
        }
    }
    return count == 1;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    unsigned expected = 0;
    if (!init_state.compare_exchange_strong(expected, 1)) return active.load() ? 0 : 33;
    try {
        if (GetModuleHandleW(L"rename_anything_editor_observer.dll")) return 40;
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768) ||
            rn::sha256_file(executable) != rn::exe_sha256) return 10;
        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew <= 0 || dos->e_lfanew > 65536) return 11;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.NumberOfSections > 96) return 11;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (!readable(sections, nt->FileHeader.NumberOfSections * sizeof(*sections))) return 11;
        const IMAGE_SECTION_HEADER* text = nullptr;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (std::memcmp(sections[i].Name, ".text\0\0\0", 8) == 0) text = sections + i;
        if (!text || text->VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            text->Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - text->VirtualAddress ||
            !readable(base + text->VirtualAddress, text->Misc.VirtualSize)) return 12;
        constexpr unsigned char dispatch_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
            0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x4c,0x8b,0xfa,0x48,0x8b,0xf9,0x48,0x8d,
            0x99,0xc0,0x03,0x00,0x00,0x45,0x33,0xc0};
        constexpr unsigned char find_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,
            0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,
            0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0x40,0x12,0x00,0x00,0x48,0x8d,0x91,0x58,0x12,
            0x00,0x00,0x49,0x8b,0xcf};
        constexpr unsigned char ctor_bytes[] = {
            0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,
            0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x0f,0x29,0x74,0x24,0x50,0x4d,0x8b,
            0xf1,0x49,0x8b,0xf8,0x48,0x8b,0xf2,0x4c,0x8b,0xe1};
        if (!unique_at(base, text, dispatch_rva, dispatch_bytes) ||
            !unique_at(base, text, find_edit_rva, find_bytes) ||
            !unique_at(base, text, ctor_rva, ctor_bytes)) return 20;

        wchar_t log_path[32768]{};
        if (!GetModuleFileNameW(own_module, log_path, 32768)) return 13;
        auto* slash = std::wcsrchr(log_path, L'\\');
        if (!slash || wcscpy_s(slash + 1, 32768 - static_cast<std::size_t>(slash + 1 - log_path),
                              L"editor_bridge.log") != 0) return 13;
        log_file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_file == INVALID_HANDLE_VALUE) return 13;
        image_base = reinterpret_cast<std::uintptr_t>(base);
        find_edit = reinterpret_cast<FindEdit>(image_base + find_edit_rva);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&RNInitialize), &pinned)) return 14;
        if (MH_Initialize() != MH_OK) return 30;
        auto* target = const_cast<unsigned char*>(base) + dispatch_rva;
        auto* ctor_target = const_cast<unsigned char*>(base) + ctor_rva;
        if (MH_CreateHook(target, reinterpret_cast<void*>(&dispatch_observed),
                          reinterpret_cast<void**>(&original_dispatch)) != MH_OK ||
            MH_CreateHook(ctor_target, reinterpret_cast<void*>(&constructor_bridged),
                          reinterpret_cast<void**>(&original_ctor)) != MH_OK) {
            MH_Uninitialize(); return 31;
        }
        if (MH_EnableHook(ctor_target) != MH_OK || MH_EnableHook(target) != MH_OK) {
            // Two specific hooks, never MH_ALL_HOOKS. Transport stays disabled;
            // preserve any trampoline that could already have an in-flight caller.
            MH_DisableHook(target);
            MH_DisableHook(ctor_target);
            write_log("RN_BRIDGE initialization failed; transport disabled; restart required\r\n");
            return 32;
        }
        active.store(true, std::memory_order_release);
        write_log("RN_BRIDGE initialized: exact hash and 3 unique fingerprints; own probe commit transport only\r\n");
        return 0;
    } catch (...) { return 99; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) own_module = instance;
    return TRUE;
}
