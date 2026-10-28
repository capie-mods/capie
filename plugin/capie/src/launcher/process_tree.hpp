/**
 * @file process_tree.hpp
 * @brief Starting the wrapped command inside a job object, and finding the game among its descendants.
 *
 * The Paradox Launcher, not this launcher, starts hoi4.exe, so the launcher cannot simply
 * keep the handle that `CreateProcess` returned. It has to recognise "the game that the
 * program I started has started", and never any other running game.
 *
 * The primary mechanism is a job object. The wrapped program is created suspended, put
 * into a fresh job and only then resumed, so every process it starts afterwards (and every
 * process those start) is a job member from its first instruction. The job is created
 * with no limits except `JOB_OBJECT_LIMIT_BREAKAWAY_OK`: a program that explicitly asks to
 * leave the job may do so, because refusing would make its `CreateProcess` fail and could
 * stop the game from starting. There is no kill-on-close: when the launcher exits, nothing
 * else is affected. A game found through the job is checked again after its handle is open
 * (`IsProcessInJob` on that handle), so a PID that was reused between the listing and the
 * open can never be mistaken for a member.
 *
 * The fallback covers a game that left the job (breakaway) or a job that could not be
 * assigned (an outer job that forbids nesting). A process named hoi4.exe, created after
 * the launch, is accepted only when its parent chain reaches a job member (or the started
 * program itself) within four links, and every parent on that chain is still running and
 * was created before its child. The creation-time order is what rules out PID reuse: a
 * PID recycled by a newer process would have a parent younger than the child.
 *
 * A game that started and was already handled is remembered by PID and creation time, so
 * it is never handled twice, and a second game started while the first one runs is not
 * touched.
 *
 * A program whose manifest requires administrator rights makes `CreateProcess` fail with
 * `ERROR_ELEVATION_REQUIRED` (740); the launcher itself runs `asInvoker`. The game must
 * still start, so the launcher then hands the same program, arguments and folder to
 * `ShellExecuteExW`, which shows the Windows consent prompt exactly as a double-click
 * would. Such a tree has no job and CAPIE stays off (`shell_started`): an elevated game is
 * out of reach for an unelevated injector anyway. The launcher waits for that program only.
 */
#pragma once
#include "config.hpp"
#include "file_pin.hpp"
#include "launcher_log.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace capie::launcher {

/// @brief Initial capacity of the job member list.
inline constexpr DWORD job_list_initial = 64;
/// @brief Largest job member list read.
inline constexpr DWORD job_list_max = 4096;

/// @brief A process identity that survives PID reuse: PID plus creation time.
struct ProcessStamp {
    DWORD pid = 0;          ///< process id
    ULONGLONG created = 0;  ///< creation time, 100 ns units since 1601
    /// @brief Field-wise equality.
    bool operator==(const ProcessStamp&) const = default;
};

/**
 * @brief FILETIME as one 64-bit number.
 * @param time File time.
 * @return 100 ns units since 1601.
 */
constexpr ULONGLONG ticks(const FILETIME& time) noexcept {
    return (static_cast<ULONGLONG>(time.dwHighDateTime) << dword_bits) | time.dwLowDateTime;
}

/**
 * @brief Creation time of a process.
 * @param process Handle with query access.
 * @return Creation time, or nothing.
 */
inline std::optional<ULONGLONG> creation_time(HANDLE process) noexcept {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) == 0) {
        return std::nullopt;
    }
    return ticks(created);
}

/**
 * @brief Exit time of an ended process.
 * @param process Handle with query access.
 * @return Exit time, or nothing while it runs or on error.
 */
inline std::optional<ULONGLONG> exit_time(HANDLE process) noexcept {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0 ||
        GetProcessTimes(process, &created, &exited, &kernel, &user) == 0) {
        return std::nullopt;
    }
    return ticks(exited);
}

/**
 * @brief Current system time.
 * @return 100 ns units since 1601.
 */
inline ULONGLONG now_ticks() noexcept {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return ticks(now);
}

/**
 * @brief Whether a process handle refers to a running process.
 * @param process Handle with SYNCHRONIZE access.
 * @return `true` while it runs.
 */
inline bool running(HANDLE process) noexcept {
    return WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

/**
 * @brief Full image path of a process.
 * @param process Handle with query access.
 * @return Path, or empty.
 */
inline std::wstring image_path(HANDLE process) {
    std::vector<wchar_t> buffer(long_path_chars);
    auto size = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size) == 0) {
        return {};
    }
    return {buffer.data(), size};
}

/**
 * @brief Case-insensitive file-name comparison of a path.
 * @param path Path.
 * @param name File name.
 * @return `true` when the last component equals `name`.
 */
