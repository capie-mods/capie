/**
 * @file fake_process.cpp
 * @brief Stand-in for dowser.exe, hoi4.exe, an intermediate process and capie_injector.exe in launcher tests.
 *
 * One binary plays every role; the role is its own file name, so the test copies it as
 * `dowser.exe`, `hoi4.exe`, `relay.exe` and `capie_injector.exe` into scratch folders.
 * Behaviour comes from environment variables, which the launcher passes down unchanged:
 *
 *  - `FAKE_RECORD`: file that every role appends one line to (role, PID, parent's view of
 *    the command line, working folder, `FAKE_MARK`).
 *  - `FAKE_DOWSER_MODE` (dowser): `child` (start hoi4.exe, wait), `grandchild` (through
 *    relay.exe), `breakaway` (CREATE_BREAKAWAY_FROM_JOB, wait), `orphan` (breakaway, exit
 *    at once), `early` (start, exit at once), `twice` (two games one after the other),
 *    `parallel` (a second game, living twice as long, 300 ms after the first), `none` (no game).
 *  - `FAKE_GAME_LOG`, `FAKE_GAME_MARKERS` (default 2), `FAKE_GAME_DELAY_MS` (default 300),
 *    `FAKE_GAME_LIFETIME_MS` (default 4000), `FAKE_GAME_WINDOW` (default 1) for hoi4.exe.
 *  - `FAKE_INJECT_FAIL_DLL` (file name that fails with code 14), `FAKE_INJECT_SLEEP_MS`,
 *    `FAKE_INJECT_KILL_GAME` (1: end the target game first, as a crash during injection would).
 *
 * The fake game creates a real, visible but off-screen top-level window and pumps its
 * messages, so the launcher's window probe sees exactly what it would see in HOI4. Nothing
 * here touches a real game, Steam or any file outside the paths given by the test.
 */
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// @brief Readiness marker written by the fake game (same text as the real build).
constexpr char marker_line[] = "[00:00:00][no_game_date][history.cpp:165]: Executing History from x to y\r\n";
/// @brief Exit code of a failing fake injection (the injector's "RNInitialize refused").
constexpr int inject_refused = 14;
/// @brief Off-screen window position.
constexpr int off_screen = -32000;
/// @brief Window size.
constexpr int window_size = 64;
/// @brief Default fake game lifetime.
constexpr DWORD default_lifetime_ms = 4000;
/// @brief Default delay before the fake game writes its markers.
constexpr DWORD default_delay_ms = 300;
/// @brief Lifetime of the `none` Paradox Launcher.
constexpr DWORD none_lifetime_ms = 1500;
/// @brief Delay between the two games of the `parallel` mode.
constexpr DWORD parallel_gap_ms = 300;
/// @brief Pump period.
constexpr DWORD pump_ms = 20;

/**
 * @brief Reads an environment variable.
 * @param name Name.
 * @return Value or empty.
 */
std::wstring env(const wchar_t* name) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    return length > 0 && length < buffer.size() ? std::wstring(buffer.data(), length) : std::wstring{};
}

/**
 * @brief Numeric environment variable.
 * @param name Name.
 * @param fallback Default.
 * @return Value.
 */
DWORD env_number(const wchar_t* name, DWORD fallback) {
    const auto text = env(name);
    return text.empty() ? fallback : static_cast<DWORD>(std::wcstoul(text.c_str(), nullptr, 10));
}

/**
 * @brief UTF-16 to UTF-8.
 * @param text Text.
 * @return UTF-8.
 */
std::string utf8(const std::wstring& text) {
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string answer(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), answer.data(), size, nullptr,
                            nullptr);
    }
    return answer;
}

/**
 * @brief Appends bytes to a file (shared append).
 * @param path File.
 * @param bytes Content.
 */
void append(const std::wstring& path, const std::string& bytes) {
    if (path.empty()) {
        return;
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(file);
    }
}

/**
 * @brief Appends this process's record line.
 * @param role Role name.
 * @param extra Extra text.
 */
void record(const std::wstring& role, const std::wstring& extra) {
    std::vector<wchar_t> cwd(32768);
    GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
    const std::wstring line = role + L"|" + std::to_wstring(GetCurrentProcessId()) + L"|" + GetCommandLineW() + L"|" +
                              cwd.data() + L"|" + env(L"FAKE_MARK") + L"|" + extra + L"\n";
    append(env(L"FAKE_RECORD"), utf8(line));
}

/**
 * @brief Starts a sibling program from this program's folder.
 * @param name File name.
 * @param arguments Arguments after the program.
 * @param flags Creation flags.
 * @return Process handle or nullptr.
 */
HANDLE start_sibling(const std::wstring& name, const std::wstring& arguments, DWORD flags) {
    std::vector<wchar_t> self(32768);
    GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    const auto folder = std::filesystem::path(self.data()).parent_path().wstring();
    const auto program = folder + L"\\" + name;
    std::wstring line = L"\"" + program + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, flags, nullptr, folder.c_str(), &startup,
                       &info) == 0) {
        record(L"start-failed", name + L" " + std::to_wstring(GetLastError()));
        return nullptr;
    }
    CloseHandle(info.hThread);
    return info.hProcess;
}

