/**
 * @file launcher_main.cpp
 * @brief `RenameAnythingLauncher.exe`: entry point, command-line modes and the production policy.
 *
 * Modes:
 *
 *  - Wrapper (Steam launch option `"...\RenameAnythingLauncher.exe" %command%`, or the
 *    bootstrap's shortcut with `"<game>\dowser.exe"` as its argument): everything after
 *    the launcher options is the game's own command. It is started with its exact
 *    arguments, the launcher's working folder and environment. CAPIE can only load when
 *    the program is the game's `dowser.exe` or `hoi4.exe`.
 *  - Shortcut / double-click (no arguments, or `--game ABS`): the launcher finds the game
 *    (remembered path, Steam registry entries, Steam libraries, file dialog) and starts its
 *    Paradox Launcher in the game folder, as Steam would.
 *  - `--game ABS --verify-only`: checks the bundle and the game hash and exits; no process,
 *    no dialog, no file written.
 *  - `--help`.
 *
 * `--no-dialogs` (before the program) sends notices to the log only.
 *
 * The game must always start, with or without CAPIE. In wrapper mode only a missing
 * program (or one outside any game folder, which is not a game command at all) is refused
 * before anything starts. Everything else that goes wrong before the start (another
 * program name in the game folder, an internal error while building the policy or checking
 * the files, a program that needs elevation) starts Steam's command unchanged with CAPIE
 * off and tells the player why. The outcome of every run is written to `last-status.txt`
 * next to `launcher.log`.
 *
 * The production policy pins the game hash from `core/game_build.hpp` and the bundle from
 * `launcher_manifest.generated.hpp`, which the build writes from the reviewed CAPIE
 * binaries. There is no test hook in this file: tests drive `launcher_app.hpp` with their
 * own policy. The launcher has no network code and runs as the invoking user
 * (`asInvoker` manifest).
 */
#include "../core/build_identity.hpp"
#include "command_line.hpp"
#include "config.hpp"
#include "file_pin.hpp"
#include "game_location.hpp"
#include "launcher_app.hpp"
#include "launcher_log.hpp"
#include "launcher_manifest.generated.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using capie::launcher::ExitCode;
using capie::launcher::LauncherError;

/// @brief Usage text printed by --help.
constexpr wchar_t usage_text[] =
    L"Rename Anything launcher for CAPIE\n"
    L"Steam launch option:  \"...\\RenameAnythingLauncher.exe\" %command%\n"
    L"Shortcut:             RenameAnythingLauncher.exe \"<game>\\dowser.exe\"\n"
    L"Double-click:         finds the game and starts its Paradox Launcher\n"
    L"--game ABS --verify-only   check bundle and game hash only (no process, no writes)\n"
    L"--no-dialogs               notices go to %LOCALAPPDATA%\\RenameAnything\\launcher.log only\n"
    L"CAPIE is loaded only into hoi4.exe started by this launcher's own process tree, and only\n"
    L"when the \"CAPIE\" mod is enabled in the playset the game was started with.";

/**
 * @brief Writes a line to stdout or stderr as UTF-8 (console use only).
 * @param text Text.
 * @param error Use stderr.
 */
void print_line(std::wstring_view text, bool error) noexcept {
    void* const destination = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (destination == nullptr || destination == INVALID_HANDLE_VALUE) {
        return;
    }
    try {
        const std::string bytes = capie::launcher::to_utf8(text) + "\n";
        DWORD written = 0;
        WriteFile(destination, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    } catch (...) {
        return;
    }
}

/**
 * @brief Folder of this executable (may be the bootstrap's `current\` junction).
 * @return Folder path.
 * @throws LauncherError when it cannot be read.
 */
std::wstring own_folder() {
    std::vector<wchar_t> buffer(capie::launcher::long_path_chars);
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) {
        throw LauncherError(ExitCode::unexpected, L"Cannot read the launcher's own path.");
    }
    return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path().wstring();
}

/**
 * @brief The production policy.
 * @param app_data The launcher's data folder (holds the crash-loop sentinel); empty disables the breaker.
 * @return Pinned game hash, bundle next to the launcher, game.log and dlc_load.json in Documents,
 *         the CAPIE switch mod identity, the crash-loop sentinel, default timeouts.
 */
