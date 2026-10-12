/**
 * @file log.hpp
 * @brief Editor log file, log channels and the debug ring dump.
 *
 * Channels. Every line belongs to one of three channels:
 *   - `error`     refusals and failures (guards that drop an action, failed prefill,
 *                 rejected caption precall, unverified focus release, init failure);
 *   - `lifecycle` state changes worth keeping in every build (init, bind reset, prefill
 *                 result, Apply transport summary, caption refresh result, focus/hide);
 *   - `debug`     high-volume or privacy-sensitive diagnostics (key log, key tick,
 *                 Execute observation, typed text and code points, debug requests).
 * The release flavor writes `error` and `lifecycle` only; `debug` call sites are compiled
 * out with `if constexpr`, so they cost nothing, not even string formatting. The debug
 * flavor (`CAPIE_DEBUG_DIAGNOSTICS=1`) writes all three channels with exactly the line
 * format of the accepted rev7 bridge, including the ring stamp suffix.
 *
 * I/O. A line is appended with one `WriteFile` under a short SRW lock. Lines are produced
 * only on transitions and events, never per frame in steady state, so the hot path does
 * no formatting and no I/O while nothing changes. Lines are written synchronously on
 * purpose: if the game crashes, the last line before the crash is on disk. (A buffered
 * writer thread was considered and rejected; see the stage-2 report, Performance.)
 *
 * State is process-global because hooks are free functions; it never holds engine
 * pointers. The ring and the request mailbox exist in both flavors (zero pages in
 * release, never touched).
 */
#pragma once
#include "trace_ring.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef CAPIE_DEBUG_DIAGNOSTICS
#define CAPIE_DEBUG_DIAGNOSTICS 0  // NOLINT(cppcoreguidelines-macro-usage): build flavor switch set by build.ps1
#endif

