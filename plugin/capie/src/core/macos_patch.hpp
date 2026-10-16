/**
 * @file macos_patch.hpp
 * @brief Restricted x86_64 detours for caller-quiescent owned code, not game activation.
 *
 * A 14-byte indirect jump replaces a fully pinned, conservatively decoded prefix.
 * No executor may execute, change protections, write, or unmap the source page,
 * destination, or trampoline during install/remove. The caller must establish that
 * precondition externally; this module neither suspends threads nor proves quiescence.
 * The current thread must run this backend and every policy/helper call on other pages.
 * The trampoline has no unwind metadata: exceptions and stack unwinding through it
 * are forbidden. Only REX.W 48/49 B8..BF MOVABS is accepted; other unlisted forms are rejected.
 * No RIP-relative operand or control-flow instruction is relocated. A failed rollback
 * leaves a poisoned handle and retained trampoline; it must never be called "disabled".
 * Explicit remove is required; there is no destructor that silently unhooks live code.
 */
#pragma once
#include "macos_memory.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <libkern/OSCacheControl.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>

namespace capie::core::macos::patch {
/// @brief Size of FF 25 disp32=0 followed by an absolute 64-bit destination.
inline constexpr std::size_t jump_size = 14;
/// @brief Deliberate upper bound of the relocated prefix.
inline constexpr std::size_t max_prefix = 32;
/// @brief Handle ownership state; poisoned means restoration could not be proved.
enum class State { empty, installed, poisoned };
/// @brief Failure before mutation differs from rollback/protection failure.
enum class Result { ok, invalid, mismatch, unsupported, allocation_failed, protection_failed, rolled_back, rollback_failed };
/// @brief Protection snapshot of the single page this backend may modify.
struct Page {
    std::uintptr_t address = 0; ///< Page start.
    std::size_t size = 0; ///< Page bytes.
    vm_prot_t current = 0; ///< Current protection before mutation.
    vm_prot_t maximum = 0; ///< Maximum protection before mutation.
};
/// @brief Explicit noncopyable owner of the trampoline and original bytes.
struct Hook {
    State state = State::empty; ///< Installed/poisoned state is never implicitly released.
    std::uintptr_t source = 0; ///< Source entry point.
    std::uintptr_t trampoline = 0; ///< Callable only while installed and not concurrently removed.
    std::size_t trampoline_size = 0; ///< Allocation size for explicit release.
    std::size_t length = 0; ///< Pinned prefix byte count.
    Page page{}; ///< Original protections.
    std::array<unsigned char, max_prefix> original{}; ///< Original prefix.
    std::array<unsigned char, max_prefix> patched{}; ///< Expected installed prefix.
    Hook() = default; ///< Construct an empty handle.
    Hook(const Hook&) = delete; ///< Ownership cannot be copied.
    Hook& operator=(const Hook&) = delete; ///< Ownership cannot be copied.
};

/** @brief Recognize only explicitly supported relocation-free whole instructions.
 * @param bytes Prefix bytes; no instruction may end beyond the span.
 * @return Whether every byte belongs to a supported full instruction.
 */
inline bool relocation_free(std::span<const unsigned char> bytes) noexcept {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto op = bytes[i];
        std::size_t length = 0;
        if (op == 0x90 || (op >= 0x50 && op <= 0x5f)) length = 1;
        else if (op >= 0xb8 && op <= 0xbf) length = 5;
        else if ((op == 0x48 || op == 0x49) && i + 1 < bytes.size() &&
                 bytes[i+1] >= 0xb8 && bytes[i+1] <= 0xbf) length = 10;
        else if (op == 0x41 && i + 1 < bytes.size() && bytes[i+1] >= 0x50 && bytes[i+1] <= 0x5f) length = 2;
        else if ((op == 0x48 || op == 0x49 || op == 0x4c || op == 0x4d) && i + 2 < bytes.size() &&
                 (bytes[i+1] == 0x89 || bytes[i+1] == 0x8b) && (bytes[i+2] & 0xc0) == 0xc0) length = 3;
        else if (op == 0x48 && i + 2 < bytes.size() && bytes[i+2] == 0xec && bytes[i+1] == 0x83) length = 4;
        else if (op == 0x48 && i + 2 < bytes.size() && bytes[i+2] == 0xec && bytes[i+1] == 0x81) length = 7;
        if (length == 0 || length > bytes.size() - i) return false;
        i += length;
    }
    return !bytes.empty();
}