capie::launcher::Policy production_policy(const std::wstring& app_data) {
    capie::launcher::Policy policy;
    policy.game_sha256 = capie::core::game_exe_sha256;
    policy.bundle_root = own_folder();
    for (std::size_t i = 0; i < policy.bundle.size(); ++i) {
        const auto& entry = capie::launcher::manifest::bundle_files.at(i);
        policy.bundle.at(i) = {.name = entry.name, .sha256 = entry.sha256, .dll = entry.dll};
    }
    policy.game_log = capie::launcher::default_game_log();
    policy.dlc_load = capie::launcher::default_dlc_load();
    policy.capie_mod = capie::launcher::capie_mod_identity();
    if (!app_data.empty()) {
        policy.crash_sentinel = app_data + L"\\" + capie::launcher::inject_sentinel_name;
    }
    return policy;
}

/**
 * @brief --verify-only: bundle and game checks, nothing else.
 * @param game Absolute hoi4.exe path.
 * @return Exit code.
 */
ExitCode verify_only(const std::wstring& game) {
    const auto policy = production_policy({});
    const auto bundle = capie::launcher::verify_bundle(policy);
    if (!capie::launcher::ends_with_nocase(game, std::wstring(L"\\") + capie::launcher::game_exe_name)) {
        throw LauncherError(ExitCode::unsupported_game, L"--game must name hoi4.exe.");
    }
    const auto pin = capie::launcher::verify_file(game, policy.game_sha256, false, ExitCode::unsupported_game);
    print_line(
        L"Verification passed: pinned hoi4.exe and the three CAPIE bundle files. Nothing was started or "
        L"written.",
        false);
    return ExitCode::ok;
}

/**
 * @brief Finds the game for the shortcut mode.
 * @param explicit_game `--game` value (may be empty).
 * @return hoi4.exe path, or empty when the player cancelled the dialog.
 */
std::wstring locate_game(const std::wstring& explicit_game) {
    using capie::launcher::file_exists;
    if (!explicit_game.empty()) {
        return explicit_game;
    }
    if (auto remembered = capie::launcher::remembered_game(); !remembered.empty() && file_exists(remembered)) {
        return remembered;
    }
    if (auto found = capie::launcher::find_game_through_steam()) {
        return *found;
    }
    return capie::launcher::choose_game();
}

/**
 * @brief The launcher's data folder, or empty when it cannot be created (no log, no breaker; the game still starts).
 * @return `%LOCALAPPDATA%\RenameAnything` or empty.
 */
std::wstring data_folder_or_empty() noexcept {
    try {
        return capie::launcher::app_data_folder(true);
    } catch (...) {
        return {};
    }
}

/**
 * @brief Text of the current exception for the log.
 * @return Message.
 */
std::wstring current_error_text() noexcept {
    return capie::launcher::unexpected_text(std::current_exception());
}

/**
 * @brief Wrapper and shortcut modes.
 * @param invocation Parsed command line.
 * @param raw Raw command line.
 * @return Exit code.
 */
