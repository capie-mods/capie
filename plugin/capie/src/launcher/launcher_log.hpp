/**
 * @file launcher_log.hpp
 * @brief The launcher's local log (`%LOCALAPPDATA%\RenameAnything\launcher.log`) and player notices.
 *
 * Every decision the launcher makes goes to the log as one UTF-8 line with a UTC time
 * stamp, the launcher's PID and a level. The log stays on the machine: the launcher has no
 * network code at all and sends nothing anywhere. At start, a log over 1 MiB is moved to
 * `launcher.log.1` (one generation), so the folder cannot grow without bound.
 *
 * Notices are the few things the player must hear about (unknown game build, damaged
 * bundle, CAPIE not loaded). Each notice is logged; when dialogs are on it is also shown
 * in a message box on its own thread, so the launcher keeps working and the game is never
 * blocked by an unanswered dialog. The box is topmost and asks for the foreground: the
 * launcher has no window of its own, and a notice hidden behind the Paradox Launcher or the
 * game's loading screen would never be read. `--no-dialogs` keeps notices in the log only.
 * Texts are Polish when the Windows UI language is Polish, English otherwise.
 *
 * Next to the log, `last-status.txt` holds the outcome of the latest run in four
 * `key=value` lines (`time`, `outcome`, `message`, `log`), replaced atomically, so the
 * player, a support request or the installer can read "what happened last time" without
 * reading the log.
 */
#pragma once
#include "config.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace capie::launcher {

/// @brief Title of every launcher dialog.
inline constexpr wchar_t dialog_title[] = L"Rename Anything / CAPIE";

/// @brief Severity of a log line.
enum class Level : std::uint8_t { info, warning, error };

/// @brief Characters of the time stamp prefix buffer.
inline constexpr std::size_t stamp_chars = 64;
/// @brief Style of every non-blocking notice box: on top of the game and the Paradox Launcher.
inline constexpr UINT notice_box_style = MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND;
/// @brief Style of a notice box shown when nothing was started.
inline constexpr UINT error_box_style = MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND;

/**
 * @brief UTC time as ISO 8601 with milliseconds.
 * @return For example `2026-10-27T12:00:00.000Z`.
 */
inline std::string utc_stamp() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    std::array<char, stamp_chars> stamp{};
    static_cast<void>(std::snprintf(stamp.data(), stamp.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", now.wYear,
                                    now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds));
    return stamp.data();
}

/**
 * @brief One line of the status file: line breaks and tabs of a notice become single spaces.
 * @param text Text.
 * @return Single-line text without trailing spaces.
 */
inline std::wstring single_line(std::wstring_view text) {
    std::wstring line;
    for (const wchar_t ch : text) {
        const bool control = ch == L'\r' || ch == L'\n' || ch == L'\t';
        if (!control) {
            line.push_back(ch);
        } else if (!line.empty() && line.back() != L' ') {
            line.push_back(L' ');
        }
    }
    while (!line.empty() && line.back() == L' ') {
        line.pop_back();
    }
    return line;
}

