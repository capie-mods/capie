/**
 * @file game_location.hpp
 * @brief Where the game is: validating the wrapped program, finding the game for the shortcut mode.
 *
 * Wrapper mode: the program handed over by Steam (or by the bootstrap's shortcut) must be
 * an absolute local path to `dowser.exe` or `hoi4.exe`, and its folder must contain
 * `hoi4.exe`. Anything else is refused before any process starts: the launcher is not a
 * general "run this" tool, and a wrong launch option should be fixed, not executed.
 *
 * Shortcut mode (double-click, no arguments): the game folder comes from, in this order,
 * the path remembered by an earlier run (`%LOCALAPPDATA%\RenameAnything\hoi4.path`), the
 * game's Steam uninstall entry (`InstallLocation`), the Steam libraries listed in
 * `libraryfolders.vdf` under Steam's own folder, and finally a file dialog. Registry and
 * Steam files are only read. The launcher then starts the Paradox Launcher of that folder,
 * or `hoi4.exe` when the folder has no `dowser.exe`.
 *
 * `%LOCALAPPDATA%` is taken from the environment first (the bootstrap does the same, and
 * tests point it to a scratch folder) and from the known-folder API otherwise.
 */
#pragma once
#include "command_line.hpp"
#include "config.hpp"
#include "file_pin.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <commdlg.h>
#include <filesystem>
#include <optional>
#include <shlobj.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace capie::launcher {

/// @brief Smallest valid remembered-path file: BOM, one character, NUL (bytes).
inline constexpr LONGLONG remembered_min_bytes = 6;
/// @brief Largest accepted remembered-path file (bytes).
inline constexpr LONGLONG remembered_max_bytes = 65538;
/// @brief UTF-16 byte order mark.
inline constexpr wchar_t utf16_bom = 0xFEFF;
/// @brief Largest Steam library list read.
inline constexpr DWORD library_list_limit = 1U << 20U;

/**
 * @brief Whether a regular file exists at a path.
 * @param path Path.
 * @return `true` for an existing non-directory.
 */