namespace capie::core::logging {

/// @brief `true` in the debug flavor: key log, key tick, Execute observer, ring and remote requests.
inline constexpr bool debug_diagnostics = CAPIE_DEBUG_DIAGNOSTICS != 0;

/// @brief Log channel of a line.
enum class Channel : unsigned {
    error,      ///< Refusal or failure.
    lifecycle,  ///< State change kept in every build.
    debug,      ///< Debug flavor only.
};

/**
 * @brief Whether lines of a channel are written in this build.
 * @param channel Channel.
 * @return `false` only for `debug` in the release flavor.
 */
constexpr bool enabled(Channel channel) noexcept {
    return channel != Channel::debug || debug_diagnostics;
}

/// @brief Size of a stamp suffix buffer.
inline constexpr std::size_t suffix_capacity = 160;
/// @brief Size of a dump header buffer.
inline constexpr std::size_t dump_header_capacity = 256;

/**
 * @brief Number of bytes `std::snprintf` actually stored in a buffer.
 * @param count Return value of `std::snprintf`.
 * @param capacity Buffer size.
 * @return 0 on error, otherwise `count` clamped to `capacity - 1` (truncated output).
 */
constexpr std::size_t stored_bytes(int count, std::size_t capacity) noexcept {
    if (count <= 0 || capacity == 0) {
        return 0;
    }
    const auto wanted = static_cast<std::size_t>(count);
    return wanted < capacity ? wanted : capacity - 1;
}

/// @brief Append-only log file next to the editor DLL; invalid until initialization.
inline HANDLE log_file = INVALID_HANDLE_VALUE;
/// @brief Serializes whole lines in the file (never held across a game call).
inline SRWLOCK debug_file_lock = SRWLOCK_INIT;
/// @brief Admits one ring dump at a time.
inline SRWLOCK debug_dump_lock = SRWLOCK_INIT;
/// @brief Serializes remote debug requests with ring arm/pause.
inline SRWLOCK debug_request_lock = SRWLOCK_INIT;
/// @brief Recent copied lines (debug flavor).
inline trace::Ring debug_ring;
/// @brief Remote request mailbox (debug flavor).
inline trace::Control debug_control;
/// @brief Dump staging buffer (debug flavor; one dump at a time).
inline trace::Dump debug_dump_buffer;

/**
 * @brief Raw append to the log file; no-op before initialization.
 * @param text Bytes.
 * @param size Byte count.
 * @pre Caller holds `debug_file_lock` when line atomicity matters.
 */
inline void write_bytes(const char* text, std::size_t size) noexcept {
    if (log_file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(log_file, text, static_cast<DWORD>(size), &written, nullptr);
}

/**
 * @brief Records a line in the ring only (no file write); debug flavor.
 * @param text NUL-terminated line.
 */
inline void remember_log(const char* text) noexcept {
    if constexpr (debug_diagnostics) {
        trace::Stamp stamp{};
        debug_ring.append(text, std::strlen(text), GetTickCount64(), GetCurrentThreadId(), stamp);
    }
}

/**
 * @brief Writes one line of known length to the file with a stamp suffix (and to the ring in the debug flavor).
 * @param text Line bytes; a trailing CR/LF is replaced by the suffix.
 * @param size Line length in bytes (no terminator needed).
 * @note Thread: any. The file lock is held only for the two `WriteFile` calls.
 */
inline void write_log(const char* text, std::size_t size) noexcept {
    char suffix[suffix_capacity]{};
    int count = 0;
    if constexpr (debug_diagnostics) {
        trace::Stamp stamp{};
        const bool remembered = debug_ring.append(text, size, GetTickCount64(), GetCurrentThreadId(), stamp);
        count = remembered
                    ? std::snprintf(suffix, sizeof(suffix),
                                    " trace_seq=%llu monotonic_ms=%llu trace_thread=%lu truncated=%u\r\n",
                                    static_cast<unsigned long long>(stamp.sequence),
                                    static_cast<unsigned long long>(stamp.ms), stamp.thread, stamp.truncated ? 1U : 0U)
                    : std::snprintf(suffix, sizeof(suffix), " trace_paused=1\r\n");
    } else {
        count = std::snprintf(suffix, sizeof(suffix), " monotonic_ms=%llu thread=%lu\r\n",
                              static_cast<unsigned long long>(GetTickCount64()), GetCurrentThreadId());
    }
    std::size_t end = size;
    // `size` is the caller's line length (strlen of a NUL-terminated snprintf buffer), so
    // text[end - 1] stays inside it; the analyzer does not model snprintf's terminator.
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
    while (end != 0 && (text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    AcquireSRWLockExclusive(&debug_file_lock);
    write_bytes(text, end);
    write_bytes(suffix, stored_bytes(count, sizeof(suffix)));
    ReleaseSRWLockExclusive(&debug_file_lock);
}

/**
 * @brief Writes one NUL-terminated line (see the sized overload).
 * @param text NUL-terminated line.
 */
inline void write_log(const char* text) noexcept {
    write_log(text, std::strlen(text));
}

/**
 * @brief Writes a line if its channel is enabled in this build.
 * @param channel Channel.
 * @param text NUL-terminated line.
 */
inline void log_line(Channel channel, const char* text) noexcept {
    if (enabled(channel)) {
        write_log(text);
    }
}

/**
 * @brief Dumps the ring to the file (debug flavor; UI thread, after the original Tick).
 * @param request Request serial shown in the header.
 * @note Never calls the game. A second concurrent dump is refused and logged.
 */
inline void dump_debug_ring(std::uint64_t request) noexcept {
    if constexpr (debug_diagnostics) {
        if (TryAcquireSRWLockExclusive(&debug_dump_lock) == 0) {
            write_log("RN_DEBUG stage=dump reason=dump_already_running; no retry\r\n");
            return;
        }
        debug_ring.copy(debug_dump_buffer);
        char header[dump_header_capacity]{};
        auto count = std::snprintf(header, sizeof(header),
                                   "RN_DEBUG_DUMP begin request=%llu count=%zu overwritten=%llu "
                                   "sequence_saturated=%u; copied history, no engine calls\r\n",
                                   static_cast<unsigned long long>(request), debug_dump_buffer.count,
                                   static_cast<unsigned long long>(debug_dump_buffer.overwritten),
                                   debug_dump_buffer.sequence_saturated ? 1U : 0U);
        AcquireSRWLockExclusive(&debug_file_lock);
        write_bytes(header, stored_bytes(count, sizeof(header)));
        for (std::size_t i = 0; i < debug_dump_buffer.count; ++i) {
            const auto& entry = debug_dump_buffer.entries[i];
            count = std::snprintf(
                header, sizeof(header), "RN_DEBUG_HISTORY seq=%llu monotonic_ms=%llu thread=%lu truncated=%u | ",
                static_cast<unsigned long long>(entry.stamp.sequence), static_cast<unsigned long long>(entry.stamp.ms),
                entry.stamp.thread, entry.stamp.truncated ? 1U : 0U);
            write_bytes(header, stored_bytes(count, sizeof(header)));
            write_bytes(entry.text, entry.size);
            if (entry.size == 0 || entry.text[entry.size - 1] != '\n') {
                write_bytes("\r\n", 2);
            }
        }
        constexpr char end_marker[] = "RN_DEBUG_DUMP end\r\n";
        write_bytes(end_marker, sizeof(end_marker) - 1);
        ReleaseSRWLockExclusive(&debug_file_lock);
        ReleaseSRWLockExclusive(&debug_dump_lock);
    } else {
        static_cast<void>(request);
    }
}

}  // namespace capie::core::logging