inline bool file_name_is(const std::wstring& path, const wchar_t* name) {
    return _wcsicmp(std::filesystem::path(path).filename().c_str(), name) == 0;
}

/// @brief What the ancestry check knows about one process.
struct ProcessFacts {
    DWORD parent = 0;       ///< parent PID recorded at creation
    ULONGLONG created = 0;  ///< creation time
    bool in_tree = false;   ///< member of the launcher's job, or the started program itself
};

/// @brief Looks up the facts of a running process (nothing when it cannot be opened).
using FactsFn = std::function<std::optional<ProcessFacts>(DWORD)>;

/**
 * @brief Whether a process descends from the launcher's tree through running, older parents.
 * @param pid Candidate PID.
 * @param facts Fact lookup.
 * @param max_depth Largest number of parent links followed.
 * @return `true` when a parent within `max_depth` links is in the tree and each link's parent
 *         is running and was created no later than its child.
 */
inline bool descends_from_tree(DWORD pid, const FactsFn& facts, unsigned max_depth) {
    auto current = facts(pid);
    if (!current) {
        return false;
    }
    for (unsigned depth = 0; depth < max_depth; ++depth) {
        if (current->parent == 0 || current->parent == pid) {
            return false;
        }
        const auto parent = facts(current->parent);
        if (!parent || parent->created > current->created) {
            return false;
        }
        if (parent->in_tree) {
            return true;
        }
        current = parent;
    }
    return false;
}

/// @brief How to start the wrapped program.
struct LaunchSpec {
    std::wstring application;        ///< absolute program path
    std::wstring command_line;       ///< full command line (program first)
    std::wstring working_directory;  ///< empty: inherit the launcher's
};

/**
 * @brief Creates the wrapped program suspended; the production value is `create_suspended`.
 *
 * Same contract as `CreateProcessW`: nonzero on success, zero with `GetLastError` set on
 * failure. Tests pass a function that fails with a chosen error (for example 740).
 */
using CreateFn = BOOL (*)(const LaunchSpec& spec, std::wstring& line, STARTUPINFOW& startup, PROCESS_INFORMATION& info);

/**
 * @brief `CreateProcessW` of the wrapped program, suspended, inheriting folder and environment.
 * @param spec Program, command line and working folder.
 * @param line Mutable command line.
 * @param startup Startup information.
 * @param info Receives the process and thread.
 * @return `CreateProcessW` result.
 */
inline BOOL create_suspended(const LaunchSpec& spec, std::wstring& line, STARTUPINFOW& startup,
                             PROCESS_INFORMATION& info) {
    return CreateProcessW(spec.application.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                          spec.working_directory.empty() ? nullptr : spec.working_directory.c_str(), &startup, &info);
}

/**
 * @brief Arguments of a launch: the command line after the quoted program, verbatim.
 * @param spec Program and command line (built by `wrapped_command_line`).
 * @return Text after the program (quoted or, for Steam's raw command, unquoted) without the one
 *         separating blank, or the whole line when it does not start with the program.
 */
inline std::wstring launch_arguments(const LaunchSpec& spec) {
    const std::wstring quoted = L"\"" + spec.application + L"\"";
    std::size_t skip = 0;
    if (spec.command_line.starts_with(quoted)) {
        skip = quoted.size();
    } else if (spec.command_line.starts_with(spec.application)) {
        skip = spec.application.size();
    } else {
        return spec.command_line;
    }
    std::wstring rest = spec.command_line.substr(skip);
    if (!rest.empty() && (rest.front() == L' ' || rest.front() == L'\t')) {
        rest.erase(0, 1);
    }
    return rest;
}

/**
 * @brief Starts the wrapped program through the shell (consent prompt for an elevated program).
 * @param spec Program, command line and working folder.
 * @param show `nShow` value.
 * @return Process handle; empty when the shell started it without returning one.
 * @throws LauncherError (game_start) when the shell cannot start it or the player declined the prompt.
 */
inline Handle shell_start(const LaunchSpec& spec, int show) {
    const std::wstring arguments = launch_arguments(spec);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = spec.application.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.lpDirectory = spec.working_directory.empty() ? nullptr : spec.working_directory.c_str();
    info.nShow = show;
    const BOOL started = ShellExecuteExW(&info);
    const DWORD error = GetLastError();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    if (started == 0) {
        throw LauncherError(ExitCode::game_start, L"Cannot start " + spec.application + windows_error(error));
    }
    return Handle(info.hProcess);
}

