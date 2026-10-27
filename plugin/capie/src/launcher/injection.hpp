/**
 * @file injection.hpp
 * @brief Loading CAPIE into the bound game: `capie_injector.exe PID ABS_PATH`, core first, then editor.
 *
 * This is the accepted launcher's injection sequence with the CAPIE file names. The
 * launcher does not inject anything itself; it runs the reviewed `capie_injector.exe`
 * twice, first with `capie_core.dll`, then with `capie_editor.dll`, and continues only
 * when the previous run ended on its own with exit code 0 and the game still passes the
 * identity check. The injector repeats its own checks (pinned game hash, accepted DLL
 * names, already-loaded refusal).
 *
 * Identity check before each step: the game process is alive, has the same PID and
 * creation time, its image is the pinned hoi4.exe (same file id), and every pinned path
 * (game and bundle) still names the pinned file.
 *
 * Each injector run inherits only an explicit list of handles: duplicates of the four file
 * pins (so the files stay pinned while the injector lives, even beyond the launcher's
 * outer deadline) and the write end of its diagnostic pipe. It never sees the game handle
 * or anything else of the launcher. Its output is drained without blocking and kept up to
 * 16 KiB for the log.
 *
 * After any failure the launcher logs and notifies once and then becomes a quiet guardian:
 * it keeps every handle until the game and every injector it already started have ended
 * by themselves. It does not retry, does not start the remaining step, unloads nothing and
 * terminates nothing.
 */
#pragma once
#include "command_line.hpp"
#include "config.hpp"
#include "file_pin.hpp"
#include "launcher_log.hpp"
#include "process_tree.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace capie::launcher {

/// @brief Injector steps: core DLL, then editor DLL.
inline constexpr std::array<std::size_t, 2> injection_order = {bundle_core, bundle_editor};
/// @brief Handles inherited by the injector: game pin, three bundle pins, pipe.
inline constexpr std::size_t inherited_count = 1 + bundle_size + 1;
/// @brief Pipe read chunk.
inline constexpr DWORD pipe_chunk = 4096;
/// @brief Pipe reads per drain call.
inline constexpr unsigned pipe_reads_per_drain = 8;
/// @brief Injector poll period.
inline constexpr DWORD helper_poll_ms = 50;
/// @brief Exit code stored when no exit code could be read.
inline constexpr DWORD unknown_helper_code = 99;

/// @brief How an injector run ended.
enum class HelperState : std::uint8_t { completed, timed_out, failed_wait, not_created };

/// @brief Result of one injector run.
struct HelperResult {
    HelperState state = HelperState::not_created;  ///< how it ended
    DWORD code = unknown_helper_code;              ///< exit code when completed
    std::string output;                            ///< bounded diagnostic output
};

/**
 * @brief Whether the next step may start.
 * @param result Previous injector run.
 * @param own_game_alive Identity check after the run.
 * @return `true` only for a natural exit with code 0 and an intact game.
 */
constexpr bool may_continue(const HelperResult& result, bool own_game_alive) noexcept {
    return result.state == HelperState::completed && result.code == 0 && own_game_alive;
}

/// @brief PROC_THREAD_ATTRIBUTE_HANDLE_LIST owner.
class AttributeList {
public:
    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;
    AttributeList(AttributeList&&) = delete;
    AttributeList& operator=(AttributeList&&) = delete;
    /**
     * @brief Builds the list.
     * @param handles Inheritable handles the child may receive.
     * @throws LauncherError (helper_failure).
     */
    explicit AttributeList(std::array<HANDLE, inherited_count>& handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0) {
            throw LauncherError(ExitCode::helper_failure, L"Cannot size the injector handle list.");
        }
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (InitializeProcThreadAttributeList(list_, 1, 0, &bytes) == 0) {
            list_ = nullptr;
            throw LauncherError(ExitCode::helper_failure, L"Cannot create the injector handle list.");
        }
        if (UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, static_cast<void*>(handles.data()),
                                      sizeof(handles), nullptr, nullptr) == 0) {
            DeleteProcThreadAttributeList(list_);
            list_ = nullptr;
            throw LauncherError(ExitCode::helper_failure, L"Cannot restrict the injector's inherited handles.");
        }
    }
    ~AttributeList() {
        if (list_ != nullptr) {
            DeleteProcThreadAttributeList(list_);
        }
    }
    /// @brief The list.
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }

private:
    std::vector<unsigned char> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

/**
 * @brief Reads what is available on a pipe without blocking, keeping at most the output limit.
 * @param pipe Read end.
 * @param output Accumulated output.
 */
inline void drain_pipe(HANDLE pipe, std::string& output) {
    std::array<char, pipe_chunk> data{};
    for (unsigned i = 0; i < pipe_reads_per_drain; ++i) {
        DWORD available = 0;
        if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == 0 || available == 0) {
            return;
        }
        DWORD actual = 0;
        if (ReadFile(pipe, data.data(), std::min(available, pipe_chunk), &actual, nullptr) == 0 || actual == 0) {
            return;
        }
        const auto kept = std::min(static_cast<std::size_t>(actual), helper_output_limit - output.size());
        output.append(data.data(), kept);  // keep draining after truncation: never block the child
    }
}

/// @brief One bound game and the CAPIE files, during injection.
class InjectionSession {
public:
    /**
     * @brief Creates the session.
     * @param game_pin Pinned hoi4.exe (outlives the session).
     * @param bundle Pinned bundle (outlives the session).
     * @param game Bound game (outlives the session).
     */
    InjectionSession(const PinnedFile& game_pin, const Bundle& bundle, const BoundGame& game)
        : game_pin_(game_pin), bundle_(bundle), game_(game) {}

    /**
     * @brief Identity check of the game and every pinned file.
     * @return `false` on any difference or error.
     */
    [[nodiscard]] bool intact() const noexcept {
        try {
            if (!running(game_.process.get()) || GetProcessId(game_.process.get()) != game_.stamp.pid ||
                creation_time(game_.process.get()).value_or(0) != game_.stamp.created || !game_pin_.still_same_path()) {
                return false;
            }
            const Handle image(CreateFileW(image_path(game_.process.get()).c_str(), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL, nullptr));
            return image && file_id(image.get()) == game_pin_.identity &&
                   std::ranges::all_of(bundle_, [](const PinnedFile& file) { return file.still_same_path(); });
        } catch (...) {
            return false;
        }
    }

