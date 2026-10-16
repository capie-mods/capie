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
/** Exact inserted order and immutable allowlist; unknown DYLD overrides refuse. */
bool policy_matches() noexcept {
    const char* inserted = std::getenv("DYLD_INSERT_LIBRARIES");
    if (!inserted || std::strcmp(inserted, capie_bootstrap_expected_inserted)) return false;
    // Use the platform accessor: declaring environ inside a namespace changes linkage.
    char** environment = *_NSGetEnviron();
    for (char** p = environment; p && *p; ++p)
        if (!std::strncmp(*p, "DYLD_", 5) && std::strncmp(*p, "DYLD_INSERT_LIBRARIES=", sizeof("DYLD_INSERT_LIBRARIES=") - 1)) return false;
    Dl_info self{};
    if (!dladdr(reinterpret_cast<void*>(capie_macos_bootstrap_state), &self) ||
        self.dli_fbase != _dyld_get_image_header(0)) return false;
    const size_t count = sizeof(capie_bootstrap_inserted_paths) / sizeof(capie_bootstrap_inserted_paths[0]);
    static_assert(sizeof(capie_bootstrap_inserted_hashes) / sizeof(capie_bootstrap_inserted_hashes[0]) == count);
    if (!count || count > 8 || _dyld_image_count() < count) return false;
    for (size_t i = 0; i < count; ++i) {
        const char* name = _dyld_get_image_name(static_cast<uint32_t>(i));
        if (!name || std::strcmp(name, capie_bootstrap_inserted_paths[i])) return false;
        if (!i) continue; // Self hash would be circular; launcher verifies the bootstrap artifact.
        File file(name);
        if (!file.bytes || !image::sha256_matches({file.bytes, file.size}, capie_bootstrap_inserted_hashes[i])) return false;
        mach_header_64 header{};
        std::memcpy(&header, file.bytes, sizeof(header));
        const size_t length = sizeof(header) + header.sizeofcmds;
        std::array<unsigned char, 131072> loaded{};
        if (header.magic != MH_MAGIC_64 || header.filetype != MH_DYLIB || length > file.size || length > loaded.size() ||
            !capie::core::macos::ProcessMemory{}.read(reinterpret_cast<uintptr_t>(_dyld_get_image_header(static_cast<uint32_t>(i))), loaded.data(), length) ||
            std::memcmp(file.bytes, loaded.data(), length)) return false;
    }
    return true;
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