/// @brief A game process bound to the launcher's tree.
struct BoundGame {
    Handle process;        ///< query + synchronize access
    ProcessStamp stamp;    ///< identity
    std::wstring image;    ///< image path
    bool via_job = false;  ///< found as a job member (else through ancestry)
};

/// @brief The started program, its job and the descendants.
class ProcessTree {
public:
    /**
     * @brief Starts the program suspended, assigns it to a new job, resumes it.
     * @param spec Program, command line and working folder.
     * @param log Log for the job decision.
     * @param create Process creation (tests replace it).
     * @return The tree. When the program requires elevation it was started through the shell
     *         instead (`shell_started`), without a job.
     * @throws LauncherError (game_start) when the program cannot be created or resumed.
     */
    static ProcessTree start(const LaunchSpec& spec, Log& log, CreateFn create = create_suspended) {
        ProcessTree tree;
        tree.started_ = now_ticks();
        tree.job_.reset(CreateJobObjectW(nullptr, nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        const bool job_ready = tree.job_ && SetInformationJobObject(tree.job_.get(), JobObjectExtendedLimitInformation,
                                                                    &limits, sizeof(limits)) != 0;
        std::wstring line = spec.command_line;
        STARTUPINFOW own{};
        own.cb = sizeof(own);
        GetStartupInfoW(&own);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = own.dwFlags & STARTF_USESHOWWINDOW;
        startup.wShowWindow = own.wShowWindow;
        PROCESS_INFORMATION info{};
        if (create(spec, line, startup, info) == 0) {
            const DWORD error = GetLastError();
            if (error != ERROR_ELEVATION_REQUIRED) {
                throw LauncherError(ExitCode::game_start, L"Cannot start " + spec.application + windows_error(error));
            }
            log.write(Level::warning, L"the program requires elevation" + windows_error(error) +
                                          L"; starting it through the shell, CAPIE off");
            const int show = (own.dwFlags & STARTF_USESHOWWINDOW) != 0 ? own.wShowWindow : SW_SHOWNORMAL;
            tree.root_ = shell_start(spec, show);
            tree.shell_ = true;
            tree.job_.reset();
            const DWORD pid = tree.root_ ? GetProcessId(tree.root_.get()) : 0;
            tree.root_stamp_ = {.pid = pid, .created = tree.root_ ? creation_time(tree.root_.get()).value_or(0) : 0};
            log.write(Level::info, L"started through the shell pid=" + std::to_wstring(pid) + L" program=" +
                                       spec.application + (tree.root_ ? L"" : L" (no process handle; not waited for)"));
            return tree;
        }
        tree.root_.reset(info.hProcess);
        const Handle thread(info.hThread);
        tree.root_stamp_ = {.pid = info.dwProcessId, .created = creation_time(info.hProcess).value_or(0)};
        tree.attached_ = job_ready && AssignProcessToJobObject(tree.job_.get(), info.hProcess) != 0;
        if (!tree.attached_) {
            log.write(Level::warning, L"job object not available" + windows_error(GetLastError()) +
                                          L"; the game will be recognised through its parent chain only");
        }
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
            // The process never ran a single instruction; ending it cannot affect anything.
            TerminateProcess(tree.root_.get(), static_cast<UINT>(ExitCode::game_start));
            throw LauncherError(ExitCode::game_start, L"Cannot resume " + spec.application);
        }
        log.write(Level::info, L"started pid=" + std::to_wstring(info.dwProcessId) + L" job=" +
                                   (tree.attached_ ? L"yes" : L"no") + L" program=" + spec.application);
        return tree;
    }

    /// @brief Whether the started program is inside the job.
    [[nodiscard]] bool job_attached() const noexcept { return attached_; }
    /// @brief Whether the program was started through the shell (elevation required; CAPIE off).
    [[nodiscard]] bool shell_started() const noexcept { return shell_; }
    /// @brief The started program's identity.
    [[nodiscard]] ProcessStamp root_stamp() const noexcept { return root_stamp_; }
    /// @brief The started program's handle.
    [[nodiscard]] HANDLE root() const noexcept { return root_.get(); }
    /// @brief Time just before the program was created.
    [[nodiscard]] ULONGLONG started() const noexcept { return started_; }

    /**
     * @brief Whether an open process belongs to the tree.
     * @param process Handle with query access.
     * @return `true` for a job member, or for the started program itself.
     */
    [[nodiscard]] bool contains(HANDLE process) const noexcept {
        BOOL member = FALSE;
        if (attached_ && IsProcessInJob(process, job_.get(), &member) != 0 && member != FALSE) {
            return true;
        }
        return ProcessStamp{.pid = GetProcessId(process), .created = creation_time(process).value_or(1)} == root_stamp_;
    }

    /**
     * @brief PIDs of the current job members.
     * @return PIDs (empty when the job is not attached or cannot be read).
     */
    [[nodiscard]] std::vector<DWORD> member_pids() const {
        if (!attached_) {
            return {};
        }
        for (DWORD capacity = job_list_initial; capacity <= job_list_max; capacity *= 2) {
            const std::size_t bytes = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + (capacity * sizeof(ULONG_PTR));
            std::vector<unsigned char> storage(bytes);
            auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
            if (QueryInformationJobObject(job_.get(), JobObjectBasicProcessIdList, list, static_cast<DWORD>(bytes),
                                          nullptr) != 0 &&
                list->NumberOfProcessIdsInList == list->NumberOfAssignedProcesses) {
                std::vector<DWORD> pids;
                pids.reserve(list->NumberOfProcessIdsInList);
                for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
                    pids.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
                }
                return pids;
            }
        }
        return {};
    }

