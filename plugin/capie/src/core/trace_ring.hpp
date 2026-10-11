/**
 * @file trace_ring.hpp
 * @brief Debug-flavor diagnostics: a bounded ring of copied log lines and the scalar request queue.
 *
 * `Ring` keeps the last 256 log lines (copied text, at most 4 KiB each) so a developer can
 * dump recent history even after the file quota stopped writing. `Control` is the mailbox
 * between the debug controller's remote thread and the UI thread: the remote thread may
 * only post a scalar opcode; the UI thread picks it up after the original Tick returned.
 *
 * What it never holds: engine addresses, callbacks or borrowed strings. Everything is
 * copied in under a short SRW lock; file I/O happens outside the lock.
 *
 * Flavors: the release editor DLL does not use either class (no ring, no remote requests,
 * no `RNDebugRequest` export). The debug flavor uses both exactly as the accepted rev7
 * bridge did.
 *
 * Performance: `Control::has_work()` is a lock-free flag so the per-frame Tick can skip
 * the queue entirely while no request is pending. When it is set, `take()` runs exactly
 * as before; when it is clear, `take()` would have returned an empty `Work`.
 */
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace capie::core::trace {

/// @brief Number of lines the ring retains.
constexpr std::size_t ring_capacity = 256;
/// @brief Maximum stored bytes per line including the terminator; longer lines are truncated.
constexpr std::size_t line_capacity = 4096;
/// @brief Largest 64-bit value; sequence and overflow counters saturate at it.
constexpr std::uint64_t counter_max = (std::numeric_limits<std::uint64_t>::max)();

/// @brief Metadata stamped on every ring entry.
struct Stamp {
    std::uint64_t sequence{};  ///< Monotonic line number (saturating).
    std::uint64_t ms{};        ///< `GetTickCount64()` at append time.
    DWORD thread{};            ///< Appending thread id.
    bool truncated{};          ///< Text was cut to `line_capacity - 1` bytes.
};

/// @brief One retained line.
struct Entry {
    Stamp stamp{};                    ///< Stamp copied at append time.
    std::size_t size{};               ///< Stored text length.
    char text[line_capacity]{};       ///< Copied text (not NUL-terminated beyond `size`).
};

/// @brief Snapshot of the ring, oldest line first.
struct Dump {
    std::array<Entry, ring_capacity> entries{};  ///< Copied entries.
    std::size_t count{};                         ///< Valid entries.
    std::uint64_t overwritten{};                 ///< Lines dropped by wrap-around (saturating).
    bool sequence_saturated{};                   ///< Sequence counter reached its maximum.
};

/// @brief Fixed-size ring of copied log lines; all members are thread-safe.
class Ring {
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::array<Entry, ring_capacity> entries_{};
    std::size_t start_{};
    std::size_t count_{};
    std::uint64_t sequence_{};
    std::uint64_t overwritten_{};
    bool armed_ = true;

public:
    /**
     * @brief Enables or pauses appends (`pause`/`resume` requests).
     * @param enabled `true` to record.
     */
    void arm(bool enabled) noexcept {
        AcquireSRWLockExclusive(&lock_);
        armed_ = enabled;
        ReleaseSRWLockExclusive(&lock_);
    }

    /**
     * @brief Appends one line unless paused.
     * @param text Line bytes.
     * @param size Line length.
     * @param ms Timestamp.
     * @param thread Thread id.
     * @param stamp Receives the stamp assigned to the line.
     * @param force Append even while paused.
     * @return `false` when paused (nothing stored).
     */
    bool append(const char* text, std::size_t size, std::uint64_t ms, DWORD thread, Stamp& stamp,
                bool force = false) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if (!armed_ && !force) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        if (sequence_ != counter_max) {
            ++sequence_;
        }
        stamp = {sequence_, ms, thread, size >= line_capacity};
        std::size_t slot = (start_ + count_) % ring_capacity;
        if (count_ == ring_capacity) {
            slot = start_;
            start_ = (start_ + 1) % ring_capacity;
            if (overwritten_ != counter_max) {
                ++overwritten_;
            }
        } else {
            ++count_;
        }
        auto& entry = entries_[slot];
        entry = {};
        entry.stamp = stamp;
        entry.size = size < line_capacity ? size : line_capacity - 1;
        if (entry.size != 0) {
            std::memcpy(entry.text, text, entry.size);
        }
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    /**
     * @brief Copies the ring into `dump` (one bounded copy under the lock; no I/O inside).
     * @param dump Destination.
     */
    void copy(Dump& dump) noexcept {
        AcquireSRWLockShared(&lock_);
        dump.count = count_;
        dump.overwritten = overwritten_;
        dump.sequence_saturated = sequence_ == counter_max;
        for (std::size_t i = 0; i < count_; ++i) {
            dump.entries[i] = entries_[(start_ + i) % ring_capacity];
        }
        ReleaseSRWLockShared(&lock_);
    }

    Ring() = default;
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    Ring(Ring&&) = delete;
    Ring& operator=(Ring&&) = delete;
    ~Ring() = default;
};

/// @brief Scalar opcodes accepted from the debug controller.
enum class Operation : std::uintptr_t {
    snapshot = 1,  ///< Snapshot every own editor model on its next visible Tick, then dump.
    dump = 2,      ///< Dump the ring.
    pause = 3,     ///< Stop recording into the ring, then dump.
    resume = 4,    ///< Resume recording.
};
/// @brief Lowest valid opcode.
constexpr std::uintptr_t first_operation = 1;
/// @brief Highest valid opcode.
constexpr std::uintptr_t last_operation = 4;
/// @brief Bit mask of the three editor models a snapshot waits for.
constexpr unsigned all_models = 7;
/// @brief A snapshot request expires after this many milliseconds.
constexpr std::uint64_t snapshot_ttl_ms = 10000;

/// @brief Work for one Tick, computed by `Control::take`.
struct Work {
    std::uint64_t serial{};  ///< Latest request serial.
    Operation operation{};   ///< Latest request opcode.
    unsigned pending{};      ///< Models still waiting for a snapshot.
    unsigned expired{};      ///< Models whose snapshot expired in this take.
    bool changed{};          ///< A new request arrived since the last take.
    bool snapshot{};         ///< Take a snapshot of the ticking model now.
    bool dump{};             ///< Dump the ring now.
};

/// @brief Mailbox between the remote request thread and the UI thread.
class Control {
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::uint64_t serial_{};
    std::uint64_t reported_{};
    std::uint64_t deadline_{};
    Operation operation_{};
    unsigned pending_{};
    bool dump_{};
    std::atomic<bool> busy_{false};

public:
    /**
     * @brief Posts a request (remote thread). Reads no GUI state.
     * @param operation Opcode.
     * @param now `GetTickCount64()`.
     * @return `false` for an invalid opcode or an exhausted serial.
     */
    bool request(Operation operation, std::uint64_t now) noexcept {
        const auto code = static_cast<std::uintptr_t>(operation);
        if (code < first_operation || code > last_operation) {
            return false;
        }
        AcquireSRWLockExclusive(&lock_);
        if (serial_ == counter_max) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        ++serial_;
        operation_ = operation;
        if (operation == Operation::snapshot) {
            pending_ = all_models;
            deadline_ = now > counter_max - snapshot_ttl_ms ? counter_max : now + snapshot_ttl_ms;
            dump_ = true;
        } else if (operation == Operation::dump) {
            dump_ = true;
        } else if (operation == Operation::pause) {
            pending_ = 0;
            dump_ = true;
        }
        busy_.store(true, std::memory_order_release);
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    /**
     * @brief Lock-free test used by the per-frame Tick before `take()`.
     * @return `false` only when `take()` would return an empty `Work`.
     */
    [[nodiscard]] bool has_work() const noexcept {
        return busy_.load(std::memory_order_acquire);
    }

    /**
     * @brief Takes the work due for one ticking window (UI thread).
     * @param now `GetTickCount64()`.
     * @param model_bit Bit of the ticking own model, or 0 for any other window.
     * @return Work to do; `changed`/`expired` report the request once.
     */
    Work take(std::uint64_t now, unsigned model_bit) noexcept {
        Work work{};
        AcquireSRWLockExclusive(&lock_);
        work.serial = serial_;
        work.operation = operation_;
        work.changed = serial_ != reported_;
        reported_ = serial_;
        if (pending_ != 0 && now >= deadline_) {
            work.expired = pending_;
            pending_ = 0;
        }
        // Exactly one recognized model at a time; unrelated ticks use zero.
        if (model_bit != 0 && (model_bit & (model_bit - 1)) == 0 && (model_bit & all_models) == model_bit &&
            (pending_ & model_bit) != 0) {
            pending_ &= ~model_bit;
            work.snapshot = true;
        }
        work.pending = pending_;
        if (dump_ && pending_ == 0) {
            dump_ = false;
            work.dump = true;
        }
        busy_.store(pending_ != 0 || dump_, std::memory_order_release);
        ReleaseSRWLockExclusive(&lock_);
        return work;
    }

    Control() = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;
    Control(Control&&) = delete;
    Control& operator=(Control&&) = delete;
    ~Control() = default;
};

}  // namespace capie::core::trace
