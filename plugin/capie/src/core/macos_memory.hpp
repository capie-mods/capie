/**
 * @file macos_memory.hpp
 * @brief Bounded kernel-mediated reads of this process on macOS.
 *
 * The kernel validates source and destination mappings while copying. No user-space
 * dereference of the source and no process-wide signal handler are needed. A read
 * succeeds only when the kernel reports the full requested size. Failed reads may
 * modify part of the destination; callers must discard it on false. The destination
 * must be caller-owned storage of at least size bytes, as with the Windows policy.
 *
 * This standalone backend is not yet connected to CAPIE's runtime. It does not make
 * calls into game code fault-tolerant, guarantee an atomic snapshot of mutable game
 * objects, or authorize writes to game memory.
 */
#pragma once
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace capie::core::macos {

/// @brief Memory policy with the same read signature as core::ProcessMemory.
struct ProcessMemory {
    /**
     * @brief Copies a complete range from this process without directly dereferencing it.
     * @param address Source address in this process.
     * @param out Caller-owned writable destination buffer.
     * @param size Number of bytes; zero succeeds without inspecting either pointer.
     * @return True only on a complete copy; false on null, overflow or kernel failure.
     * @note Callable from any thread; one kernel read per nonempty valid range.
     */
    [[nodiscard]] static bool read(std::uintptr_t address, void* out, std::size_t size) noexcept {
        if (size == 0) {
            return true;
        }
        constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
        const auto destination = reinterpret_cast<std::uintptr_t>(out);
        if (address == 0 || destination == 0 || size > maximum - address || size > maximum - destination) {
            return false;
        }
        mach_vm_size_t copied = 0;
        const auto status = mach_vm_read_overwrite(mach_task_self(), static_cast<mach_vm_address_t>(address),
                                                   static_cast<mach_vm_size_t>(size),
                                                   static_cast<mach_vm_address_t>(destination), &copied);
        return status == KERN_SUCCESS && copied == size;
    }
};

}  // namespace capie::core::macos