    /**
     * @brief Whether anything started by the launcher still runs.
     * @return Job: any active member; without job: the started program.
     */
    [[nodiscard]] bool active() const noexcept {
        if (attached_) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (QueryInformationJobObject(job_.get(), JobObjectBasicAccountingInformation, &accounting,
                                          sizeof(accounting), nullptr) != 0) {
                return accounting.ActiveProcesses > 0;
            }
        }
        return running(root_.get());
    }

private:
    ProcessTree() = default;

    Handle job_;
    Handle root_;
    ProcessStamp root_stamp_;
    ULONGLONG started_ = 0;
    bool attached_ = false;
    bool shell_ = false;
};

/// @brief Access the launcher needs on a game process.
inline constexpr DWORD game_access = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;

/**
 * @brief Opens a process and reads its identity.
 * @param pid PID.
 * @return Handle and stamp, or nothing.
 */
inline std::optional<std::pair<Handle, ProcessStamp>> open_stamped(DWORD pid) {
    Handle process(OpenProcess(game_access, FALSE, pid));
    if (!process) {
        return std::nullopt;
    }
    const auto created = creation_time(process.get());
    if (!created) {
        return std::nullopt;
    }
    return std::make_pair(std::move(process), ProcessStamp{.pid = pid, .created = *created});
}

/**
 * @brief Looks for a new game process in the tree.
 * @param tree The launcher's tree.
 * @param seen Games already handled (never returned again).
 * @return The game, or nothing.
 */
inline std::optional<BoundGame> find_game(const ProcessTree& tree, const std::vector<ProcessStamp>& seen) {
    const auto unseen = [&seen](const ProcessStamp& stamp) { return std::ranges::find(seen, stamp) == seen.end(); };
    for (const DWORD pid : tree.member_pids()) {
        auto opened = open_stamped(pid);
        if (!opened || !unseen(opened->second) || !tree.contains(opened->first.get())) {
            continue;  // gone, handled, or the PID now belongs to a process outside the job
        }
        auto image = image_path(opened->first.get());
        if (file_name_is(image, game_exe_name)) {
            return BoundGame{.process = std::move(opened->first),
                             .stamp = opened->second,
                             .image = std::move(image),
                             .via_job = true};
        }
    }
    const Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return std::nullopt;
    }
    std::unordered_map<DWORD, DWORD> parents;
    std::vector<DWORD> candidates;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more != FALSE;
         more = Process32NextW(snapshot.get(), &entry)) {
        parents[entry.th32ProcessID] = entry.th32ParentProcessID;
        if (_wcsicmp(entry.szExeFile, game_exe_name) == 0) {
            candidates.push_back(entry.th32ProcessID);
        }
    }
    const FactsFn facts = [&tree, &parents](DWORD pid) -> std::optional<ProcessFacts> {
        const auto parent = parents.find(pid);
        auto opened = open_stamped(pid);
        if (parent == parents.end() || !opened || !running(opened->first.get())) {
            return std::nullopt;
        }
        return ProcessFacts{
            .parent = parent->second, .created = opened->second.created, .in_tree = tree.contains(opened->first.get())};
    };
    for (const DWORD pid : candidates) {
        auto opened = open_stamped(pid);
        if (!opened || !unseen(opened->second) || opened->second.created < tree.started()) {
            continue;
        }
        const bool member = tree.contains(opened->first.get());
        if (member || descends_from_tree(pid, facts, max_ancestry_depth)) {
            auto image = image_path(opened->first.get());
            return BoundGame{.process = std::move(opened->first),
                             .stamp = opened->second,
                             .image = std::move(image),
                             .via_job = member};
        }
    }
    return std::nullopt;
}

}  // namespace capie::launcher
