/** @file platform.hpp
 * @brief Platform locks and scalar diagnostics; no game addresses or callbacks.
 */
#pragma once
#include <cstdint>
#if defined(__APPLE__)
#include <chrono>
#include <mutex>
#include <pthread.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace capie::core::platform {
#if defined(__APPLE__)
/// @brief Short nonrecursive lock; shared readers serialize on macOS.
using Lock = std::mutex;
inline void lock(Lock* value) noexcept { value->lock(); }
inline void unlock(Lock* value) noexcept { value->unlock(); }
inline void lock_shared(Lock* value) noexcept { value->lock(); }
inline void unlock_shared(Lock* value) noexcept { value->unlock(); }
inline bool try_lock(Lock* value) noexcept { return value->try_lock(); }
inline std::uint64_t monotonic_ms() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline unsigned long thread_id() noexcept {
    static_assert(sizeof(unsigned long) == sizeof(std::uint64_t));
    std::uint64_t id = 0;
    return pthread_threadid_np(nullptr, &id) == 0 ? static_cast<unsigned long>(id) : 0;
}
#else
/// @brief Preserve Windows SRW lock layout and operations for existing callers.
using Lock = SRWLOCK;
inline void lock(Lock* value) noexcept { AcquireSRWLockExclusive(value); }
inline void unlock(Lock* value) noexcept { ReleaseSRWLockExclusive(value); }
inline void lock_shared(Lock* value) noexcept { AcquireSRWLockShared(value); }
inline void unlock_shared(Lock* value) noexcept { ReleaseSRWLockShared(value); }
inline bool try_lock(Lock* value) noexcept { return TryAcquireSRWLockExclusive(value) != 0; }
inline std::uint64_t monotonic_ms() noexcept { return GetTickCount64(); }
inline unsigned long thread_id() noexcept { return GetCurrentThreadId(); }
#endif
}  // namespace capie::core::platform