inline bool file_exists(const std::wstring& path) noexcept {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/**
 * @brief Folder part of a path.
 * @param path File path.
 * @return Parent folder without trailing separator.
 */
inline std::wstring parent_folder(const std::wstring& path) {
    return std::filesystem::path(path).parent_path().wstring();
}

/**
 * @brief Checks the program of a wrapped command.
 * @param program Decoded program path.
 * @return Path of `hoi4.exe` in the same folder.
 * @throws LauncherError (usage) when the program is not an absolute local `dowser.exe`/`hoi4.exe`
 *         next to an existing `hoi4.exe`.
 */
inline std::wstring validate_wrapped_program(const std::wstring& program) {
    if (!absolute_local_path(program) || !names_game_program(program) || !file_exists(program)) {
        throw LauncherError(ExitCode::usage,
                            L"The wrapped program is not the game's dowser.exe or hoi4.exe: " + program);
    }
    auto game = parent_folder(program) + L"\\" + game_exe_name;
    if (!file_exists(game)) {
        throw LauncherError(ExitCode::usage, L"No hoi4.exe next to the wrapped program: " + program);
    }
    return game;
}

/**
 * @brief Program the shortcut mode starts for a game: its Paradox Launcher, else the game itself.
 * @param game Path of hoi4.exe.
 * @return `dowser.exe` in the same folder when present, otherwise `game`.
 */
inline std::wstring default_program(const std::wstring& game) {
    auto launcher = parent_folder(game) + L"\\" + paradox_launcher_exe_name;
    return file_exists(launcher) ? launcher : game;
}

/**
 * @brief Extracts the library paths from Steam's `libraryfolders.vdf` text.
 * @param text File content (UTF-8).
 * @return Every `"path"` value, with `\\` unescaped.
 * @note Minimal reader: only quoted `"path" "value"` pairs are used; malformed text yields fewer paths.
 */
inline std::vector<std::wstring> parse_library_folders(std::string_view text) {
    constexpr std::string_view key = "\"path\"";
    std::vector<std::wstring> paths;
    for (std::size_t at = text.find(key); at != std::string_view::npos; at = text.find(key, at + key.size())) {
        std::size_t open = at + key.size();
        while (open < text.size() && (text[open] == ' ' || text[open] == '\t')) {
            ++open;
        }
        if (open >= text.size() || text[open] != '"') {
            continue;
        }
        std::string value;
        std::size_t pos = open + 1;
        for (; pos < text.size() && text[pos] != '"' && text[pos] != '\n'; ++pos) {
            if (text[pos] == '\\' && pos + 1 < text.size()) {
                ++pos;  // VDF escapes: \\ -> \, \" -> "
            }
            value.push_back(text[pos]);
        }
        if (pos < text.size() && text[pos] == '"' && !value.empty()) {
            paths.push_back(from_utf8(value));
        }
    }
    return paths;
}

/**
 * @brief Reads one string value from the registry.
 * @param root Hive.
 * @param key Key path.
 * @param name Value name.
 * @param view `KEY_WOW64_64KEY`, `KEY_WOW64_32KEY` or 0.
 * @return The value, or nothing.
 */
inline std::optional<std::wstring> registry_string(HKEY root, const wchar_t* key, const wchar_t* name, REGSAM view) {
    DWORD bytes = 0;
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | static_cast<DWORD>(view), nullptr, nullptr, &bytes) !=
            ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ | static_cast<DWORD>(view), nullptr, value.data(), &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

/**
 * @brief Reads a small text file whole.
 * @param path File path.
 * @param limit Largest accepted size.
 * @return Content, or nothing.
 */
inline std::optional<std::string> read_small_file(const std::wstring& path, DWORD limit) {
    const Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER size{};
    if (!file || GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < 0 || std::cmp_greater(size.QuadPart, limit)) {
        return std::nullopt;
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD actual = 0;
    if (!content.empty() &&
        (ReadFile(file.get(), content.data(), static_cast<DWORD>(content.size()), &actual, nullptr) == 0 ||
         actual != content.size())) {
        return std::nullopt;
    }
    return content;
}

/**
 * @brief Finds hoi4.exe through Steam: uninstall entry first, then every Steam library.
 * @return Path of an existing hoi4.exe, or nothing.
 * @note Read-only registry and file access.
 */
inline std::optional<std::wstring> find_game_through_steam() {
    for (const REGSAM view : {static_cast<REGSAM>(KEY_WOW64_64KEY), static_cast<REGSAM>(KEY_WOW64_32KEY)}) {
        if (const auto location = registry_string(HKEY_LOCAL_MACHINE, steam_uninstall_key, L"InstallLocation", view)) {
            auto game = *location + L"\\" + game_exe_name;
            if (absolute_local_path(game) && file_exists(game)) {
                return game;
            }
        }
    }
    const auto steam = registry_string(HKEY_CURRENT_USER, steam_user_key, L"SteamPath", 0);
    if (!steam) {
        return std::nullopt;
    }
    std::vector<std::wstring> roots{*steam};
    if (const auto list = read_small_file(*steam + L"\\" + steam_library_list, library_list_limit)) {
        for (auto& path : parse_library_folders(*list)) {
            roots.push_back(std::move(path));
        }
    }
    for (const auto& root : roots) {
        auto game =
            std::filesystem::path(root).make_preferred().wstring() + L"\\" + steam_game_folder + L"\\" + game_exe_name;
        if (absolute_local_path(game) && file_exists(game)) {
            return game;
        }
    }
    return std::nullopt;
}

/**
 * @brief The launcher's data folder, `%LOCALAPPDATA%\RenameAnything`.
 * @param create Create it when missing.
 * @return Folder path.
 * @throws LauncherError when no local application data folder is available.
 */
inline std::wstring app_data_folder(bool create) {
    std::wstring base;
    std::vector<wchar_t> buffer(long_path_chars);
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size() && absolute_local_path({buffer.data(), length})) {
        base.assign(buffer.data(), length);
    } else {
        PWSTR known = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
            CoTaskMemFree(known);
            throw LauncherError(ExitCode::unexpected, L"No local application data folder.");
        }
        base = known;
        CoTaskMemFree(known);
    }
    auto folder = base + L"\\" + app_data_dir_name;
    if (create && CreateDirectoryW(folder.c_str(), nullptr) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw LauncherError(ExitCode::unexpected, L"Cannot create " + folder);
    }
    return folder;
}