/// @brief Append-only, thread-safe local log file.
class Log {
public:
    /**
     * @brief Opens (rotates) the log.
     * @param path Log path; empty disables logging.
     */
    explicit Log(std::wstring path) : path_(std::move(path)) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!path_.empty() && GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data) != 0 &&
            ((static_cast<ULONGLONG>(data.nFileSizeHigh) << dword_bits) | data.nFileSizeLow) > log_rotate_bytes) {
            MoveFileExW(path_.c_str(), (path_ + rotated_log_suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }

    /**
     * @brief Appends one line.
     * @param level Severity.
     * @param text Message.
     * @note Never throws; a failed write is dropped.
     */
    void write(Level level, std::wstring_view text) noexcept {
        if (path_.empty()) {
            return;
        }
        try {
            std::array<char, stamp_chars> prefix{};
            static_cast<void>(std::snprintf(prefix.data(), prefix.size(), " pid=%lu %s ", GetCurrentProcessId(),
                                            level_name(level)));
            std::string line = utc_stamp() + prefix.data() + to_utf8(text) + "\r\n";
            const std::scoped_lock lock(mutex_);
            const Handle file(CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD written = 0;
            if (file) {
                WriteFile(file.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            }
        } catch (...) {
            return;  // logging must never break the launch
        }
    }

    /// @brief Log path (empty when disabled).
    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

    /**
     * @brief Path of the status file next to the log.
     * @return `last-status.txt` in the log's folder, or empty when logging is disabled.
     */
    [[nodiscard]] std::wstring status_path() const {
        const auto slash = path_.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring{} : path_.substr(0, slash + 1) + status_file_name;
    }

    /**
     * @brief Replaces the status file with the outcome of this run.
     * @param outcome Short tag (for example `capie-loaded`, `game-without-capie`, `not-started`).
     * @param message One sentence for the player (line breaks become spaces).
     * @note Never throws. Written to a temporary file and moved over the old one, so a reader
     *       sees the previous or the new status, never a half-written one.
     */
    void status(std::wstring_view outcome, std::wstring_view message) noexcept {
        try {
            const auto target = status_path();
            if (target.empty()) {
                return;
            }
            const std::string text = "time=" + utc_stamp() + "\r\noutcome=" + to_utf8(single_line(outcome)) +
                                     "\r\nmessage=" + to_utf8(single_line(message)) + "\r\nlog=" + to_utf8(path_) +
                                     "\r\n";
            const auto temporary = target + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
            const std::scoped_lock lock(mutex_);
            bool written = false;
            {
                const Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr));
                DWORD actual = 0;
                written = file &&
                          WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &actual, nullptr) != 0 &&
                          actual == text.size();
            }
            if (!written || MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
                DeleteFileW(temporary.c_str());
            }
        } catch (...) {
            return;  // the status file is a convenience; it never affects the launch
        }
    }

private:
    /**
     * @brief Text of a level.
     * @param level Level.
     * @return Fixed tag.
     */
    static const char* level_name(Level level) noexcept {
        switch (level) {
            case Level::warning:
                return "[warn]";
            case Level::error:
                return "[error]";
            case Level::info:
                break;
        }
        return "[info]";
    }

    std::wstring path_;
    std::mutex mutex_;
};

/// @brief Things the player is told about.
enum class Notice : std::uint8_t {
    unknown_game_build,  ///< hoi4.exe hash is not the pinned build
    bundle_invalid,      ///< CAPIE files missing, modified or not writable
    foreign_game,        ///< the game in the tree is not the pinned hoi4.exe next to the wrapped program
    not_ready,           ///< the game did not reach the main menu in time
    capie_failed,        ///< an injector step failed
    invalid_command,     ///< wrapped command refused
    start_failed,        ///< the wrapped program could not start
    playset_unknown,     ///< the active playset could not be read; CAPIE stays off
    unknown_program,     ///< Steam's command names another program in the game folder: started unchanged, CAPIE off
    elevated_start,      ///< the program needs administrator rights: started through the shell, CAPIE off
    launcher_problem,    ///< an unexpected error before the start: the game's command started unchanged, CAPIE off
    crash_guard,         ///< the last start ended while CAPIE was loading: this start runs without CAPIE once
};

/// @brief Last notice value (tests iterate every notice up to it).
inline constexpr Notice last_notice = Notice::crash_guard;

/**
 * @brief Whether the Windows UI language is Polish.
 * @return `true` for Polish.
 */
inline bool polish_ui() noexcept {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_POLISH;
}

/**
 * @brief Player-facing text of a notice.
 * @param notice Notice.
 * @param polish Polish text instead of English.
 * @return Text without details.
 */
