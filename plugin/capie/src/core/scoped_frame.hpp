/**
 * @file scoped_frame.hpp
 * @brief Sets a thread-local frame pointer for one call and restores it, also on an exception.
 *
 * The country hooks publish a frame on the stack ("this thread is inside UpdateNames for this
 * country") while they call the game's original function, and nested hooks read it. Since the
 * hook bodies let the game's C++ exceptions pass through (scan N6), the old pointer must come
 * back in a destructor; otherwise a thrown exception would leave the thread pointing at a
 * stack frame that no longer exists.
 */
#pragma once

namespace capie::core {

/**
 * @brief Installs `frame` in `slot` and restores the previous value on scope exit.
 * @tparam T Frame type.
 */
template <class T>
class ScopedFrame {
public:
    /**
     * @brief Installs a frame.
     * @param slot Thread-local slot.
     * @param frame Frame that lives at least as long as this object.
     */
    ScopedFrame(T*& slot, T* frame) noexcept : slot_(slot), previous_(slot) { slot_ = frame; }
    ~ScopedFrame() { slot_ = previous_; }
    ScopedFrame(const ScopedFrame&) = delete;
    ScopedFrame& operator=(const ScopedFrame&) = delete;
    ScopedFrame(ScopedFrame&&) = delete;
    ScopedFrame& operator=(ScopedFrame&&) = delete;

private:
    T*& slot_;     ///< Slot restored on scope exit.
    T* previous_;  ///< Value restored.
};

}  // namespace capie::core