    /**
     * @brief Runs the injector for one DLL and waits for it, bounded.
     * @param step Index into `injection_order`.
     * @param timeout_ms Outer deadline.
     * @return Result; the injector handle stays owned by the session.
     * @throws LauncherError (helper_failure) when the run cannot be prepared.
     */
    HelperResult run_injector(std::size_t step, DWORD timeout_ms) {
        if (!intact()) {
            throw LauncherError(ExitCode::helper_failure, L"The game did not pass the identity check.");
        }
        const auto& injector = bundle_[bundle_injector];
        const auto& dll = bundle_[injection_order.at(step)];
        auto line = command_line({injector.path, std::to_wstring(game_.stamp.pid), dll.path});
        std::array<Handle, inherited_count - 1> pins;
        std::array<HANDLE, inherited_count> inherited{};
        for (std::size_t i = 0; i < pins.size(); ++i) {
            void* const original = i == 0 ? game_pin_.file.get() : bundle_.at(i - 1).file.get();
            HANDLE duplicate = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, TRUE,
                                DUPLICATE_SAME_ACCESS) == 0) {
                throw LauncherError(ExitCode::helper_failure, L"Cannot pin the files for the injector.");
            }
            pins.at(i).reset(duplicate);
            inherited.at(i) = duplicate;
        }
        SECURITY_ATTRIBUTES security{
            .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
        HANDLE read = nullptr;
        HANDLE write = nullptr;
        if (CreatePipe(&read, &write, &security, 0) == 0) {
            throw LauncherError(ExitCode::helper_failure, L"Cannot create the injector pipe.");
        }
        const Handle pipe_read(read);
        Handle pipe_write(write);
        if (SetHandleInformation(pipe_read.get(), HANDLE_FLAG_INHERIT, 0) == 0) {
            throw LauncherError(ExitCode::helper_failure, L"Cannot restrict the injector pipe.");
        }
        inherited.back() = pipe_write.get();
        const AttributeList attributes(inherited);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = pipe_write.get();
        startup.StartupInfo.hStdError = pipe_write.get();
        startup.lpAttributeList = attributes.get();
        if (!intact()) {  // again, after every allocation of the preparation
            throw LauncherError(ExitCode::helper_failure, L"The game or a file changed before the injector.");
        }
        const auto folder = std::filesystem::path(injector.path).parent_path().wstring();
        PROCESS_INFORMATION child{};
        if (CreateProcessW(injector.path.c_str(), line.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, folder.c_str(),
                           &startup.StartupInfo, &child) == 0) {
            throw LauncherError(ExitCode::helper_failure,
                                L"Cannot start the injector." + windows_error(GetLastError()));
        }
        helpers_.at(step).reset(child.hProcess);  // the guardian owns it from here on
        const Handle thread(child.hThread);
        pipe_write.reset();
        return wait_injector(helpers_.at(step).get(), pipe_read.get(), timeout_ms);
    }

    /**
     * @brief Keeps every handle until the game and every started injector ended on their own.
     * @note Never terminates anything; can wait indefinitely for a hung injector (documented limit).
     */
    void guardian() const noexcept {
        std::array<HANDLE, injection_order.size() + 1> handles{};
        DWORD count = 0;
        if (game_.process) {
            handles.at(count++) = game_.process.get();
        }
        for (const auto& helper : helpers_) {
            if (helper) {
                handles.at(count++) = helper.get();
            }
        }
        while (count > 0 && WaitForMultipleObjects(count, handles.data(), TRUE, INFINITE) == WAIT_FAILED) {
            Sleep(helper_poll_ms);
        }
    }

    /// @brief The bound game.
    [[nodiscard]] const BoundGame& game() const noexcept { return game_; }

private:
    /**
     * @brief Waits for an injector, draining its pipe.
     * @param process Injector process.
     * @param pipe Read end.
     * @param timeout_ms Outer deadline.
     * @return Result.
     */
    static HelperResult wait_injector(HANDLE process, HANDLE pipe, DWORD timeout_ms) {
        HelperResult result;
        const ULONGLONG started = GetTickCount64();
        for (;;) {
            drain_pipe(pipe, result.output);
            const DWORD wait = WaitForSingleObject(process, helper_poll_ms);
            if (wait == WAIT_OBJECT_0) {
                drain_pipe(pipe, result.output);
                result.state =
                    GetExitCodeProcess(process, &result.code) != 0 ? HelperState::completed : HelperState::failed_wait;
                return result;
            }
            if (wait != WAIT_TIMEOUT) {
                result.state = HelperState::failed_wait;
                return result;
            }
            if (GetTickCount64() - started >= timeout_ms) {
                result.state = HelperState::timed_out;
                return result;
            }
        }
    }

    const PinnedFile& game_pin_;
    const Bundle& bundle_;
    const BoundGame& game_;
    std::array<Handle, injection_order.size()> helpers_;
};

/**
 * @brief Log text for an injector result.
 * @param result Result.
 * @return Description with the bounded output.
 */
inline std::wstring describe(const HelperResult& result) {
    std::wstring text;
    switch (result.state) {
        case HelperState::completed:
            text = L"injector exit code " + std::to_wstring(result.code);
            break;
        case HelperState::timed_out:
            text = L"injector timed out; state uncertain";
            break;
        case HelperState::failed_wait:
            text = L"injector wait failed";
            break;
        case HelperState::not_created:
            text = L"injector not started";
            break;
    }
    if (!result.output.empty()) {
        text += L": " + from_utf8(result.output);
    }
    return text;
}

}  // namespace capie::launcher
