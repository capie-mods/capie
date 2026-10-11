/**
 * @file memory.hpp
 * @brief Bounded, fault-tolerant reads of process memory.
 *
 * Every read CAPIE makes of game memory goes through this file. A read first asks
 * `VirtualQuery` whether the whole range is committed, readable and not a guard page,
 * then copies the bytes inside a structured-exception handler that catches access
 * violations and in-page errors. A failed read returns `false`; it never faults the
 * game.
 *
 * What it does not do: it does not cache region information between calls, it never
 * writes, and it does not keep pointers. Callers copy scalars out and drop the address.
 * The `VirtualQuery` check runs on every call on purpose: game objects can be freed
 * between two frames, and the check is what keeps a stale pointer from touching a guard
 * page (a guard-page fault is not caught by the handler below).
 *
 * Contracts: all functions are `noexcept` and callable from any thread. `rd()` is the
 * template the feature code uses with either `ProcessMemory` (in the game) or a fake
 * memory object (in offline tests), so the same guard chains run in both places.
 *
 * Risks: there is still a time-of-check/time-of-use gap between `VirtualQuery` and the
 * copy; the SEH handler covers access violations in that gap. SEH is used only in
 * functions without C++ objects that need unwinding.
 */
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace capie::core {

/// @brief Address-sized unsigned integer used for every engine address.
using uptr = std::uintptr_t;
/// @brief 64-bit unsigned integer used for engine sizes and counters.
using u64 = std::uint64_t;

/// @brief Largest representable address; used for overflow checks before `base + offset`.
inline constexpr uptr uptr_max = (std::numeric_limits<uptr>::max)();
/// @brief Mask selecting the basic page-protection value from `MEMORY_BASIC_INFORMATION::Protect`.
inline constexpr DWORD page_access_mask = 0xFF;

/**
 * @brief Checks that `[source, source + size)` is committed, readable and has no guard page.
 * @param source First byte of the range.
 * @param size Range length in bytes; zero is always readable.
 * @return `true` when every page in the range may be read.
 * @note Thread: any. Cost: one `VirtualQuery` per memory region spanned.
 */
[[nodiscard]] inline bool readable(const void* source, std::size_t size) noexcept {
    if (size == 0) {
        return true;
    }
    auto cursor = reinterpret_cast<uptr>(source);
    if (cursor == 0 || size > uptr_max - cursor) {
        return false;
    }
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) == 0 ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        const DWORD access = region.Protect & page_access_mask;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) {
            return false;
        }
        const auto start = reinterpret_cast<uptr>(region.BaseAddress);
        if (region.RegionSize > uptr_max - start) {
            return false;
        }
        const auto next = start + region.RegionSize;
        if (next <= cursor) {
            return false;
        }
        cursor = next < end ? next : end;
    }
    return true;
}

/**
 * @brief SEH filter that handles only memory faults.
 * @param code Exception code from `GetExceptionCode()`.
 * @return `EXCEPTION_EXECUTE_HANDLER` for access violations and in-page errors, otherwise
 *         `EXCEPTION_CONTINUE_SEARCH` so unrelated exceptions keep their normal handling.
 */
inline int memory_exception(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER
                                                                                 : EXCEPTION_CONTINUE_SEARCH;
}

/**
 * @brief Copies `size` bytes after a readability check, under an SEH guard.
 * @param target Destination buffer owned by the caller (at least `size` bytes).
 * @param source Engine address to read.
 * @param size Number of bytes.
 * @return `true` when the full range was copied; on `false` the target content is unspecified.
 * @note SEH is restricted to this POD helper; no C++ destructor is skipped.
 */
inline bool guarded_copy(void* target, const void* source, std::size_t size) noexcept {
    if (!readable(source, size)) {
        return false;
    }
    __try {
        if (size != 0) {
            std::memcpy(target, source, size);
        }
        return true;
    } __except (memory_exception(GetExceptionCode())) {
        return false;
    }
}

/**
 * @brief Reads one trivially copyable member at `object + offset`.
 * @tparam T Member type.
 * @param object Engine object address (may be null; then the read fails).
 * @param offset Byte offset of the member.
 * @param value Receives the copy on success.
 * @return `true` when the member was copied.
 */
template <class T> bool member(const void* object, std::size_t offset, T& value) noexcept {
    const auto address = reinterpret_cast<uptr>(object);
    if (address == 0 || offset > uptr_max - address) {
        return false;
    }
    return guarded_copy(&value, reinterpret_cast<const void*>(address + offset), sizeof(T));
}

/**
 * @brief Overflow-checked `base + offset` for addresses that are formed but not yet read.
 * @param base Base address.
 * @param offset Offset to add.
 * @param out Receives the sum on success.
 * @return `false` when the sum would wrap.
 */
inline bool add_offset(uptr base, uptr offset, uptr& out) noexcept {
    if (offset > uptr_max - base) {
        return false;
    }
    out = base + offset;
    return true;
}

/**
 * @brief Memory policy for the in-game build: every read is `guarded_copy`.
 *
 * Offline tests replace it with a fake object that has the same `read` signature, so the
 * guard chains in `caption` and `focus` are tested unchanged.
 */
struct ProcessMemory {
    /**
     * @brief Reads `size` bytes from `address`.
     * @param address Engine address.
     * @param out Destination buffer.
     * @param size Byte count.
     * @return `true` on a complete copy.
     */
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        return guarded_copy(out, reinterpret_cast<const void*>(address), size);
    }
};

/**
 * @brief Reads a `T` at `object + offset` through a memory policy.
 * @tparam T Value type.
 * @tparam M Memory policy with `bool read(uptr, void*, size_t) const`.
 * @param mem Memory policy.
 * @param object Object address; zero fails.
 * @param offset Member offset.
 * @param out Receives the value.
 * @return `true` on success.
 */
template <class T, class M> bool rd(const M& mem, uptr object, uptr offset, T& out) noexcept {
    if (object == 0 || offset > uptr_max - object) {
        return false;
    }
    return mem.read(object + offset, &out, sizeof(T));
}

}  // namespace capie::core
