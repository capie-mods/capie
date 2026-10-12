/**
 * @file win_handle.hpp
 * @brief Owning Windows handle, the launcher error type and UTF-8/UTF-16 conversion.
 *
 * `Handle` closes exactly one kernel handle and never closes `INVALID_HANDLE_VALUE`. The
 * launcher keeps process and file handles for a long time (the whole game session), so
 * ownership has to be explicit: a handle is either owned by one `Handle` or released with
 * `release()` to code that takes ownership.
 *
 * `LauncherError` carries an exit code and a UTF-8 message. The message lives inside
 * `std::runtime_error`, whose copy constructor cannot throw; this keeps the exception type
 * safe to copy (cert-err60-cpp). Code that shows the message converts it back to UTF-16.
 */
#pragma once
#include "config.hpp"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace capie::launcher {

/// @brief Bits in a DWORD (high half of a FILETIME or a 64-bit size).
inline constexpr unsigned dword_bits = 32U;

/// @brief Exclusive owner of one kernel handle.
class Handle {
public:
    Handle() noexcept = default;
    /**
     * @brief Takes ownership of a handle.
     * @param value Handle, `nullptr` or `INVALID_HANDLE_VALUE`.
     */
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    /// @brief Moves ownership.
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    /// @brief Moves ownership, closing the current handle first.
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.value_, nullptr));
        }
        return *this;
    }
    /**
     * @brief Closes the owned handle and takes a new one.
     * @param value New handle (default none).
     */
    void reset(HANDLE value = nullptr) noexcept {
        if (valid(value_)) {
            CloseHandle(value_);
        }
        value_ = value;
    }
    /// @brief The raw handle (still owned).
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    /// @brief Whether a usable handle is owned.
    explicit operator bool() const noexcept { return valid(value_); }

private:
    /**
     * @brief Whether a raw handle value refers to an object.
     * @param value Raw handle.
     * @return `false` for `nullptr` and `INVALID_HANDLE_VALUE`.
     */
    static bool valid(HANDLE value) noexcept { return value != nullptr && value != INVALID_HANDLE_VALUE; }

    HANDLE value_{nullptr};
};

/**
 * @brief Converts UTF-16 to UTF-8.
 * @param text UTF-16 text.
 * @return UTF-8 text; invalid sequences become U+FFFD (Windows behaviour).
 */
inline std::string to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string answer(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), answer.data(), size, nullptr,
                            nullptr);
    }
    return answer;
}

/**
 * @brief Converts UTF-8 to UTF-16.
 * @param text UTF-8 text.
 * @return UTF-16 text.
 */
inline std::wstring from_utf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring answer(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), answer.data(), size);
    }
    return answer;
}

/// @brief Error with a launcher exit code; the message is UTF-8.
class LauncherError : public std::runtime_error {
public:
    /**
     * @brief Creates the error.
     * @param code Exit code reported by the process.
     * @param message Human-readable reason.
     */
    LauncherError(ExitCode code, std::wstring_view message) : std::runtime_error(to_utf8(message)), code_(code) {}
    /// @brief Exit code.
    [[nodiscard]] ExitCode code() const noexcept { return code_; }
    /// @brief Message as UTF-16.
    [[nodiscard]] std::wstring message() const { return from_utf8(what()); }

private:
    ExitCode code_;
};

/**
 * @brief Formats a Windows error code for messages.
 * @param value Error code from `GetLastError`.
 * @return Text such as " (Windows 5)".
 */
inline std::wstring windows_error(DWORD value) {
    return L" (Windows " + std::to_wstring(value) + L")";
}

}  // namespace capie::launcher