/**
 * @brief Path of game.log in the user's Documents folder.
 * @return Absolute path (the file may not exist).
 */
inline std::wstring default_game_log() {
    PWSTR documents = nullptr;
    std::wstring answer;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
        answer = std::wstring(documents) + L"\\" + game_log_relative;
    }
    CoTaskMemFree(documents);
    return answer;
}

/**
 * @brief Game path remembered by an earlier shortcut-mode run.
 * @return Path, or empty when missing or malformed.
 */
inline std::wstring remembered_game() noexcept {
    try {
        const Handle file(CreateFileW((app_data_folder(false) + L"\\" + remembered_game_file).c_str(), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        LARGE_INTEGER size{};
        if (!file || GetFileSizeEx(file.get(), &size) == 0 || size.QuadPart < remembered_min_bytes ||
            size.QuadPart > remembered_max_bytes || size.QuadPart % 2 != 0) {
            return {};
        }
        std::vector<wchar_t> data(static_cast<std::size_t>(size.QuadPart) / sizeof(wchar_t));
        DWORD actual = 0;
        if (ReadFile(file.get(), data.data(), static_cast<DWORD>(size.QuadPart), &actual, nullptr) == 0 ||
            std::cmp_not_equal(actual, size.QuadPart) || data.front() != utf16_bom || data.back() != L'\0') {
            return {};
        }
        const std::wstring path(data.data() + 1, data.size() - 2);
        return absolute_local_path(path) && path.find(L'\0') == std::wstring::npos ? path : std::wstring{};
    } catch (...) {
        return {};
    }
}

/**
 * @brief Remembers the game path for the next shortcut-mode run (atomic replace; failures ignored).
 * @param path Path of hoi4.exe.
 */
inline void remember_game(const std::wstring& path) noexcept {
    try {
        const auto folder = app_data_folder(true);
        const auto temporary =
            folder + L"\\" + remembered_game_file + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        Handle file(
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            return;
        }
        std::wstring data(1, utf16_bom);
        data += path;
        data.push_back(L'\0');
        DWORD actual = 0;
        const auto count = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        const bool written = WriteFile(file.get(), data.data(), count, &actual, nullptr) != 0 && actual == count &&
                             FlushFileBuffers(file.get()) != 0;
        file.reset();
        if (!written || MoveFileExW(temporary.c_str(), (folder + L"\\" + remembered_game_file).c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
            DeleteFileW(temporary.c_str());
        }
    } catch (...) {
        return;  // optional preference: never affects the launch
    }
}

/**
 * @brief Asks the player for hoi4.exe.
 * @return Chosen path, or empty on Cancel.
 * @throws LauncherError (usage) when the dialog cannot open.
 */
inline std::wstring choose_game() {
    std::vector<wchar_t> path(long_path_chars);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Hearts of Iron IV (hoi4.exe)\0hoi4.exe\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Rename Anything: hoi4.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != 0) {
        return path.data();
    }
    const DWORD status = CommDlgExtendedError();
    if (status != 0) {
        throw LauncherError(ExitCode::usage, L"Cannot open the file dialog." + windows_error(status));
    }
    return {};
}

}  // namespace capie::launcher
