/** @file macos_gui_probe.hpp
 * @brief Optional bounded forward-only GUI telemetry; never installs hooks or calls GUI helpers.
 * Pointer values are observations, not retained ownership or permission to dereference.
 */
#pragma once
#include "macos_game_layout.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
namespace capie::core::macos::gui_probe {
/// @brief Supplied by the hook bridge from thread_id() and pthread_main_np(), not inferred from a pointer.
struct Thread {
    unsigned long id{};
    bool main{};
};
/// @brief Recorded native hook.
enum class Kind { tick, dispatch };
/// @brief Position relative to the single forwarded original invocation.
enum class Phase { enter, leave };
/// @brief Independently copied GUI identity; complete does not imply an atomic engine snapshot.
struct Snapshot {
    std::uintptr_t window{}, model{}, root{}, context{}, model_vtable{}, root_vtable{};
    bool complete{}, context_matches{};
};
/// @brief Immutable event once published. Pair events by call, not adjacent array positions.
struct Event {
    std::size_t call{};
    Kind kind{};
    Phase phase{};
    Thread thread{};
    Snapshot snapshot{};
};
namespace detail {
/** @brief Read one pointer-sized field with checked address arithmetic. */
template <class Memory>
bool field(const Memory& memory, std::uintptr_t object, std::uintptr_t offset, std::uintptr_t& value) noexcept {
    constexpr auto max = std::numeric_limits<std::uintptr_t>::max();
    return object && object <= max - offset && sizeof(value) <= max - (object + offset) &&
           memory.read(object + offset, &value, sizeof(value));
}
/** @brief Capture only identity fields through the bounded memory policy. */
template <class Memory>
Snapshot capture(const Memory& memory, std::uintptr_t object, Kind kind) noexcept {
    Snapshot s{};
    if (kind == Kind::dispatch) {
        s.context = object;
        if (!field(memory, object, off::context_window, s.window))
            return s;
    } else
        s.window = object;
    constexpr auto max = std::numeric_limits<std::uintptr_t>::max();
    if (!s.window || s.window > max - off::window_context)
        return s;
    const auto embedded = s.window + off::window_context;
    if (kind == Kind::tick)
        s.context = embedded;
    std::uintptr_t back = 0;
    s.context_matches =
        s.context == embedded && field(memory, s.context, off::context_window, back) && back == s.window;
    s.complete = field(memory, s.window, off::window_model, s.model) &&
                 field(memory, s.window, off::window_root, s.root) && field(memory, s.model, 0, s.model_vtable) &&
                 field(memory, s.root, 0, s.root_vtable);
    return s;
}
}  // namespace detail
/** @brief Append-only fixed storage, safe for concurrent producers and snapshot readers.
 * Saturation drops observations, never forwarding. No allocation, locks, logging,
 * selector/text capture, game calls beyond the supplied original, or admission changes.
 * Keep the recorder alive while hooks/readers run. There is deliberately no reset.
 */
template <std::size_t Capacity = 1024>
class Recorder {
    struct Slot {
        Event value{};
        std::atomic<bool> ready{false};
    };
    std::array<Slot, Capacity> slots_{};
    std::atomic<std::size_t> next_{0}, calls_{0}, dropped_{0};
    void append(const Event& value) noexcept {
        auto index = next_.load(std::memory_order_relaxed);
        while (index < Capacity && !next_.compare_exchange_weak(index, index + 1, std::memory_order_relaxed)) {
        }
        if (index == Capacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        slots_[index].value = value;
        slots_[index].ready.store(true, std::memory_order_release);
    }

public:
    /** @brief Copy one published event; false for an absent or out-of-range slot. */
    [[nodiscard]] bool read(std::size_t index, Event& out) const noexcept {
        if (index >= Capacity || !slots_[index].ready.load(std::memory_order_acquire))
            return false;
        out = slots_[index].value;
        return true;
    }
    /// @brief Count dropped events after fixed storage fills.
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    /** @brief Preserve exact Tick arguments and invoke the supplied original exactly once.
     * Exceptions propagate unchanged; no leave event is emitted if the original throws.
     */
    template <class Memory, class Original>
    void tick(const Memory& memory, std::uintptr_t window, char force, Thread thread, Original&& original) {
        const auto call = calls_.fetch_add(1, std::memory_order_relaxed);
        append({call, Kind::tick, Phase::enter, thread, detail::capture(memory, window, Kind::tick)});
        std::forward<Original>(original)(reinterpret_cast<void*>(window), force);
        append({call, Kind::tick, Phase::leave, thread, detail::capture(memory, window, Kind::tick)});
    }
    /** @brief Preserve exact Dispatch arguments; selector contents are never read or recorded. */
    template <class Memory, class Original>
    void dispatch(const Memory& memory, std::uintptr_t context, void* selector, Thread thread, Original&& original) {
        const auto call = calls_.fetch_add(1, std::memory_order_relaxed);
        append({call, Kind::dispatch, Phase::enter, thread, detail::capture(memory, context, Kind::dispatch)});
        std::forward<Original>(original)(reinterpret_cast<void*>(context), selector);
        append({call, Kind::dispatch, Phase::leave, thread, detail::capture(memory, context, Kind::dispatch)});
    }
};
}  // namespace capie::core::macos::gui_probe
