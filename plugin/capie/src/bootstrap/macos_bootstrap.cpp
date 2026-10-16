/** @file macos_bootstrap.cpp
 * @brief Minimal first-inserted bootstrap. Build without libc++, exceptions or RTTI.
 * A generated immutable policy is required; an unconfigured build refuses activation.
 * No thread count heuristic is used: lifecycle authorization belongs to the audited
 * exact image/dependency profile selected by the generated policy.
 */
#include "macos_bootstrap_api.h"
#include "../core/macos_patch.hpp"
#include "../core/macos_image.hpp"
#include "../core/macos_activation_checks.hpp"
#include "../core/macos_pins.hpp"
#include "../core/macos_profile.generated.hpp"
#ifdef CAPIE_BOOTSTRAP_FIXTURE_PREFIXES
#include CAPIE_BOOTSTRAP_FIXTURE_PREFIXES
#else
#include "../core/macos_hook_prefixes.h"
#endif
#include <atomic>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdlib>
#include <crt_externs.h>

#if __has_include("macos_bootstrap_policy.h")
#include "macos_bootstrap_policy.h"
#else
#define CAPIE_BOOTSTRAP_POLICY_READY 0
#endif

namespace patch = capie::core::macos::patch;
namespace image = capie::core::macos::image;
namespace pins = capie::core::macos::pins;
namespace profile = capie::core::macos::profile;
static constexpr unsigned hook_count = 15;
static_assert(capie_macos_hook_prefix_count == hook_count);
static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(sizeof(std::atomic<void*>) == sizeof(void*));
extern "C" {
/// Assembly tail jumps load these aligned pointers with x86 acquire semantics.
__attribute__((visibility("hidden"))) constinit std::atomic<void*> capie_bootstrap_slots[hook_count]{};
}
namespace {
constinit patch::Hook hooks[hook_count]{};
constinit std::atomic<uint32_t> status{CAPIE_BOOTSTRAP_DISABLED};
constinit uintptr_t verified_base = 0;
constinit int64_t verified_slide = 0;
#define STUB(N) \
 extern "C" void capie_bootstrap_stub_##N(); \
 asm(".text\n.p2align 4\n.private_extern _capie_bootstrap_stub_" #N "\n" \
 "_capie_bootstrap_stub_" #N ":\n jmpq *_capie_bootstrap_slots+8*" #N "(%rip)\n");
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7)
STUB(8) STUB(9) STUB(10) STUB(11) STUB(12) STUB(13) STUB(14)
#undef STUB
using Entry = void (*)();
constexpr Entry entries[] = {
    capie_bootstrap_stub_0, capie_bootstrap_stub_1, capie_bootstrap_stub_2,
    capie_bootstrap_stub_3, capie_bootstrap_stub_4, capie_bootstrap_stub_5,
    capie_bootstrap_stub_6, capie_bootstrap_stub_7, capie_bootstrap_stub_8,
    capie_bootstrap_stub_9, capie_bootstrap_stub_10, capie_bootstrap_stub_11,
    capie_bootstrap_stub_12, capie_bootstrap_stub_13, capie_bootstrap_stub_14
};

/** A bounded immutable file mapping: no game or library file is ever written. */
struct File {
    const unsigned char* bytes = nullptr;
    size_t size = 0;
    explicit File(const char* path) noexcept {
        if (!path || path[0] != '/') return;
        const int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return;
        struct stat st{};
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 32 &&
            st.st_size <= 1073741824) {
            void* p = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
            if (p != MAP_FAILED) { bytes = static_cast<const unsigned char*>(p); size = static_cast<size_t>(st.st_size); }
        }
        close(fd);
    }
    ~File() { if (bytes) munmap(const_cast<unsigned char*>(bytes), size); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

/** Verify actual loaded main header and every static pin before any page mutation. */
[[maybe_unused]] bool verify_main(image::Image& parsed, uint32_t index) noexcept {
    File file(_dyld_get_image_name(index));
    if (!file.bytes) return false;
    verified_slide = _dyld_get_image_vmaddr_slide(index);
    if (!image::verify_game({file.bytes, file.size}, verified_slide, parsed) ||
        !parsed.runtime_address(image::preferred_base, sizeof(mach_header_64), verified_base) ||
        verified_base != reinterpret_cast<uintptr_t>(_dyld_get_image_header(index))) return false;
    mach_header_64 header{};
    std::memcpy(&header, file.bytes, sizeof(header));
    constexpr size_t limit = 131072;
    const size_t length = sizeof(header) + header.sizeofcmds;
    std::array<unsigned char, limit> loaded{};
    capie::core::macos::ProcessMemory memory;
    if (length > limit || length > file.size || !memory.read(verified_base, loaded.data(), length) ||
        std::memcmp(file.bytes, loaded.data(), length)) return false;
    for (const auto& pin : profile::bytes) if (!pins::bytes_match(memory, parsed, pin)) return false;
    for (const auto& pin : profile::pointers) if (!pins::pointer_matches(memory, parsed, pin)) return false;
    for (const auto& pin : profile::relatives) if (!pins::relative_matches(memory, parsed, pin)) return false;
    return profile::bytes.size() + profile::pointers.size() + profile::relatives.size() == 240 &&
           capie::core::macos::activation::allocator_matches(memory, verified_base);
}

/** Install all original-forward gates while the selected lifecycle is quiescent. */
[[maybe_unused]] void install_all(const image::Image& parsed) noexcept {
    patch::NativeMemory memory;
    std::array<uintptr_t, hook_count> sources{};
    // Preflight all prefixes before the first mutation, not one hook at a time.
    for (unsigned i = 0; i < hook_count; ++i) {
        const auto& pin = capie_macos_hook_prefixes[i];
        if (!parsed.runtime_address(image::preferred_base + pin.rva, pin.length, sources[i]) ||
            !pins::bytes_match(memory, parsed, {image::preferred_base + pin.rva, {pin.bytes, pin.length}})) return;
        patch::Page source{}, destination{};
        if (!patch::relocation_free({pin.bytes, pin.length}) ||
            !memory.inspect(sources[i], pin.length, source) ||
            !memory.inspect(reinterpret_cast<uintptr_t>(entries[i]), 1, destination) ||
            source.address == destination.address) return;
    }
    unsigned installed = 0;
    for (; installed < hook_count; ++installed) {
        const auto& pin = capie_macos_hook_prefixes[installed];
        if (patch::install(memory, hooks[installed], sources[installed],
                           reinterpret_cast<uintptr_t>(entries[installed]),
                           {pin.bytes, pin.length}, true) != patch::Result::ok) break;
        capie_bootstrap_slots[installed].store(reinterpret_cast<void*>(hooks[installed].trampoline), std::memory_order_release);
    }
    if (installed == hook_count) {
        status.store(CAPIE_BOOTSTRAP_FORWARDING, std::memory_order_release);
        return;
    }
    bool poisoned = hooks[installed].state == patch::State::poisoned;
    while (installed) {
        --installed;
        if (patch::remove(memory, hooks[installed], true) != patch::Result::ok) poisoned = true;
        else capie_bootstrap_slots[installed].store(nullptr, std::memory_order_release);
    }
    // Catastrophic OS rollback failure is explicitly distinct from safe refusal.
    if (poisoned) status.store(CAPIE_BOOTSTRAP_POISONED, std::memory_order_release);
}

#if CAPIE_BOOTSTRAP_POLICY_READY
/** Select a bounded x86_64 slice after hashing the complete universal carrier. */
std::span<const unsigned char> x86_slice(std::span<const unsigned char> file) noexcept {
    const auto big = [&file](size_t at, unsigned width) {
        uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i) value = (value << 8) | file[at + i];
        return value;
    };
    if (file.size() < sizeof(mach_header_64)) return {};
    const uint64_t magic = big(0, 4);
    if (magic != 0xcafebabe && magic != 0xcafebabf) return file;
    const uint64_t count = big(4, 4);
    const size_t stride = magic == 0xcafebabf ? 32 : 20;
    if (!count || count > 64 || count > (file.size() - 8) / stride) return {};
    const size_t table_end = 8 + static_cast<size_t>(count) * stride;
    std::span<const unsigned char> selected;
    for (size_t i = 0; i < count; ++i) {
        const size_t at = 8 + i * stride;
        const unsigned width = stride == 32 ? 8 : 4;
        const uint64_t offset = big(at + 8, width), size = big(at + 8 + width, width);
        if (offset < table_end || offset > file.size() || size < sizeof(mach_header_64) ||
            size > file.size() - offset) return {};
        if (big(at, 4) != CPU_TYPE_X86_64) continue;
        if (!selected.empty()) return {};
        selected = file.subspan(static_cast<size_t>(offset), static_cast<size_t>(size));
    }
    return selected;
}

/** File carrier identity plus the matching loaded x86 header; no image-index assumptions. */
bool loaded_file_matches(uint32_t index, const char* expected_hash) noexcept {
    File file(_dyld_get_image_name(index));
    if (!file.bytes || !image::sha256_matches({file.bytes, file.size}, expected_hash)) return false;
    const auto slice = x86_slice({file.bytes, file.size});
    if (slice.size() < sizeof(mach_header_64)) return false;
    mach_header_64 header{};
    std::memcpy(&header, slice.data(), sizeof(header));
    const size_t length = sizeof(header) + header.sizeofcmds;
    std::array<unsigned char, 131072> loaded{};
    return header.magic == MH_MAGIC_64 && header.cputype == CPU_TYPE_X86_64 &&
        header.filetype == MH_DYLIB && length <= slice.size() && length <= loaded.size() &&
        capie::core::macos::ProcessMemory{}.read(
            reinterpret_cast<uintptr_t>(_dyld_get_image_header(index)), loaded.data(), length) &&
        std::memcmp(slice.data(), loaded.data(), length) == 0;
}

/** The production closure must include all three known bundled game dependencies. */
bool dependencies_match(uint32_t image_count) noexcept {
    constexpr size_t count = CAPIE_BOOTSTRAP_DEPENDENCY_COUNT;
    static_assert(sizeof(capie_bootstrap_dependency_paths) / sizeof(capie_bootstrap_dependency_paths[0]) >= count);
    static_assert(sizeof(capie_bootstrap_dependency_hashes) / sizeof(capie_bootstrap_dependency_hashes[0]) >= count);
#ifndef CAPIE_BOOTSTRAP_FIXTURE_PREFIXES
    if (count != 3) return false;
#endif
    if (count > 3) return false;
    for (size_t i = 0; i < count; ++i) {
        for (size_t prior = 0; prior < i; ++prior)
            if (!std::strcmp(capie_bootstrap_dependency_paths[i], capie_bootstrap_dependency_paths[prior])) return false;
        uint32_t found = 0, selected = 0;
        for (uint32_t j = 0; j < image_count; ++j) {
            const char* name = _dyld_get_image_name(j);
            if (name && !std::strcmp(name, capie_bootstrap_dependency_paths[i])) { ++found; selected = j; }
        }
        if (found != 1 || !loaded_file_matches(selected, capie_bootstrap_dependency_hashes[i])) return false;
    }
    return true;
}

/** Exact inserted order and immutable allowlist; unknown DYLD overrides refuse. */
bool policy_matches() noexcept {
    const char* inserted = std::getenv("DYLD_INSERT_LIBRARIES");
    if (!inserted || std::strcmp(inserted, capie_bootstrap_expected_inserted)) return false;
    // Use the platform accessor: declaring environ inside a namespace changes linkage.
    char** environment = *_NSGetEnviron();
    for (char** p = environment; p && *p; ++p)
        if (!std::strncmp(*p, "DYLD_", 5) && std::strncmp(*p, "DYLD_INSERT_LIBRARIES=", sizeof("DYLD_INSERT_LIBRARIES=") - 1)) return false;
    Dl_info self{};
    if (!dladdr(reinterpret_cast<void*>(capie_macos_bootstrap_state), &self)) return false;
    const size_t count = sizeof(capie_bootstrap_inserted_paths) / sizeof(capie_bootstrap_inserted_paths[0]);
    static_assert(sizeof(capie_bootstrap_inserted_hashes) / sizeof(capie_bootstrap_inserted_hashes[0]) == count);
    const uint32_t image_count = _dyld_image_count();
    uint32_t executable = 0;
    if (!count || count > 8 || image_count < count ||
        !image::executable_index(image_count, _dyld_get_image_header, executable)) return false;
    uint32_t image_index = 0;
    for (size_t i = 0; i < count; ++i, ++image_index) {
        // dyld permutes the main to index zero for x86 executables targeting < macOS 12.
        // Skip only that unique executable; an unexpected dylib must still fail order checks.
        if (image_index == executable) ++image_index;
        if (image_index >= image_count) return false;
        const auto* loaded_header = _dyld_get_image_header(image_index);
        if (!loaded_header || loaded_header->filetype != MH_DYLIB ||
            (!i && self.dli_fbase != loaded_header)) return false;
        const char* name = _dyld_get_image_name(image_index);
        if (!name || std::strcmp(name, capie_bootstrap_inserted_paths[i])) return false;
        if (!i) continue; // Self hash would be circular; launcher verifies the bootstrap artifact.
        if (!loaded_file_matches(image_index, capie_bootstrap_inserted_hashes[i])) return false;
    }
    if (!dependencies_match(image_count)) return false;
    // The only non-system startup images are main, the exact inserted list and the three dependencies.
    for (uint32_t j = 0; j < image_count; ++j) {
        if (j == executable) continue;
        const char* name = _dyld_get_image_name(j);
        if (!name) return false;
        if (!std::strncmp(name, "/usr/lib/", 9) || !std::strncmp(name, "/System/Library/", 16)) continue;
        bool known = false;
        for (const char* path : capie_bootstrap_inserted_paths) known |= !std::strcmp(path, name);
        for (size_t i = 0; i < CAPIE_BOOTSTRAP_DEPENDENCY_COUNT; ++i)
            known |= !std::strcmp(capie_bootstrap_dependency_paths[i], name);
        if (!known) return false;
    }
    return _dyld_image_count() == image_count;
}
#endif

/** Constructor has no C++ runtime dependency and never enables feature dispatch. */
__attribute__((constructor)) void initialise() noexcept {
#if CAPIE_BOOTSTRAP_POLICY_READY
    if (!policy_matches()) return;
    uint32_t index = 0;
    if (!image::executable_index(_dyld_image_count(), _dyld_get_image_header, index)) return;
    image::Image parsed;
    if (!verify_main(parsed, index)) return;
    install_all(parsed);
#endif
}
} // namespace

extern "C" uint32_t capie_macos_bootstrap_state() { return status.load(std::memory_order_acquire); }
extern "C" void* capie_macos_bootstrap_trampoline(uint32_t index) {
    return index < hook_count && capie_macos_bootstrap_state() == CAPIE_BOOTSTRAP_FORWARDING ?
        reinterpret_cast<void*>(hooks[index].trampoline) : nullptr;
}
extern "C" bool capie_macos_bootstrap_set_dispatch(uint32_t index, void* destination) {
    void* original = capie_macos_bootstrap_trampoline(index);
    if (!original) return false;
    if (destination) {
        patch::Page page{};
        if (!patch::NativeMemory{}.inspect(reinterpret_cast<uintptr_t>(destination), 1, page)) return false;
    }
    capie_bootstrap_slots[index].store(destination ? destination : original, std::memory_order_release);
    return true;
}
extern "C" uintptr_t capie_macos_bootstrap_image_base() {
    return capie_macos_bootstrap_state() == CAPIE_BOOTSTRAP_FORWARDING ? verified_base : 0;
}
extern "C" int64_t capie_macos_bootstrap_slide() {
    return capie_macos_bootstrap_state() == CAPIE_BOOTSTRAP_FORWARDING ? verified_slide : 0;
}
