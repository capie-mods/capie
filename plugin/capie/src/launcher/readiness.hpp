/**
 * @file readiness.hpp
 * @brief Deciding that the game has reached the main menu, the point where CAPIE was always loaded.
 *
 * Until now a person loaded CAPIE by hand once the main menu was on screen, and every
 * accepted in-game test started from that state. The launcher keeps that state instead of
 * guessing an earlier one, and only reads things the game produces anyway; nothing is sent
 * to the game. Three conditions must hold together:
 *
 *  1. The game process (the bound one, same PID and creation time) still runs, and owns a
 *     visible top-level window that answers a `WM_NULL` message within 5 s. A window on
 *     its own is not enough: HOI4 creates it at once and shows the loading screen in it.
 *  2. game.log in the player's Documents folder was written after the game process was
 *     created (so it is this session's log, not the previous one) and contains the
 *     loading-finished marker twice: the game's two history passes, the last thing it
 *     logs before the front end. The marker is a property of the pinned build.
 *  3. Both still hold after a quiet period of 15 s (`settle_ms`).
 *
 * If that does not happen within 15 minutes of the game's creation, CAPIE is not loaded
 * and the player is told; the game is never touched. A game started with a different
 * user folder (`-userdir`) writes its log elsewhere and therefore runs without CAPIE; that
 * is a known limit, listed for the live test, and errs on the safe side.
 *
 * `count_marker` and `evaluate` are pure so the tests can drive every combination.
 */
#pragma once
#include "config.hpp"
#include "process_tree.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <string>
#include <string_view>

namespace capie::launcher {

/**
 * @brief Counts non-overlapping occurrences of a marker.
 * @param text Text.
 * @param marker Marker (non-empty).
 * @return Number of occurrences.
 */
inline std::size_t count_marker(std::string_view text, std::string_view marker) noexcept {
    if (marker.empty()) {
        return 0;
    }
    std::size_t count = 0;
    for (std::size_t at = text.find(marker); at != std::string_view::npos; at = text.find(marker, at + marker.size())) {
        ++count;
    }
    return count;
}

/// @brief One observation of the game.
struct Observation {
    bool alive = false;         ///< bound process still runs
    bool window_ready = false;  ///< visible, responsive top-level window
    bool log_ready = false;     ///< fresh game.log with the marker count
};

/// @brief Result of the readiness wait.
enum class Readiness : std::uint8_t { ready, exited, timed_out };

/// @brief What the wait loop does next.
enum class Step : std::uint8_t { keep_waiting, start_settle, ready, exited, timed_out };

/**
 * @brief The readiness decision for one observation.
 * @param seen Observation.
 * @param settling Whether the settle period already started.
 * @param settle_elapsed Whether the settle period is over.
 * @param deadline_passed Whether the readiness deadline passed.
 * @return Next step.
 */
constexpr Step evaluate(const Observation& seen, bool settling, bool settle_elapsed, bool deadline_passed) noexcept {
    if (!seen.alive) {
        return Step::exited;
    }
    const bool conditions = seen.window_ready && seen.log_ready;
    if (conditions && settling && settle_elapsed) {
        return Step::ready;
    }
    if (deadline_passed) {
        return Step::timed_out;
    }
    if (conditions && !settling) {
        return Step::start_settle;
    }
    return Step::keep_waiting;
}

/// @brief EnumWindows context.
struct WindowSearch {
    DWORD pid = 0;         ///< process to match
    HWND found = nullptr;  ///< first visible, unowned top-level window
};

/**
 * @brief EnumWindows callback.
 * @param window Window.
 * @param context WindowSearch pointer.
 * @return FALSE to stop at the first match.
 */
inline BOOL CALLBACK match_window(HWND window, LPARAM context) {
    auto* search = reinterpret_cast<WindowSearch*>(context);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner == search->pid && IsWindowVisible(window) != 0 && GetWindow(window, GW_OWNER) == nullptr) {
        search->found = window;
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Whether a process shows a visible top-level window that answers messages.
 * @param pid Process.
 * @param timeout_ms Bound of the `WM_NULL` round trip.
 * @return `true` when such a window exists and answered in time.
 */
inline bool window_responsive(DWORD pid, DWORD timeout_ms) noexcept {
    WindowSearch search{.pid = pid};
    EnumWindows(match_window, reinterpret_cast<LPARAM>(&search));
    DWORD_PTR result = 0;
    return search.found != nullptr &&
           SendMessageTimeoutW(search.found, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout_ms, &result) != 0;
}

/**
 * @brief Whether game.log belongs to this session and shows that loading finished.
 * @param path game.log path.
 * @param game_created Creation time of the game process.
 * @return `true` when written after `game_created` and containing the marker `ready_marker_count` times.
 * @note Opened with full sharing and read only; the game keeps writing it undisturbed.
 */
inline bool game_log_ready(const std::wstring& path, ULONGLONG game_created) {
    const Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    FILETIME written{};
    LARGE_INTEGER size{};
    if (!file || GetFileTime(file.get(), nullptr, nullptr, &written) == 0 || ticks(written) < game_created ||
        GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart <= 0) {
        return false;
    }
    std::string content(std::min(static_cast<std::size_t>(size.QuadPart), game_log_read_limit), '\0');
    DWORD actual = 0;
    if (ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &actual, nullptr) == 0) {
        return false;
    }
    content.resize(actual);
    return count_marker(content, ready_log_marker) >= ready_marker_count;
}

/**
 * @brief Waits until the bound game reaches the main menu, exits, or the deadline passes.
 * @param game Bound game.
 * @param policy Game log path and timeouts.
 * @param log Log.
 * @return Outcome.
 */
inline Readiness wait_until_ready(const BoundGame& game, const Policy& policy, Log& log) {
    const auto& timing = policy.timeouts;
    constexpr ULONGLONG ticks_per_ms = 10000;
    const ULONGLONG deadline = game.stamp.created + (static_cast<ULONGLONG>(timing.readiness_ms) * ticks_per_ms);
    ULONGLONG settle_end = 0;
    bool settling = false;
    for (;;) {
        const Observation seen{.alive = running(game.process.get()),
                               .window_ready = window_responsive(game.stamp.pid, timing.window_probe_ms),
                               .log_ready = game_log_ready(policy.game_log, game.stamp.created)};
        const ULONGLONG now = now_ticks();
        switch (evaluate(seen, settling, settling && now >= settle_end, now >= deadline)) {
            case Step::ready:
                log.write(Level::info, L"game reached the main menu (window + game.log marker + settle)");
                return Readiness::ready;
            case Step::exited:
                return Readiness::exited;
            case Step::timed_out:
                return Readiness::timed_out;
            case Step::start_settle:
                settling = true;
                settle_end = now + (static_cast<ULONGLONG>(timing.settle_ms) * ticks_per_ms);
                log.write(Level::info, L"main menu conditions seen; settling");
                break;
            case Step::keep_waiting:
                if (settling && !(seen.window_ready && seen.log_ready)) {
                    settling = false;  // a condition dropped during the quiet period: start over
                }
                break;
        }
        WaitForSingleObject(game.process.get(), timing.poll_ms);
    }
}

}  // namespace capie::launcher