inline std::wstring_view notice_text(Notice notice, bool polish) noexcept {
    switch (notice) {
        case Notice::unknown_game_build:
            return polish ? L"CAPIE wymaga aktualizacji do tej wersji Hearts of Iron IV. Gra uruchomi się bez "
                            L"CAPIE: do czasu aktualizacji zmiana nazw w polu tekstowym i nazwy krajów są wyłączone. "
                            L"Zaktualizuj CAPIE w menu instalatora (instalacja lub aktualizacja) albo pobierz "
                            L"najnowsze wydanie: https://github.com/capie-mods/capie/releases/latest"
                          : L"CAPIE needs an update for this Hearts of Iron IV version. The game starts without "
                            L"CAPIE: in-place renaming and country names are off until then. Update CAPIE from the "
                            L"installer menu (install or update) or download the latest release: "
                            L"https://github.com/capie-mods/capie/releases/latest";
        case Notice::bundle_invalid:
            return polish ? L"Pliki CAPIE są niekompletne, zmienione albo folder nie jest zapisywalny. Gra uruchomi "
                            L"się bez CAPIE. Zainstaluj ponownie: install.ps1 -Install."
                          : L"The CAPIE files are incomplete, modified, or their folder is not writable. The game "
                            L"starts without CAPIE. Reinstall: install.ps1 -Install.";
        case Notice::foreign_game:
            return polish ? L"Uruchomiona gra nie jest plikiem hoi4.exe sprawdzonym przed startem. CAPIE nie "
                            L"zostanie załadowane; gra działa normalnie."
                          : L"The running game is not the hoi4.exe checked before the start. CAPIE is not loaded; "
                            L"the game runs normally.";
        case Notice::not_ready:
            return polish ? L"Gra nie doszła do menu głównego w oczekiwanym czasie. CAPIE nie zostanie załadowane; "
                            L"gra działa normalnie."
                          : L"The game did not reach the main menu in time. CAPIE is not loaded; the game runs "
                            L"normally.";
        case Notice::capie_failed:
            return polish ? L"Nie udało się w pełni załadować CAPIE. Gra pozostaje otwarta; zamknij ją przed "
                            L"kolejną próbą. Launcher niczego nie ponawia i niczego nie zamyka."
                          : L"CAPIE could not be fully loaded. The game stays open; close it before trying again. "
                            L"The launcher retries nothing and closes nothing.";
        case Notice::invalid_command:
            return polish ? L"Launcher oczekuje polecenia gry (dowser.exe lub hoi4.exe z folderu gry). W Steam "
                            L"opcje uruchamiania powinny mieć postać: \"...\\RenameAnythingLauncher.exe\" %command%"
                          : L"The launcher expects the game's command (dowser.exe or hoi4.exe from the game folder). "
                            L"The Steam launch options should read: \"...\\RenameAnythingLauncher.exe\" %command%";
        case Notice::start_failed:
            return polish ? L"Nie można uruchomić gry." : L"The game could not be started.";
        case Notice::playset_unknown:
            return polish ? L"Nie udało się odczytać aktywnego zestawu modów, więc nie wiadomo, czy mod „CAPIE” jest "
                            L"włączony. Gra działa bez CAPIE. Nic nie zmieniono."
                          : L"The active playset could not be read, so it is not known whether the \"CAPIE\" mod is "
                            L"enabled. The game runs without CAPIE. Nothing was changed.";
        case Notice::unknown_program:
            return polish ? L"Steam uruchamia program, którego launcher nie zna (ani dowser.exe, ani hoi4.exe). "
                            L"Został uruchomiony bez zmian, a gra działa bez CAPIE. Jeśli to skutek aktualizacji "
                            L"gry, CAPIE wymaga aktualizacji: https://github.com/capie-mods/capie/releases/latest"
                          : L"Steam starts a program the launcher does not know (neither dowser.exe nor hoi4.exe). "
                            L"It was started unchanged and the game runs without CAPIE. If this follows a game "
                            L"update, CAPIE needs an update: https://github.com/capie-mods/capie/releases/latest";
        case Notice::elevated_start:
            return polish ? L"Program gry wymaga uprawnień administratora, więc uruchomił go system Windows. Gra "
                            L"działa bez CAPIE (CAPIE nie ładuje się do gry uruchomionej jako administrator)."
                          : L"The game's program requires administrator rights, so Windows started it. The game runs "
                            L"without CAPIE (CAPIE does not load into a game that runs as administrator).";
        case Notice::launcher_problem:
            return polish ? L"Launcher napotkał nieoczekiwany błąd przed startem. Polecenie gry zostało uruchomione "
                            L"bez zmian, a gra działa bez CAPIE. Szczegóły są w launcher.log."
                          : L"The launcher hit an unexpected error before the start. The game's command was started "
                            L"unchanged and the game runs without CAPIE. Details are in launcher.log.";
        case Notice::crash_guard:
            return polish ? L"Poprzednie uruchomienie zakończyło się w trakcie ładowania CAPIE (gra mogła ulec "
                            L"awarii lub zostać zamknięta). Tym razem gra działa bez CAPIE; następne uruchomienie "
                            L"znów spróbuje je załadować."
                          : L"The previous start ended while CAPIE was loading (the game may have crashed or been "
                            L"closed). This time the game runs without CAPIE; the next start tries again.";
    }
    return {};
}