ExitCode launch(capie::launcher::Invocation invocation, std::wstring_view raw) {
    namespace cl = capie::launcher;
    const std::wstring app_data = data_folder_or_empty();
    cl::Log log(app_data.empty() ? std::wstring{} : app_data + L"\\" + cl::log_file_name);
    cl::Notifier notifier(log, !invocation.options.no_dialogs);
    log.write(cl::Level::info, L"launcher start: " + std::wstring(raw));
    cl::LaunchSpec spec;
    std::wstring game;
    cl::RunOptions options;
    try {
        if (invocation.wrapped) {
            auto& wrapped = *invocation.wrapped;
            try {
                cl::resolve_program(raw, wrapped, [](const std::wstring& path) { return cl::file_exists(path); });
            } catch (...) {
                log.write(cl::Level::warning, L"program resolution failed; the program token is used as written");
            }
            const auto program = cl::classify_wrapped_program(wrapped.program);  // the only refusal
            try {
                spec = {.application = wrapped.program,
                        .command_line = cl::wrapped_command_line(wrapped.program, wrapped.tail),
                        .working_directory = {}};
                if (program.kind == cl::ProgramKind::game) {
                    game = program.game;
                } else {
                    log.write(cl::Level::warning, L"unknown program in the game folder: " + wrapped.program);
                    options.capie_off = cl::PendingNotice{cl::Notice::unknown_program, wrapped.program};
                }
            } catch (...) {
                // Last resort: Steam's own command, character for character, CAPIE off.
                const auto text = current_error_text();
                log.write(cl::Level::error, L"command rebuild failed: " + text + L"; starting Steam's command as is");
                game.clear();
                spec = {.application = wrapped.program,
                        .command_line = std::wstring(raw.substr(wrapped.begin)),
                        .working_directory = {}};
                options.capie_off = cl::PendingNotice{cl::Notice::launcher_problem, text};
            }
        } else {
            game = locate_game(invocation.options.game);
            if (game.empty()) {
                log.write(cl::Level::info, L"no game chosen; nothing started");
                return ExitCode::ok;
            }
            const auto program = cl::default_program(game);
            game = cl::validate_wrapped_program(program);
            cl::remember_game(game);
            spec = {.application = program,
                    .command_line = cl::wrapped_command_line(program, {}),
                    .working_directory = cl::parent_folder(program)};
        }
    } catch (const LauncherError& error) {
        notifier.notify_blocking(cl::Notice::invalid_command, error.message());
        log.status(L"not-started", notifier.last_text());
        return error.code();
    }
    cl::Policy policy;
    if (!options.capie_off) {
        try {
            policy = production_policy(app_data);
        } catch (...) {
            const auto text = current_error_text();
            log.write(cl::Level::error, L"policy failed: " + text);
            options.capie_off = cl::PendingNotice{cl::Notice::launcher_problem, text};
        }
    }
    try {
        const auto report = cl::run_wrapped(policy, spec, game, log, notifier, options);
        log.write(cl::Level::info, L"games handled: " + std::to_wstring(report.games.size()));
        return ExitCode::ok;
    } catch (const LauncherError& error) {
        notifier.notify_blocking(
            error.code() == ExitCode::game_start ? cl::Notice::start_failed : cl::Notice::capie_failed,
            error.message());
        log.status(error.code() == ExitCode::game_start ? L"not-started" : L"failed", notifier.last_text());
        return error.code();
    }
}

/**
 * @brief Shows an error that ended the launcher before any game started.
 * @param error Error.
 * @param raw Raw command line.
 * @param console Console mode (--help/--verify-only): stderr instead of a dialog.
 * @return The error's exit code.
 */
int report(const LauncherError& error, std::wstring_view raw, bool console) noexcept {
    try {
        if (console || raw.find(capie::launcher::option_verify) != std::wstring_view::npos) {
            print_line(error.message(), true);
        } else if (raw.find(capie::launcher::option_no_dialogs) == std::wstring_view::npos) {
            MessageBoxW(nullptr, error.message().c_str(), capie::launcher::dialog_title,
                        capie::launcher::error_box_style);
        }
    } catch (...) {
        print_line(L"Launcher error.", true);
    }
    return static_cast<int>(error.code());
}

/**
 * @brief Dispatches the modes; every error becomes an exit code.
 * @return Exit code.
 */
int run() noexcept {
    const std::wstring_view raw = GetCommandLineW();
    bool console = false;
    try {
        auto invocation = capie::launcher::parse_invocation(raw);
        console = invocation.options.help || invocation.options.verify_only;
        if (invocation.options.help) {
            print_line(std::wstring(usage_text) + L"\nBuild: " + capie::launcher::manifest::launcher_version, false);
            return static_cast<int>(ExitCode::ok);
        }
        if (invocation.options.verify_only) {
            return static_cast<int>(verify_only(invocation.options.game));
        }
        return static_cast<int>(launch(std::move(invocation), raw));
    } catch (const LauncherError& error) {
        return report(error, raw, console);
    } catch (...) {
        print_line(L"Unexpected launcher error.", true);
        return static_cast<int>(ExitCode::unexpected);
    }
}

}  // namespace

/**
 * @brief Windows entry point.
 * @return Exit code (see `ExitCode`).
 */
int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, PWSTR /*command*/, int /*show*/) {
    return run();
}
