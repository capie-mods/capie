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
 * blocked by an unanswered dialog. `--no-dialogs` keeps notices in the log only. Texts are
 * Polish when the Windows UI language is Polish, English otherwise.
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
            SYSTEMTIME now{};
            GetSystemTime(&now);
            std::array<char, stamp_chars> stamp{};
            static_cast<void>(std::snprintf(stamp.data(), stamp.size(),
                                            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu %s ", now.wYear, now.wMonth,
                                            now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
                                            GetCurrentProcessId(), level_name(level)));
            std::string line = stamp.data() + to_utf8(text) + "\r\n";
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
};

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
            return polish ? L"Ta wersja Hearts of Iron IV nie jest obsługiwana przez zainstalowane CAPIE. Gra "
                            L"uruchomi się bez CAPIE: zmiana nazw w polu tekstowym i nazwy krajów są wyłączone. "
                            L"Sprawdź aktualizację: install.ps1 -CheckUpdate."
                          : L"This Hearts of Iron IV version is not supported by the installed CAPIE. The game "
                            L"starts without CAPIE: in-place renaming and country names are off. Check for an "
                            L"update: install.ps1 -CheckUpdate.";
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
            if (dialogs_) {
                std::thread([message = std::move(text)]() noexcept {
                    MessageBoxW(nullptr, message.c_str(), dialog_title, MB_OK | MB_ICONWARNING);
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
            if (dialogs_) {
                MessageBoxW(nullptr, text.c_str(), dialog_title, MB_OK | MB_ICONERROR);
            }
        } catch (...) {
            log_.write(Level::error, L"notice could not be shown");
        }
    }

    /// @brief Number of notices issued (tests).
    [[nodiscard]] unsigned count() const noexcept { return count_.load(); }

private:
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
};

}  // namespace capie::launcher
