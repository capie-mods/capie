/**
 * @file launcher_app.hpp
 * @brief One launcher run: start the game's command, find the game it starts, load CAPIE when safe.
 *
 * Order of a run (wrapper and shortcut mode are the same from here on):
 *
 *  1. Check what can be checked before anything starts: pin and verify the CAPIE bundle
 *     (and that its folder is writable, since the editor DLL writes its log there), pin and
 *     hash the game's hoi4.exe. Problems here never stop the game; they only switch CAPIE
 *     off for this run.
 *  2. Start the wrapped program (Paradox Launcher or hoi4.exe) with its exact command
 *     line, the launcher's working folder and environment, inside a new job object.
 *  3. If CAPIE is off, tell the player why (non-blocking) and wait, without doing anything
 *     else, until everything started in step 2 has ended. Steam keeps counting play time
 *     because the launcher is still running.
 *  4. Otherwise look for hoi4.exe in the started tree, for at most 30 minutes (the player
 *     may take a while to pick a playset). Each game found is handled once: its image
 *     must be the pinned hoi4.exe, it must reach the main menu, then the injector runs for
 *     `capie_core.dll` and `capie_editor.dll`. The launcher then waits for that game to
 *     end. If the Paradox Launcher starts another game afterwards, the search starts over.
 *  5. The run ends when nothing started in step 2 is running any more.
 *
 * Every decision is logged. Return value and `RunReport` describe the run for the tests;
 * Steam ignores the wrapper's exit code.
 */
#pragma once
#include "config.hpp"
#include "file_pin.hpp"
#include "injection.hpp"
#include "launcher_log.hpp"
#include "process_tree.hpp"
#include "readiness.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace capie::launcher {

/// @brief What happened to one game process.
enum class GameOutcome : std::uint8_t {
    loaded,         ///< both injector steps succeeded
    foreign_image,  ///< the process image is not the pinned hoi4.exe
    exited_early,   ///< the game ended before the main menu
    not_ready,      ///< no main menu within the deadline
    failed,         ///< an injector step failed (guardian ran)
};

/// @brief One handled game.
struct GameRecord {
    ProcessStamp stamp;                         ///< game identity
    bool via_job = false;                       ///< found through the job (else through ancestry)
    GameOutcome outcome = GameOutcome::failed;  ///< result
};

/// @brief Summary of a run.
struct RunReport {
    bool capie_enabled = false;      ///< bundle and game hash were fine
    bool discovery_expired = false;  ///< no game appeared in time
    std::vector<GameRecord> games;   ///< handled games, in order
};

/**
 * @brief Text of a game outcome for the log.
 * @param outcome Outcome.
 * @return Short tag.
 */
constexpr const wchar_t* outcome_name(GameOutcome outcome) noexcept {
    switch (outcome) {
        case GameOutcome::loaded:
            return L"loaded";
        case GameOutcome::foreign_image:
            return L"foreign-image";
        case GameOutcome::exited_early:
            return L"exited-early";
        case GameOutcome::not_ready:
            return L"not-ready";
        case GameOutcome::failed:
            break;
    }
    return L"failed";
}

/// @brief Pins that make CAPIE possible for this run.
struct Prerequisites {
    std::optional<PinnedFile> game;  ///< pinned hoi4.exe with the pinned hash
    std::optional<Bundle> bundle;    ///< verified bundle
};

/**
 * @brief Step 1: pins and checks the bundle and the game, logging and collecting notices.
 * @param policy Policy.
 * @param game_exe hoi4.exe next to the wrapped program.
 * @param log Log.
 * @param pending Receives the notices to show after the game was started.
 * @return The pins that passed.
 */
inline Prerequisites check_prerequisites(const Policy& policy, const std::wstring& game_exe, Log& log,
                                         std::vector<std::pair<Notice, std::wstring>>& pending) {
    Prerequisites result;
    try {
        result.bundle = verify_bundle(policy);
        if (!runtime_writable(policy)) {
            result.bundle.reset();
            throw LauncherError(ExitCode::invalid_bundle, L"runtime folder is not writable");
        }
    } catch (const LauncherError& error) {
        log.write(Level::warning, L"bundle check failed: " + error.message());
        pending.emplace_back(Notice::bundle_invalid, error.message());
    }
    try {
        auto pin = PinnedFile::open(game_exe);
        const bool known = pin.digest == policy.game_sha256 && pe_x64(pin.file.get(), false);
        log.write(known ? Level::info : Level::warning, L"game " + pin.path + L" sha256=" + from_utf8(pin.digest) +
                                                            (known ? L" (pinned build)" : L" (unknown build)"));
        if (known) {
            result.game = std::move(pin);
        } else {
            pending.emplace_back(Notice::unknown_game_build, L"SHA-256 " + from_utf8(pin.digest));
        }
    } catch (const LauncherError& error) {
        log.write(Level::warning, L"game check failed: " + error.message());
        pending.emplace_back(Notice::unknown_game_build, error.message());
    }
    return result;
}

