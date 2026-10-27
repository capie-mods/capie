/**
 * @file launcher_app.hpp
 * @brief One launcher run: start the game's command, find the game it starts, load CAPIE when safe.
 *
 * Order of a run (wrapper and shortcut mode are the same from here on):
 *
 *  1. Check what can be checked before anything starts: pin and verify the CAPIE bundle
 *     (and that its folder is writable, since the editor DLL writes its log there), pin and
 *     hash the game's hoi4.exe, and look for the crash-loop sentinel of the previous start
 *     (crash_guard.hpp). Problems here never stop the game, whatever they are (an
 *     unexpected exception included); they only switch CAPIE off for this run. The caller
 *     may also switch CAPIE off up front (`RunOptions::capie_off`: a program the launcher
 *     does not know, an internal error while building the policy).
 *  2. Start the wrapped program (Paradox Launcher or hoi4.exe) with its exact command
 *     line, the launcher's working folder and environment, inside a new job object. A
 *     program that requires elevation is started through the shell instead, CAPIE off.
 *  3. If CAPIE is off, release every pin (so Steam can update the game and the installer
 *     can update CAPIE meanwhile), tell the player why (non-blocking) and wait, without
 *     doing anything else, until everything started in step 2 has ended. Steam keeps
 *     counting play time because the launcher is still running.
 *  4. Otherwise look for hoi4.exe in the started tree, for at most 30 minutes (the player
 *     may take a while to pick a playset). Each game found is handled once: its image
 *     must be the pinned hoi4.exe, the playset it was started with (dlc_load.json) must
 *     enable the "CAPIE" switch mod (playset_gate.hpp; disabled or unreadable = vanilla, the
 *     game is never touched), it must reach the main menu, then the injector runs for
 *     `capie_core.dll` and `capie_editor.dll`, inside the crash-loop sentinel. The launcher
 *     then waits for that game to end. If the Paradox Launcher starts another game
 *     afterwards, the search starts over. An unexpected error in this step switches CAPIE
 *     off and the launcher keeps waiting for the tree.
 *  5. The run ends when nothing started in step 2 is running any more. The outcome goes
 *     to the log and to `last-status.txt`.
 *
 * Every decision is logged. Return value and `RunReport` describe the run for the tests;
 * Steam ignores the wrapper's exit code.
 */
#pragma once
#include "config.hpp"
#include "crash_guard.hpp"
#include "file_pin.hpp"
#include "injection.hpp"
#include "launcher_log.hpp"
#include "playset_gate.hpp"
#include "process_tree.hpp"
#include "readiness.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <exception>
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
    playset_off,    ///< the "CAPIE" mod is not enabled in the playset, or the playset is unreadable
};

/// @brief One handled game.
struct GameRecord {
    ProcessStamp stamp;                         ///< game identity
    bool via_job = false;                       ///< found through the job (else through ancestry)
    GameOutcome outcome = GameOutcome::failed;  ///< result
};

/// @brief Summary of a run.
struct RunReport {
    bool capie_enabled = false;      ///< bundle and game hash were fine, nothing switched CAPIE off
    bool discovery_expired = false;  ///< no game appeared in time
    bool shell_started = false;      ///< the program required elevation and was started through the shell
    bool crash_guard = false;        ///< the previous start's sentinel was found: CAPIE skipped once
    std::vector<GameRecord> games;   ///< handled games, in order
};

/// @brief A notice and its detail, shown after the start.
using PendingNotice = std::pair<Notice, std::wstring>;

/// @brief How the caller runs `run_wrapped` beyond the policy.
struct RunOptions {
    /// @brief CAPIE off from the start, with the notice that says why (nothing is pinned or checked).
    std::optional<PendingNotice> capie_off;
    /// @brief Process creation (tests replace it to simulate `ERROR_ELEVATION_REQUIRED`).
    CreateFn create = create_suspended;
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
        case GameOutcome::playset_off:
            return L"playset-off";
        case GameOutcome::failed:
            break;
    }
    return L"failed";
}

/// @brief Pins that make CAPIE possible for this run.
struct Prerequisites {
    std::optional<PinnedFile> game;  ///< pinned hoi4.exe with the pinned hash
    std::optional<Bundle> bundle;    ///< verified bundle
    bool crash_guard = false;        ///< the previous start ended during injection: CAPIE off once
};