/// @brief Native policy: all reads are bounded Mach reads; writes occur only after an RW transition.
struct NativeMemory {
    /** @brief Read this process safely. @param address Source. @param out Destination. @param size Bytes. @return Success. */
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        return ProcessMemory{}.read(address, out, size);
    }
    /** @brief Snapshot one RX page containing the full range.
     * @param address Source VA. @param size Range bytes. @param out Snapshot. @return Valid RX range.
     */
    bool inspect(std::uintptr_t address, std::size_t size, Page& out) const noexcept {
        if (!address || !size || size > std::numeric_limits<std::uintptr_t>::max() - address) return false;
        const auto page_size = static_cast<std::size_t>(getpagesize());
        const auto page = address & ~(page_size - 1);
        if (size > page_size - (address - page)) return false;
        mach_vm_address_t region = address;
        mach_vm_size_t region_size = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const auto result = mach_vm_region(mach_task_self(), &region, &region_size, VM_REGION_BASIC_INFO_64,
                                          reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (result != KERN_SUCCESS || region > page || region_size < page - region + page_size ||
            info.protection != (VM_PROT_READ | VM_PROT_EXECUTE)) return false;
        out = {page, page_size, info.protection, info.max_protection};
        return true;
    }
    /** @brief Make a private RW copy, never RWX. @param page Snapshot. @return OS success. */
    bool writable(const Page& page) const noexcept {
        return mach_vm_protect(mach_task_self(), page.address, page.size, false,
                               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS;
    }
    /** @brief Restore both protection bounds. @param page Snapshot. @return Both transitions succeeded. */
    bool restore(const Page& page) const noexcept {
        const auto current = mach_vm_protect(mach_task_self(), page.address, page.size, false, page.current);
        const auto maximum = mach_vm_protect(mach_task_self(), page.address, page.size, true, page.maximum);
        return current == KERN_SUCCESS && maximum == KERN_SUCCESS;
    }
    /** @brief Write through Mach without dereferencing untrusted addresses.
     * @param address Destination. @param bytes Source. @param size Bytes (bounded by prefix).
     * @return OS write succeeded.
     */
    bool write(std::uintptr_t address, const void* bytes, std::size_t size) const noexcept {
        if (size > 4096) return false;
        return mach_vm_write(mach_task_self(), address, reinterpret_cast<vm_offset_t>(bytes),
                             static_cast<mach_msg_type_number_t>(size)) == KERN_SUCCESS;
    }
    /** @brief Flush modified instructions. @param address Start. @param size Bytes. */
    void flush(std::uintptr_t address, std::size_t size) const noexcept {
        sys_icache_invalidate(reinterpret_cast<void*>(address), size);
    }
    /** @brief Allocate RW trampoline storage. @param size Requested bytes. @return VA or zero. */
    std::uintptr_t allocate(std::size_t size) const noexcept {
        void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        return memory == MAP_FAILED ? 0 : reinterpret_cast<std::uintptr_t>(memory);
    }
    /** @brief Seal a new trampoline RX. @param address Start. @param size Bytes. @return OS success. */
    bool seal(std::uintptr_t address, std::size_t size) const noexcept {
        return mprotect(reinterpret_cast<void*>(address), size, PROT_READ | PROT_EXEC) == 0;
    }
    /** @brief Release an unreachable trampoline. @param address Start. @param size Bytes. */
    void release(std::uintptr_t address, std::size_t size) const noexcept {
        munmap(reinterpret_cast<void*>(address), size);
    }
};

/** @brief Emit a register-preserving absolute indirect jump.
 * @param bytes At least 14 writable bytes. @param target Destination VA.
 */
inline void jump(unsigned char* bytes, std::uintptr_t target) noexcept {
    const unsigned char prefix[] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(bytes, prefix, 6);
    std::memcpy(bytes + 6, &target, 8);
}
/** @brief Compare live prefix through the policy. @param memory Policy. @param address Source.
 * @param expected Expected bytes. @return Complete match.
 */
template<class M>
bool matches(M& memory, std::uintptr_t address, std::span<const unsigned char> expected) noexcept {
    std::array<unsigned char, max_prefix> actual{};
    return expected.size() <= actual.size() && memory.read(address, actual.data(), expected.size()) &&
           std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
}
/** @brief Clear explicit ownership after the source is safe.
 * @param memory Policy. @param hook Inactive handle with unreachable trampoline.
 */
template<class M>
void clear(M& memory, Hook& hook) noexcept {
    if (hook.trampoline) memory.release(hook.trampoline, hook.trampoline_size);
    hook.trampoline = 0;
    hook.source = 0;
    hook.length = 0;
    hook.state = State::empty;
}
/** @brief Recover original code and protections, retaining poisoned state on any failure.
 * @param memory Policy. @param hook Handle with original bytes. @return Fully restored.
 */
template<class M>
bool rollback(M& memory, Hook& hook) noexcept {
    bool restored = memory.writable(hook.page);
    if (restored) restored = memory.write(hook.source, hook.original.data(), hook.length);
    memory.flush(hook.source, hook.length);
    const bool protected_again = memory.restore(hook.page);
    restored = restored && protected_again && matches(memory, hook.source, {hook.original.data(), hook.length});
    if (restored) clear(memory, hook);
    else hook.state = State::poisoned;
    return restored;
}
/** @brief Install a detour only under an externally established quiescence guarantee.
 * @param memory Memory/protection policy. @param hook Empty handle.
 * @param source RX function entry. @param destination RX replacement outside the source page.
 * @param expected Entire relocation-free prefix, 14..32 bytes. @param caller_quiescent Explicit caller guarantee.
 * @return Status; rollback_failed retains poisoned state and requires caller containment.
 */
template<class M>
Result install(M& memory, Hook& hook, std::uintptr_t source, std::uintptr_t destination,
               std::span<const unsigned char> expected, bool caller_quiescent) noexcept {
#if !defined(__x86_64__)
    (void)memory; (void)hook; (void)source; (void)destination; (void)expected; (void)caller_quiescent;
    return Result::unsupported;
#else
    if (!caller_quiescent || hook.state != State::empty || expected.size() < jump_size || expected.size() > max_prefix)
        return Result::invalid;
    if (!relocation_free(expected)) return Result::unsupported;
    Page source_page{}, destination_page{};
    if (!memory.inspect(source, expected.size(), source_page) || !memory.inspect(destination, 1, destination_page) ||
        source_page.address == destination_page.address) return Result::invalid;
    if (!matches(memory, source, expected)) return Result::mismatch;
    hook.source = source;
    hook.page = source_page;
    hook.length = expected.size();
    std::memcpy(hook.original.data(), expected.data(), expected.size());
    hook.patched.fill(0x90);
    jump(hook.patched.data(), destination);
    hook.trampoline_size = source_page.size;
    hook.trampoline = memory.allocate(hook.trampoline_size);
    if (!hook.trampoline) { clear(memory, hook); return Result::allocation_failed; }
    std::array<unsigned char, max_prefix + jump_size> relocated{};
    std::memcpy(relocated.data(), expected.data(), expected.size());
    jump(relocated.data() + expected.size(), source + expected.size());
    if (!memory.write(hook.trampoline, relocated.data(), expected.size() + jump_size) ||
        !memory.seal(hook.trampoline, hook.trampoline_size)) { clear(memory, hook); return Result::allocation_failed; }
    memory.flush(hook.trampoline, expected.size() + jump_size);
    if (!memory.writable(hook.page)) { clear(memory, hook); return Result::protection_failed; }
    if (!matches(memory, source, expected)) {
        if (!memory.restore(hook.page)) { hook.state = State::poisoned; return Result::rollback_failed; }
        clear(memory, hook);
        return Result::mismatch;
    }
    if (!memory.write(source, hook.patched.data(), hook.length))
        return rollback(memory, hook) ? Result::rolled_back : Result::rollback_failed;
    memory.flush(source, hook.length);
    if (!memory.restore(hook.page) || !matches(memory, source, {hook.patched.data(), hook.length}))
        return rollback(memory, hook) ? Result::rolled_back : Result::rollback_failed;
    hook.state = State::installed;
    return Result::ok;
#endif
}
/** @brief Remove an unchanged installed detour while its code and trampoline are quiescent.
 * @param memory Policy. @param hook Installed handle. @param caller_quiescent Explicit caller guarantee.
 * @return Status; mismatch never overwrites another writer's bytes.
 */
template<class M>
Result remove(M& memory, Hook& hook, bool caller_quiescent) noexcept {
    if (!caller_quiescent || hook.state != State::installed) return Result::invalid;
    if (!matches(memory, hook.source, {hook.patched.data(), hook.length})) return Result::mismatch;
    return rollback(memory, hook) ? Result::ok : Result::rollback_failed;
}
} // namespace capie::core::macos::patch