/// @brief Logs notices and, when enabled, shows them without blocking the launcher.
class Notifier {
public:
    /**
     * @brief Creates the notifier.
     * @param log Log (outlives the notifier).
     * @param dialogs Show message boxes.
     */
    Notifier(Log& log, bool dialogs) : log_(log), dialogs_(dialogs), polish_(polish_ui()) {}

    /**
     * @brief Logs a notice and shows it on a separate thread.
     * @param notice Notice.
     * @param detail Technical detail (logged, and shown after the text).
     */
    void notify(Notice notice, std::wstring_view detail) noexcept {
        ++count_;
        try {
            auto text = compose(notice, detail);
            log_.write(Level::warning, L"notice: " + text);
            remember(text);
            if (dialogs_) {
                std::thread([message = std::move(text)]() noexcept {
                    MessageBoxW(nullptr, message.c_str(), dialog_title, notice_box_style);
                }).detach();
            }
        } catch (...) {
            log_.write(Level::error, L"notice could not be shown");
        }
    }

    /**
     * @brief Logs a notice and shows it modally (only before any game process exists).
     * @param notice Notice.
     * @param detail Technical detail.
     */
    void notify_blocking(Notice notice, std::wstring_view detail) noexcept {
        ++count_;
        try {
            const auto text = compose(notice, detail);
            log_.write(Level::error, L"notice: " + text);
            remember(text);
            if (dialogs_) {
                MessageBoxW(nullptr, text.c_str(), dialog_title, error_box_style);
            }
        } catch (...) {
            log_.write(Level::error, L"notice could not be shown");
        }
    }

    /// @brief Number of notices issued (tests).
    [[nodiscard]] unsigned count() const noexcept { return count_.load(); }

    /**
     * @brief Text of the latest notice (for the status file).
     * @return Text with detail, or empty when there was none.
     */
    [[nodiscard]] std::wstring last_text() const {
        const std::scoped_lock lock(mutex_);
        return last_;
    }

private:
    /**
     * @brief Keeps the latest notice text.
     * @param text Text.
     */
    void remember(const std::wstring& text) {
        const std::scoped_lock lock(mutex_);
        last_ = text;
    }

    /**
     * @brief Text plus detail.
     * @param notice Notice.
     * @param detail Detail.
     * @return Combined text (never throws; falls back to the bare text).
     */
    std::wstring compose(Notice notice, std::wstring_view detail) const noexcept {
        try {
            std::wstring text(notice_text(notice, polish_));
            if (!detail.empty()) {
                text += L"\n\n";
                text += detail;
            }
            return text;
        } catch (...) {
            return {};
        }
    }

    Log& log_;
    bool dialogs_;
    bool polish_;
    std::atomic<unsigned> count_{0};
    mutable std::mutex mutex_;
    std::wstring last_;
};

}  // namespace capie::launcher