/**
 * @brief Step 4 for one game: image check, readiness, injection, wait for the end.
 * @param game Bound game (stays owned by the caller).
 * @param prerequisites Pins (game and bundle present).
 * @param policy Policy.
 * @param log Log.
 * @param notifier Notifier.
 * @return Outcome.
 */
inline GameOutcome handle_game(const BoundGame& game, const Prerequisites& prerequisites, const Policy& policy,
                               Log& log, Notifier& notifier) {
    const auto& game_pin = *prerequisites.game;
    const Handle image(CreateFileW(game.image.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
    bool same_file = false;
    try {
        same_file = image && file_id(image.get()) == game_pin.identity;
    } catch (const LauncherError&) {
        same_file = false;
    }
    if (!same_file) {
        notifier.notify(Notice::foreign_game, game.image);
        WaitForSingleObject(game.process.get(), INFINITE);
        return GameOutcome::foreign_image;
    }
    const Readiness readiness = wait_until_ready(game, policy, log);
    if (readiness != Readiness::ready) {
        if (readiness == Readiness::timed_out) {
            notifier.notify(Notice::not_ready, {});
            WaitForSingleObject(game.process.get(), INFINITE);
            return GameOutcome::not_ready;
        }
        return GameOutcome::exited_early;
    }
    InjectionSession session(game_pin, *prerequisites.bundle, game);
    try {
        for (std::size_t step = 0; step < injection_order.size(); ++step) {
            const auto result = session.run_injector(step, policy.timeouts.helper_ms);
            log.write(Level::info, policy.bundle.at(injection_order.at(step)).name + L": " + describe(result));
            if (!may_continue(result, session.intact())) {
                throw LauncherError(ExitCode::helper_failure, describe(result));
            }
        }
    } catch (const LauncherError& error) {
        notifier.notify(Notice::capie_failed, error.message());
        session.guardian();
        return GameOutcome::failed;
    }
    log.write(Level::info, L"CAPIE loaded into pid=" + std::to_wstring(game.stamp.pid));
    WaitForSingleObject(game.process.get(), INFINITE);
    return GameOutcome::loaded;
}

/**
 * @brief Runs the wrapped command and serves its games until the whole tree has ended.
 * @param policy Policy.
 * @param spec Program to start.
 * @param game_exe hoi4.exe in the program's folder.
 * @param log Log.
 * @param notifier Notifier.
 * @return Summary.
 * @throws LauncherError (game_start) when the program cannot start.
 */
inline RunReport run_wrapped(const Policy& policy, const LaunchSpec& spec, const std::wstring& game_exe, Log& log,
                             Notifier& notifier) {
    std::vector<std::pair<Notice, std::wstring>> pending;
    const Prerequisites prerequisites = check_prerequisites(policy, game_exe, log, pending);
    RunReport report;
    report.capie_enabled = prerequisites.game.has_value() && prerequisites.bundle.has_value();
    const ProcessTree tree = ProcessTree::start(spec, log);
    for (const auto& [notice, detail] : pending) {
        notifier.notify(notice, detail);  // after the start: the game is never held up by a dialog
    }
    std::vector<ProcessStamp> seen;
    std::optional<BoundGame> next;
    constexpr ULONGLONG ticks_per_ms = 10000;
    const auto discovery = static_cast<ULONGLONG>(policy.timeouts.discovery_ms) * ticks_per_ms;
    ULONGLONG deadline = tree.started() + discovery;
    while (tree.active() || next) {
        if (report.capie_enabled && !report.discovery_expired) {
            auto game = next ? std::exchange(next, std::nullopt) : find_game(tree, seen);
            if (game) {
                seen.push_back(game->stamp);
                GameRecord record{.stamp = game->stamp, .via_job = game->via_job};
                log.write(Level::info, L"game pid=" + std::to_wstring(game->stamp.pid) + L" bound via " +
                                           (game->via_job ? L"job" : L"parent chain") + L": " + game->image);
                record.outcome = handle_game(*game, prerequisites, policy, log, notifier);
                log.write(Level::info, L"game pid=" + std::to_wstring(record.stamp.pid) + L" ended: " +
                                           outcome_name(record.outcome));
                report.games.push_back(record);
                const ULONGLONG ended = exit_time(game->process.get()).value_or(now_ticks());
                while (auto other = find_game(tree, seen)) {
                    if (other->stamp.created >= ended) {
                        next = std::move(other);  // started after the game ended: handled like the first
                        break;
                    }
                    seen.push_back(other->stamp);  // started while the first ran: never touched
                    log.write(Level::warning, L"ignoring game pid=" + std::to_wstring(other->stamp.pid) +
                                                  L" that started during another game");
                }
                deadline = now_ticks() + discovery;
                continue;
            }
            if (now_ticks() >= deadline) {
                report.discovery_expired = true;
                log.write(Level::warning, L"no game appeared in the started tree in time; CAPIE stays off");
            }
        }
        Sleep(policy.timeouts.poll_ms);
    }
    log.write(Level::info, L"started tree ended; launcher exits");
    return report;
}

}  // namespace capie::launcher