/**
 * @brief Text of an exception that is not a LauncherError, for the log.
 * @param error Exception (may be null).
 * @return `what()` as UTF-16, or a fixed text.
 */
inline std::wstring unexpected_text(const std::exception_ptr& error) noexcept {
    try {
        if (error) {
            std::rethrow_exception(error);
        }
    } catch (const LauncherError& known) {
        return known.message();
    } catch (const std::exception& other) {
        try {
            return L"unexpected error: " + from_utf8(other.what());
        } catch (...) {
            return L"unexpected error";
        }
    } catch (...) {
        return L"unexpected error";
    }
    return L"unexpected error";
}

/**
 * @brief Step 1: pins and checks the bundle and the game, logging and collecting notices.
 * @param policy Policy.
 * @param game_exe hoi4.exe next to the wrapped program.
 * @param log Log.
 * @param pending Receives the notices to show after the game was started.
 * @return The pins that passed.
 * @note The game pin allows rename and delete (`PinShare::allow_replace`): Steam may update
 *       the game while the launcher waits; every injection step re-checks the identity.
 */
inline Prerequisites check_prerequisites(const Policy& policy, const std::wstring& game_exe, Log& log,
                                         std::vector<PendingNotice>& pending) {
    Prerequisites result;
    if (consume_sentinel(policy.crash_sentinel)) {
        log.write(Level::warning, L"crash guard: the previous start ended during CAPIE injection; CAPIE skipped once");
        pending.emplace_back(Notice::crash_guard, policy.crash_sentinel);
        result.crash_guard = true;
        return result;
    }
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
        auto pin = PinnedFile::open(game_exe, PinShare::allow_replace);
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
 * @brief Step 1 with a safety net: any exception switches CAPIE off instead of stopping the game.
 * @param policy Policy.
 * @param game_exe hoi4.exe next to the wrapped program.
 * @param log Log.
 * @param pending Receives the notices to show after the game was started.
 * @return The pins that passed; none after an unexpected error.
 */
inline Prerequisites checked_prerequisites(const Policy& policy, const std::wstring& game_exe, Log& log,
                                           std::vector<PendingNotice>& pending) noexcept {
    try {
        return check_prerequisites(policy, game_exe, log, pending);
    } catch (...) {
        const auto text = unexpected_text(std::current_exception());
        log.write(Level::error, L"prerequisite check failed: " + text + L"; CAPIE off, the game starts unchanged");
        try {
            pending.emplace_back(Notice::launcher_problem, text);
        } catch (...) {
            log.write(Level::error, L"notice could not be queued");
        }
        return {};
    }
}

/**
 * @brief Injects core and editor inside the crash-loop sentinel.
 * @param game Bound game.
 * @param prerequisites Pins (game and bundle present).
 * @param policy Policy.
 * @param log Log.
 * @param notifier Notifier.
 * @return `loaded` or `failed`.
 * @note The sentinel stays only when the game ended during injection; a refused step with the
 *       game still running is a clean outcome and removes it.
 */
inline GameOutcome inject(const BoundGame& game, const Prerequisites& prerequisites, const Policy& policy, Log& log,
                          Notifier& notifier) {
    if (!arm_sentinel(policy.crash_sentinel, "launcher injection into hoi4.exe pid=" +
                                                 std::to_string(game.stamp.pid))) {
        log.write(Level::warning, L"crash guard: sentinel not written; injecting without the breaker");
    }
    InjectionSession session(*prerequisites.game, *prerequisites.bundle, game);
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
        if (running(game.process.get())) {
            disarm_sentinel(policy.crash_sentinel);
        } else {
            log.write(Level::warning, L"crash guard: the game ended during injection; the next start skips CAPIE once");
        }
        session.guardian();
        return GameOutcome::failed;
    }
    disarm_sentinel(policy.crash_sentinel);
    return GameOutcome::loaded;
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
    const Handle image(CreateFileW(game.image.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
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
    const PlaysetVerdict playset = read_playset(policy.dlc_load, policy.capie_mod);
    log.write(playset.state == PlaysetState::enabled ? Level::info : Level::warning,
              std::wstring(L"playset ") + playset_state_name(playset.state) + L": " + playset.reason);
    if (playset.state != PlaysetState::enabled) {
        if (playset.state == PlaysetState::unknown) {
            notifier.notify(Notice::playset_unknown, playset.reason);  // fail-open: vanilla, never blocked
        }
        WaitForSingleObject(game.process.get(), INFINITE);
        return GameOutcome::playset_off;
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
    if (inject(game, prerequisites, policy, log, notifier) != GameOutcome::loaded) {
        return GameOutcome::failed;
    }
    log.write(Level::info, L"CAPIE loaded into pid=" + std::to_wstring(game.stamp.pid));
    WaitForSingleObject(game.process.get(), INFINITE);
    return GameOutcome::loaded;
}

/**
 * @brief Status-file outcome of a finished run.
 * @param report Run summary.
 * @param latest_notice Text of the latest notice (may be empty).
 * @return Outcome tag and one sentence for the player.
 */
inline std::pair<std::wstring, std::wstring> run_status(const RunReport& report, const std::wstring& latest_notice) {
    for (const auto& game : report.games) {
        if (game.outcome == GameOutcome::loaded) {
            return {L"capie-loaded", L"CAPIE was loaded into hoi4.exe pid=" + std::to_wstring(game.stamp.pid) + L"."};
        }
    }
    if (!latest_notice.empty()) {
        return {L"game-without-capie", latest_notice};
    }
    for (const auto& game : report.games) {
        if (game.outcome == GameOutcome::playset_off) {
            return {L"game-without-capie", L"The \"CAPIE\" mod is not enabled in the playset; the game ran without "
                                           L"CAPIE."};
        }
        if (game.outcome == GameOutcome::exited_early) {
            return {L"game-without-capie", L"The game was closed before the main menu; CAPIE was not loaded."};
        }
    }
    return {L"no-game", L"No game was started from the Paradox Launcher; CAPIE was not loaded."};
}

/**
 * @brief Runs the wrapped command and serves its games until the whole tree has ended.
 * @param policy Policy.
 * @param spec Program to start.
 * @param game_exe hoi4.exe in the program's folder (empty: CAPIE off).
 * @param log Log.
 * @param notifier Notifier.
 * @param options CAPIE switched off up front, process creation.
 * @return Summary.
 * @throws LauncherError (game_start) when the program cannot start; nothing else stops the run.
 */
inline RunReport run_wrapped(const Policy& policy, const LaunchSpec& spec, const std::wstring& game_exe, Log& log,
                             Notifier& notifier, const RunOptions& options = {}) {
    std::vector<PendingNotice> pending;
    Prerequisites prerequisites;
    if (options.capie_off) {
        log.write(Level::warning, L"CAPIE off for this run; the program starts unchanged");
        pending.push_back(*options.capie_off);
    } else if (game_exe.empty()) {
        log.write(Level::warning, L"no hoi4.exe known for this run; CAPIE off");
    } else {
        prerequisites = checked_prerequisites(policy, game_exe, log, pending);
    }
    RunReport report;
    report.crash_guard = prerequisites.crash_guard;
    report.capie_enabled = prerequisites.game.has_value() && prerequisites.bundle.has_value();
    const ProcessTree tree = ProcessTree::start(spec, log, options.create);
    if (tree.shell_started()) {
        report.shell_started = true;
        report.capie_enabled = false;
        pending.emplace_back(Notice::elevated_start, spec.application);
    }
    if (!report.capie_enabled) {
        prerequisites = {};  // nothing to protect: Steam may update the game, the installer CAPIE
    }
    log.status(L"game-started", report.capie_enabled ? L"The game was started; CAPIE loads at the main menu when the "
                                                       L"\"CAPIE\" mod is enabled."
                                                     : L"The game was started without CAPIE.");
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
            try {
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
            } catch (...) {
                report.capie_enabled = false;  // the game keeps running; the launcher only waits from here on
                next.reset();
                const auto text = unexpected_text(std::current_exception());
                log.write(Level::error, L"CAPIE stopped after an internal error: " + text);
                notifier.notify(Notice::launcher_problem, text);
            }
        }
        Sleep(policy.timeouts.poll_ms);
    }
    const auto [outcome, message] = run_status(report, notifier.last_text());
    log.status(outcome, message);
    log.write(Level::info, L"started tree ended; launcher exits");
    return report;
}

}  // namespace capie::launcher