/**
 * @brief Starts a sibling and optionally waits for it.
 * @param name File name.
 * @param flags Creation flags.
 * @param wait Wait for its end.
 */
void run_sibling(const std::wstring& name, DWORD flags, bool wait) {
    HANDLE child = start_sibling(name, L"-gdpr-compliant", flags);
    if (child != nullptr) {
        if (wait) {
            WaitForSingleObject(child, INFINITE);
        }
        CloseHandle(child);
    }
}

/**
 * @brief Paradox Launcher stand-in.
 * @return Exit code.
 */
int dowser() {
    const auto mode = env(L"FAKE_DOWSER_MODE");
    record(L"dowser", mode);
    if (mode == L"grandchild") {
        run_sibling(L"relay.exe", 0, true);
    } else if (mode == L"breakaway") {
        run_sibling(L"hoi4.exe", CREATE_BREAKAWAY_FROM_JOB, true);
    } else if (mode == L"orphan") {
        run_sibling(L"hoi4.exe", CREATE_BREAKAWAY_FROM_JOB, false);
    } else if (mode == L"early") {
        run_sibling(L"hoi4.exe", 0, false);
    } else if (mode == L"parallel") {
        HANDLE first = start_sibling(L"hoi4.exe", L"-gdpr-compliant", 0);
        Sleep(parallel_gap_ms);
        HANDLE second = start_sibling(L"hoi4.exe", L"-gdpr-compliant -long", 0);
        for (HANDLE child : {first, second}) {
            if (child != nullptr) {
                WaitForSingleObject(child, INFINITE);
                CloseHandle(child);
            }
        }
    } else if (mode == L"twice") {
        run_sibling(L"hoi4.exe", 0, true);
        run_sibling(L"hoi4.exe", 0, true);
    } else if (mode == L"none") {
        Sleep(none_lifetime_ms);
    } else {
        run_sibling(L"hoi4.exe", 0, true);
    }
    return 0;
}

/**
 * @brief Game stand-in: window, game.log markers, fixed lifetime.
 * @return Exit code.
 */
int game() {
    record(L"hoi4", {});
    const auto log = env(L"FAKE_GAME_LOG");
    const DWORD markers = env_number(L"FAKE_GAME_MARKERS", 2);
    const DWORD delay = env_number(L"FAKE_GAME_DELAY_MS", default_delay_ms);
    const DWORD base_lifetime = env_number(L"FAKE_GAME_LIFETIME_MS", default_lifetime_ms);
    const bool long_run = std::wstring_view(GetCommandLineW()).ends_with(L" -long");
    const DWORD lifetime = long_run ? base_lifetime * 2 : base_lifetime;
    HWND window = nullptr;
    if (env_number(L"FAKE_GAME_WINDOW", 1) != 0) {
        window =
            CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"fake hoi4", WS_POPUP | WS_VISIBLE, off_screen, off_screen,
                            window_size, window_size, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!log.empty() && markers > 0) {
        HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CloseHandle(file);
    }
    const ULONGLONG start = GetTickCount64();
    DWORD written = 0;
    while (GetTickCount64() - start < lifetime) {
        if (written < markers && GetTickCount64() - start >= delay) {
            append(log, marker_line);
            ++written;
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, pump_ms, QS_ALLINPUT);
    }
    if (window != nullptr) {
        DestroyWindow(window);
    }
    return 0;
}

/**
 * @brief Injector stand-in: records its arguments, fails or sleeps on request.
 * @param argc Argument count.
 * @param argv Arguments.
 * @return 0, or 14 for the configured failing DLL.
 */
int injector(int argc, wchar_t** argv) {
    record(L"injector", argc == 3 ? std::wstring(argv[1]) + L" " + argv[2] : L"bad-usage");
    Sleep(env_number(L"FAKE_INJECT_SLEEP_MS", 0));
    if (argc == 3 && env_number(L"FAKE_INJECT_KILL_GAME", 0) != 0) {
        HANDLE target = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                                    static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10)));
        if (target != nullptr) {
            TerminateProcess(target, 3);  // what a crash inside RNInitialize looks like from outside
            WaitForSingleObject(target, INFINITE);
            CloseHandle(target);
        }
        return inject_refused;
    }
    const auto failing = env(L"FAKE_INJECT_FAIL_DLL");
    if (argc == 3 && !failing.empty() && std::filesystem::path(argv[2]).filename().wstring() == failing) {
        return inject_refused;
    }
    return argc == 3 ? 0 : 2;
}

}  // namespace

/**
 * @brief Entry point; dispatches on the executable's own file name.
 * @param argc Argument count.
 * @param argv Arguments.
 * @return Role exit code.
 */
int wmain(int argc, wchar_t** argv) {  // NOLINT(misc-use-internal-linkage): process entry point
    std::vector<wchar_t> self(32768);
    GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    const auto name = std::filesystem::path(self.data()).filename().wstring();
    if (_wcsicmp(name.c_str(), L"dowser.exe") == 0) {
        return dowser();
    }
    if (_wcsicmp(name.c_str(), L"relay.exe") == 0) {
        record(L"relay", {});
        run_sibling(L"hoi4.exe", 0, true);
        return 0;
    }
    if (_wcsicmp(name.c_str(), L"capie_injector.exe") == 0) {
        return injector(argc, argv);
    }
    return game();
}
