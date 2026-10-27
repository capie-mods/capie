/**
 * @file crash_guard.hpp
 * @brief Crash-loop breaker: a sentinel file that exists only while CAPIE is being loaded.
 *
 * Loading CAPIE (the launcher's two injector steps, the proxy's two `RNInitialize` calls)
 * is the one moment where CAPIE can take the game down with it. If that ever happens on a
 * player's machine, the next start must not do the same again, or the game would never
 * start. So the loader writes a small sentinel file right before the first step and deletes
 * it as soon as the steps returned with the game still alive. A sentinel that is still
 * there at the next start means the previous start ended during loading (crash, kill,
 * power loss): that start deletes it and runs without CAPIE, once, and says so. The start
 * after it tries again, so a single bad moment never disables CAPIE for good.
 *
 * The launcher and the proxy use different files (`capie-inject.pending`,
 * `proxy-init.pending`) in `%LOCALAPPDATA%\RenameAnything`. Every function here is
 * best-effort and never throws: a folder that cannot be written only means there is no
 * breaker, never that the game is held up.
 */
#pragma once
#include "win_handle.hpp"

#include <windows.h>

#include <string>
#include <string_view>

namespace capie::launcher {

/**
 * @brief Whether a sentinel file exists.
 * @param path Sentinel path (empty: breaker disabled).
 * @return `true` for an existing file.
 */
inline bool sentinel_present(const std::wstring& path) noexcept {
    if (path.empty()) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/**
 * @brief Writes the sentinel right before CAPIE is loaded.
 * @param path Sentinel path (empty: nothing is written).
 * @param what One line naming the loader and the game, for a person who finds the file.
 * @return `true` when the file was written and flushed.
 */
inline bool arm_sentinel(const std::wstring& path, std::string_view what) noexcept {
    if (path.empty()) {
        return false;
    }
    try {
        const Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
        const std::string text = "CAPIE loading started; pid=" + std::to_string(GetCurrentProcessId()) + " " +
                                 std::string(what) + "\r\n";
        DWORD written = 0;
        return file && WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0 &&
               written == text.size() && FlushFileBuffers(file.get()) != 0;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Deletes the sentinel after the loading steps returned with the game alive.
 * @param path Sentinel path (empty: nothing to do).
 */
inline void disarm_sentinel(const std::wstring& path) noexcept {
    if (!path.empty()) {
        DeleteFileW(path.c_str());
    }
}

/**
 * @brief At start: whether the previous start ended during loading; the sentinel is removed.
 * @param path Sentinel path (empty: breaker disabled).
 * @return `true` when a sentinel was found: this start must run without CAPIE.
 * @note The sentinel is deleted here, so only this one start is skipped.
 */
inline bool consume_sentinel(const std::wstring& path) noexcept {
    if (!sentinel_present(path)) {
        return false;
    }
    disarm_sentinel(path);
    return true;
}

}  // namespace capie::launcher
