/**
 * @file launcher_flow_test.cpp
 * @brief End-to-end launcher runs against fake dowser.exe, hoi4.exe and capie_injector.exe.
 *
 * Each scenario builds a scratch game folder (`dowser.exe`, `hoi4.exe`, `relay.exe`, all
 * copies of fake_process.exe), a scratch bundle (`capie_core.dll` and `capie_editor.dll`
 * are synthetic PE headers, `capie_injector.exe` is the fake) and a policy pinned to those
 * files, then calls `run_wrapped` exactly as the launcher's entry point does. The fakes
 * record what they saw (command line, folder, environment, PIDs, injector arguments) so the
 * test can check the binding and the injection order. The "game must always start"
 * scenarios (elevation fallback, CAPIE switched off up front, crash-loop breaker, a game
 * update while the launcher waits) use the same fakes. Usage:
 * `launcher_flow_test.exe SCRATCH_DIR FAKE_PROCESS_EXE`.
 */
#include "../../src/launcher/command_line.hpp"
#include "../../src/launcher/config.hpp"
#include "../../src/launcher/launcher_app.hpp"
#include "../../src/launcher/launcher_log.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace cl = capie::launcher;

int checks = 0;
int failures = 0;

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Printed on failure.
 */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label));
    }
}

/// @brief One line of the fakes' record file.
struct RecordLine {
    std::wstring role;   ///< dowser, relay, hoi4, injector
    DWORD pid = 0;       ///< process id
    std::wstring line;   ///< its command line
    std::wstring cwd;    ///< its working folder
    std::wstring mark;   ///< FAKE_MARK
    std::wstring extra;  ///< role-specific
};

/**
 * @brief Splits a string.
 * @param text Text.
 * @param separator Separator.
 * @return Parts.
 */
std::vector<std::wstring> split(const std::wstring& text, wchar_t separator) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (const wchar_t ch : text) {
        if (ch == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

/**
 * @brief Reads the record file.
 * @param path Record path.
 * @return Lines.
 */
std::vector<RecordLine> read_records(const std::wstring& path) {
    const std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::vector<RecordLine> lines;
    for (const auto& raw : split(cl::from_utf8(buffer.str()), L'\n')) {
        const auto parts = split(raw, L'|');
        if (parts.size() == 6) {
            lines.push_back({.role = parts[0],
                             .pid = static_cast<DWORD>(std::wcstoul(parts[1].c_str(), nullptr, 10)),
                             .line = parts[2],
                             .cwd = parts[3],
                             .mark = parts[4],
                             .extra = parts[5]});
        }
    }
    return lines;
}

/**
 * @brief Lines of one role.
 * @param lines All lines.
 * @param role Role.
 * @return Matching lines.
 */
std::vector<RecordLine> of_role(const std::vector<RecordLine>& lines, const wchar_t* role) {
    std::vector<RecordLine> answer;
    for (const auto& line : lines) {
        if (line.role == role) {
            answer.push_back(line);
        }
    }
    return answer;
}

/**
 * @brief Writes bytes to a file.
 * @param path Path.
 * @param bytes Content.
 */
void write_file(const std::wstring& path, const std::string& bytes) {
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/**
 * @brief Synthetic x64 PE DLL header with a distinguishing tail.
 * @param tag Distinguishing byte.
 * @return Bytes.
 */
std::string fake_dll(char tag) {
    constexpr LONG header_offset = 128;
    std::string bytes(1024, tag);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = header_offset;
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections = 1;
    nt.FileHeader.Characteristics = IMAGE_FILE_DLL;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    std::memcpy(bytes.data(), &dos, sizeof(dos));
    std::memcpy(bytes.data() + header_offset, &nt, sizeof(nt));
    return bytes;
}

/**
 * @brief SHA-256 of a file.
 * @param path Path.
 * @return Lower-case hex.
 */
std::string sha(const std::wstring& path) {
    return cl::PinnedFile::open(path).digest;
}

/// @brief One scenario's scratch layout and policy.
struct Fixture {
    std::wstring root;      ///< scenario folder
    std::wstring game_dir;  ///< fake game folder
    std::wstring record;    ///< fakes' record file
    cl::Policy policy;      ///< pinned to the fakes
};

/// @brief Environment variables a scenario may set (cleared before each scenario).
constexpr const wchar_t* fake_variables[] = {L"FAKE_DOWSER_MODE",      L"FAKE_GAME_MARKERS", L"FAKE_GAME_DELAY_MS",
                                             L"FAKE_GAME_LIFETIME_MS", L"FAKE_GAME_WINDOW",  L"FAKE_INJECT_FAIL_DLL",
                                             L"FAKE_INJECT_SLEEP_MS",  L"FAKE_INJECT_KILL_GAME"};

/**
 * @brief Builds a scenario folder.
 * @param base Scratch root.
 * @param name Scenario name.
 * @param fake fake_process.exe.
 * @return Fixture.
 */
Fixture make_fixture(const std::wstring& base, const std::wstring& name, const std::wstring& fake) {
    Fixture fixture;
    fixture.root = base + L"\\" + name;
    fixture.game_dir = fixture.root + L"\\Hearts of Iron IV";
    const auto runtime = fixture.root + L"\\bundle\\runtime";
    for (const auto& folder :
         {fixture.root, fixture.game_dir, fixture.root + L"\\bundle", runtime, fixture.root + L"\\logs"}) {
        CreateDirectoryW(folder.c_str(), nullptr);
    }
    for (const wchar_t* exe : {L"dowser.exe", L"hoi4.exe", L"relay.exe"}) {
        CopyFileW(fake.c_str(), (fixture.game_dir + L"\\" + exe).c_str(), FALSE);
    }
    CopyFileW(fake.c_str(), (runtime + L"\\capie_injector.exe").c_str(), FALSE);
    write_file(runtime + L"\\capie_core.dll", fake_dll('c'));
    write_file(runtime + L"\\capie_editor.dll", fake_dll('e'));
    fixture.record = fixture.root + L"\\record.txt";
    DeleteFileW(fixture.record.c_str());
    auto& policy = fixture.policy;
    policy.game_sha256 = sha(fixture.game_dir + L"\\hoi4.exe");
    policy.bundle_root = fixture.root + L"\\bundle";
    policy.bundle = {
        cl::BundleEntry{.name = L"capie_core.dll", .sha256 = sha(runtime + L"\\capie_core.dll"), .dll = true},
        cl::BundleEntry{.name = L"capie_editor.dll", .sha256 = sha(runtime + L"\\capie_editor.dll"), .dll = true},
        cl::BundleEntry{.name = L"capie_injector.exe", .sha256 = sha(runtime + L"\\capie_injector.exe"), .dll = false}};
    policy.game_log = fixture.root + L"\\logs\\game.log";
    policy.dlc_load = fixture.root + L"\\dlc_load.json";
    policy.capie_mod = {.workshop_id = {}, .descriptors = {"mod/capie.mod"}};
    policy.crash_sentinel = fixture.root + L"\\capie-inject.pending";
    DeleteFileW(policy.crash_sentinel.c_str());
    write_file(policy.dlc_load, R"({"enabled_mods":["mod/ugc_820260968.mod","mod/capie.mod"],"disabled_dlcs":[]})");
    policy.timeouts = {.poll_ms = 50,
                       .discovery_ms = 15000,
                       .readiness_ms = 20000,
                       .settle_ms = 300,
                       .window_probe_ms = 1000,
                       .helper_ms = 10000};
    for (const wchar_t* variable : fake_variables) {
        SetEnvironmentVariableW(variable, nullptr);
    }
    SetEnvironmentVariableW(L"FAKE_RECORD", fixture.record.c_str());
    SetEnvironmentVariableW(L"FAKE_GAME_LOG", policy.game_log.c_str());
    SetEnvironmentVariableW(L"FAKE_MARK", (L"mark-" + name).c_str());
    return fixture;
}

/// @brief Result of one run.
struct Run {
    cl::RunReport report;           ///< launcher summary
    std::vector<RecordLine> lines;  ///< fakes' records
    unsigned notices = 0;           ///< notices issued
    ULONGLONG elapsed_ms = 0;       ///< duration of run_wrapped
};

/**
 * @brief Runs the launcher logic on a fixture.
 * @param fixture Fixture.
 * @param program Program inside the game folder.
 * @param tail Raw argument tail.
 * @param game_exe hoi4.exe pinned before the start (default: the one in the game folder).
 * @param options CAPIE switched off up front, process creation.
 * @return Run result.
 */
Run run(const Fixture& fixture, const wchar_t* program, const std::wstring& tail, const std::wstring& game_exe = {},
        const cl::RunOptions& options = {}) {
    cl::Log log(fixture.root + L"\\launcher.log");
    cl::Notifier notifier(log, false);
    const auto path = fixture.game_dir + L"\\" + program;
    const cl::LaunchSpec spec{
        .application = path, .command_line = cl::wrapped_command_line(path, tail), .working_directory = {}};
    Run result;
    const ULONGLONG start = GetTickCount64();
    const auto pinned_game = game_exe.empty() ? fixture.game_dir + L"\\hoi4.exe" : game_exe;
    result.report = cl::run_wrapped(fixture.policy, spec, pinned_game, log, notifier, options);
    result.elapsed_ms = GetTickCount64() - start;
    Sleep(200);  // let detached fakes flush their last record line
    result.lines = read_records(fixture.record);
    result.notices = notifier.count();
    return result;
}

/**
 * @brief The status file a run left next to its log.
 * @param fixture Fixture.
 * @return Content as UTF-16.
 */
std::wstring status_of(const Fixture& fixture) {
    const std::ifstream file(fixture.root + L"\\last-status.txt", std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return cl::from_utf8(buffer.str());
}

/**
 * @brief Whether a file exists.
 * @param path Path.
 * @return `true` when it exists.
 */
bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

/**
 * @brief Creation that fails like a program whose manifest requires administrator rights.
 * @return FALSE with `ERROR_ELEVATION_REQUIRED`.
 */
BOOL needs_elevation(const cl::LaunchSpec& /*spec*/, std::wstring& /*line*/, STARTUPINFOW& /*startup*/,
                     PROCESS_INFORMATION& /*info*/) {
    SetLastError(ERROR_ELEVATION_REQUIRED);
    return FALSE;
}

/**
 * @brief Checks a run that loaded CAPIE into exactly one game.
 * @param result Run.
 * @param via_job Expected binding path.
 * @param label Scenario label.
 */
void expect_loaded_once(const Run& result, bool via_job, const char* label) {
    const auto games = of_role(result.lines, L"hoi4");
    const auto injections = of_role(result.lines, L"injector");
    const bool one = result.report.games.size() == 1 && games.size() == 1;
    check(one && result.report.games[0].outcome == cl::GameOutcome::loaded, label);
    check(one && result.report.games[0].via_job == via_job, label);
    check(one && result.report.games[0].stamp.pid == games[0].pid, label);
    const auto pid = one ? std::to_wstring(games[0].pid) : std::wstring{};
    check(injections.size() == 2 && injections[0].extra.starts_with(pid + L" ") &&
              injections[0].extra.ends_with(L"\\capie_core.dll") && injections[1].extra.starts_with(pid + L" ") &&
              injections[1].extra.ends_with(L"\\capie_editor.dll"),
          label);
}

}  // namespace

/**
 * @brief Entry point.
 * @param argc Argument count (3).
 * @param argv Scratch folder and fake process path.
 * @return 0 when every check passed.
 */
int wmain(int argc, wchar_t** argv) {  // NOLINT(misc-use-internal-linkage): process entry point
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    if (argc != 3) {
        static_cast<void>(std::printf("usage: launcher_flow_test SCRATCH FAKE_EXE\n"));
        return 2;
    }
    const std::wstring base = argv[1];
    const std::wstring fake = argv[2];
    CreateDirectoryW(base.c_str(), nullptr);
    try {
        {  // Steam form: dowser -> hoi4; arguments, folder and environment reach the program unchanged.
            const auto fixture = make_fixture(base, L"child", fake);
            const std::wstring tail = L" \"-- \" -debug_mode \"a\\\"b\" C:\\x\\ ";
            const auto result = run(fixture, L"dowser.exe", tail);
            expect_loaded_once(result, true, "dowser child: loaded via job, core then editor");
            const auto dowser = of_role(result.lines, L"dowser");
            std::vector<wchar_t> cwd(MAX_PATH);
            GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
            check(dowser.size() == 1 && dowser[0].line == L"\"" + fixture.game_dir + L"\\dowser.exe\"" + tail,
                  "wrapped command line verbatim");
            check(dowser.size() == 1 && dowser[0].cwd == cwd.data(), "working folder inherited");
            check(dowser.size() == 1 && dowser[0].mark == L"mark-child", "environment inherited");
            check(result.report.capie_enabled && result.notices == 0, "no notice on success");
            check(status_of(fixture).find(L"outcome=capie-loaded") != std::wstring::npos, "status: CAPIE loaded");
            check(!exists(fixture.policy.crash_sentinel), "clean injection removes the crash sentinel");
        }
        {  // CreateProcess says 740: the same program and arguments go through the shell, CAPIE off, game runs.
            auto fixture = make_fixture(base, L"elevated", fake);
            const std::wstring tail = L" -gdpr-compliant \"a b\"";
            const auto result = run(fixture, L"dowser.exe", tail, {}, {.capie_off = {}, .create = needs_elevation});
            const auto dowser = of_role(result.lines, L"dowser");
            check(result.report.shell_started && !result.report.capie_enabled, "elevation: started through the shell");
            check(dowser.size() == 1 && dowser[0].line.ends_with(tail) && dowser[0].mark == L"mark-elevated",
                  "elevation: same arguments and environment");
            check(of_role(result.lines, L"hoi4").size() == 1 && of_role(result.lines, L"injector").empty() &&
                      result.notices == 1,
                  "elevation: game runs without CAPIE, one notice");
            check(result.elapsed_ms >= 3500, "elevation: launcher waits for the shell-started program");
        }
        {  // Unknown program or internal error before the start: CAPIE off up front, nothing pinned, game runs.
            auto fixture = make_fixture(base, L"capie-off", fake);
            const cl::RunOptions off{.capie_off = cl::PendingNotice{cl::Notice::unknown_program, L"relay.exe"}};
            const auto result = run(fixture, L"relay.exe", L" -x", {}, off);
            check(!result.report.capie_enabled && result.report.games.empty() &&
                      of_role(result.lines, L"relay").size() == 1 && of_role(result.lines, L"hoi4").size() == 1 &&
                      of_role(result.lines, L"injector").empty() && result.notices == 1,
                  "CAPIE off up front: program and game run unchanged, one notice");
            check(status_of(fixture).find(L"outcome=game-without-capie") != std::wstring::npos &&
                      status_of(fixture).find(L"relay.exe") != std::wstring::npos,
                  "status: game without CAPIE, with the notice");
        }
        {  // Crash-loop breaker: a sentinel from the previous start skips CAPIE once, the next start loads.
            auto fixture = make_fixture(base, L"crash-guard", fake);
            check(cl::arm_sentinel(fixture.policy.crash_sentinel, "previous start"), "sentinel planted");
            const auto skipped = run(fixture, L"dowser.exe", L"");
            check(skipped.report.crash_guard && !skipped.report.capie_enabled &&
                      of_role(skipped.lines, L"injector").empty() && of_role(skipped.lines, L"hoi4").size() == 1 &&
                      skipped.notices == 1,
                  "sentinel left behind: game runs without CAPIE once, one notice");
            check(!exists(fixture.policy.crash_sentinel), "skipped start removes the sentinel");
            DeleteFileW(fixture.record.c_str());
            expect_loaded_once(run(fixture, L"dowser.exe", L""), true, "the start after the skip loads CAPIE again");
        }
        {  // The game dies inside injection: the sentinel stays, so the next start skips CAPIE.
            auto fixture = make_fixture(base, L"crash-inject", fake);
            SetEnvironmentVariableW(L"FAKE_INJECT_KILL_GAME", L"1");
            const auto crashed = run(fixture, L"dowser.exe", L"");
            check(crashed.report.games.size() == 1 && crashed.report.games[0].outcome == cl::GameOutcome::failed &&
                      of_role(crashed.lines, L"injector").size() == 1,
                  "game ended during injection: failed, editor never started");
            check(exists(fixture.policy.crash_sentinel), "game ended during injection: sentinel kept");
            SetEnvironmentVariableW(L"FAKE_INJECT_KILL_GAME", nullptr);
            DeleteFileW(fixture.record.c_str());
            const auto next = run(fixture, L"dowser.exe", L"");
            check(next.report.crash_guard && of_role(next.lines, L"injector").empty() &&
                      of_role(next.lines, L"hoi4").size() == 1,
                  "start after the crash: CAPIE skipped once, game runs");
        }
        {  // Steam updates hoi4.exe while the launcher waits for the running game: the pin does not block it.
            auto fixture = make_fixture(base, L"update", fake);
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"5000");
            const auto hoi4 = fixture.game_dir + L"\\hoi4.exe";
            const auto moved = fixture.game_dir + L"\\hoi4.exe.old";
            std::atomic<bool> renamed{false};
            std::thread steam([&] {
                const ULONGLONG until = GetTickCount64() + 15000;
                while (GetTickCount64() < until && of_role(read_records(fixture.record), L"injector").size() < 2) {
                    Sleep(50);
                }
                Sleep(200);
                renamed = MoveFileExW(hoi4.c_str(), moved.c_str(), 0) != 0;
                if (renamed) {
                    MoveFileExW(moved.c_str(), hoi4.c_str(), 0);
                }
            });
            const auto result = run(fixture, L"dowser.exe", L"");
            steam.join();
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::loaded,
                  "update: CAPIE loaded");
            check(renamed.load(), "update: hoi4.exe can be renamed while the launcher holds its pin");
        }
        {  // Same with CAPIE off (unknown build): no pin is held at all while the launcher waits.
            auto fixture = make_fixture(base, L"update-off", fake);
            fixture.policy.game_sha256 = std::string(64, '0');
            const auto runtime_dll = fixture.policy.bundle_root + L"\\runtime\\capie_editor.dll";
            std::atomic<bool> replaced{false};
            std::thread installer([&] {
                const ULONGLONG until = GetTickCount64() + 15000;
                while (GetTickCount64() < until && of_role(read_records(fixture.record), L"hoi4").empty()) {
                    Sleep(50);
                }
                Sleep(300);
                const cl::Handle writer(CreateFileW(runtime_dll.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                                    FILE_ATTRIBUTE_NORMAL, nullptr));
                replaced = static_cast<bool>(writer);
            });
            const auto result = run(fixture, L"dowser.exe", L"");
            installer.join();
            check(!result.report.capie_enabled && replaced.load(),
                  "CAPIE off: bundle pins released, the installer can update CAPIE meanwhile");
        }
        {
            auto fixture = make_fixture(base, L"grand", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"grandchild");
            expect_loaded_once(run(fixture, L"dowser.exe", L""), true, "grandchild through relay: via job");
        }
        {
            auto fixture = make_fixture(base, L"early", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"early");
            expect_loaded_once(run(fixture, L"dowser.exe", L""), true, "parent exits first: still via job");
        }
        {
            auto fixture = make_fixture(base, L"breakaway", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"breakaway");
            expect_loaded_once(run(fixture, L"dowser.exe", L""), false,
                               "game left the job: bound through the running parent chain");
        }
        {
            auto fixture = make_fixture(base, L"direct", fake);
            expect_loaded_once(run(fixture, L"hoi4.exe", L" -gdpr-compliant"), true, "hoi4.exe as the wrapped program");
        }
        {  // A game that left the job and whose parent is gone cannot be proven ours: untouched.
            auto fixture = make_fixture(base, L"orphan", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"orphan");
            // First look happens before dowser starts its game, the next one after dowser has
            // exited: the game is only ever seen without a running parent in the tree.
            fixture.policy.timeouts.poll_ms = 2000;
            const auto result = run(fixture, L"dowser.exe", L"");
            Sleep(4500);
            const auto lines = read_records(fixture.record);
            check(result.report.games.empty() && of_role(lines, L"injector").empty(), "orphaned game never injected");
            check(of_role(lines, L"hoi4").size() == 1, "orphaned game still ran");
        }
        {  // Unrelated hoi4.exe (same file) running before the launch is never chosen.
            auto fixture = make_fixture(base, L"unrelated", fake);
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"12000");
            const auto hoi4 = fixture.game_dir + L"\\hoi4.exe";
            std::wstring line = L"\"" + hoi4 + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION outside{};
            CreateProcessW(hoi4.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &outside);
            const cl::Handle outside_process(outside.hProcess);
            const cl::Handle outside_thread(outside.hThread);
            Sleep(500);
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"4000");
            const auto result = run(fixture, L"dowser.exe", L"");
            const auto injections = of_role(result.lines, L"injector");
            check(result.report.games.size() == 1 && result.report.games[0].stamp.pid != outside.dwProcessId,
                  "the launcher's own game is bound, not the older one");
            bool untouched = !injections.empty();
            for (const auto& injection : injections) {
                untouched = untouched && !injection.extra.starts_with(std::to_wstring(outside.dwProcessId) + L" ");
            }
            check(untouched, "no injection into the unrelated game");
            auto none = make_fixture(base, L"unrelated-none", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"none");
            const auto idle = run(none, L"dowser.exe", L"");
            check(idle.report.games.empty() && of_role(idle.lines, L"injector").empty(),
                  "player closes the Paradox Launcher: nothing bound although another game runs");
            WaitForSingleObject(outside_process.get(), INFINITE);
        }
        {  // Switch mod off in the playset: the game runs vanilla, nothing is injected, no notice.
            auto fixture = make_fixture(base, L"playset-off", fake);
            write_file(fixture.policy.dlc_load, R"({"enabled_mods":["mod/ugc_820260968.mod"],"disabled_dlcs":[]})");
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::playset_off &&
                      of_role(result.lines, L"injector").empty() && of_role(result.lines, L"hoi4").size() == 1 &&
                      result.notices == 0,
                  "CAPIE mod disabled: vanilla game, no injection, no notice");
            check(result.elapsed_ms >= 3500, "CAPIE mod disabled: launcher still lives until the game ends");
        }
        {  // Playset unreadable: fail-open to vanilla with one non-blocking notice.
            auto fixture = make_fixture(base, L"playset-missing", fake);
            DeleteFileW(fixture.policy.dlc_load.c_str());
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::playset_off &&
                      of_role(result.lines, L"injector").empty() && of_role(result.lines, L"hoi4").size() == 1 &&
                      result.notices == 1,
                  "dlc_load.json missing: vanilla game, one notice");
        }
        {
            auto fixture = make_fixture(base, L"playset-corrupt", fake);
            write_file(fixture.policy.dlc_load, R"({"enabled_mods":["mod/capie.mod")");
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::playset_off &&
                      of_role(result.lines, L"injector").empty() && result.notices == 1,
                  "corrupted dlc_load.json: vanilla game, one notice");
        }
        {  // The production identity: a subscribed Workshop item, as the Paradox Launcher lists it.
            auto fixture = make_fixture(base, L"playset-workshop", fake);
            fixture.policy.capie_mod = cl::capie_mod_identity();
            write_file(fixture.policy.dlc_load,
                       R"({"enabled_mods":["mod/ugc_820260968.mod","mod/ugc_3819929471.mod"],"disabled_dlcs":[]})");
            expect_loaded_once(run(fixture, L"dowser.exe", L""), true, "Workshop CAPIE mod enabled: loaded");
        }
        {
            auto fixture = make_fixture(base, L"unknown", fake);
            fixture.policy.game_sha256 = std::string(64, '0');
            const auto result = run(fixture, L"dowser.exe", L"");
            check(!result.report.capie_enabled && result.report.games.empty(), "unknown build: CAPIE off");
            check(of_role(result.lines, L"hoi4").size() == 1 && of_role(result.lines, L"injector").empty(),
                  "unknown build: game runs, no injector");
            check(result.notices == 1, "unknown build: one notice");
            check(status_of(fixture).find(cl::release_page_url) != std::wstring::npos,
                  "unknown build: status names the release page");
            check(result.elapsed_ms >= 3500, "unknown build: launcher lives until the game ends");
        }
        {
            auto fixture = make_fixture(base, L"bundle", fake);
            write_file(fixture.policy.bundle_root + L"\\runtime\\capie_editor.dll", fake_dll('X'));
            const auto result = run(fixture, L"dowser.exe", L"");
            check(!result.report.capie_enabled && of_role(result.lines, L"injector").empty() &&
                      of_role(result.lines, L"hoi4").size() == 1 && result.notices == 1,
                  "modified bundle: game runs without CAPIE, one notice");
        }
        {
            auto fixture = make_fixture(base, L"notready", fake);
            SetEnvironmentVariableW(L"FAKE_GAME_MARKERS", L"1");
            fixture.policy.timeouts.readiness_ms = 2000;
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::not_ready &&
                      of_role(result.lines, L"injector").empty() && result.notices == 1,
                  "one marker only: not ready, no injection");
        }
        {
            auto fixture = make_fixture(base, L"stale", fake);
            write_file(fixture.policy.game_log, "Executing History from 1\nExecuting History from 2\n");
            Sleep(100);
            SetEnvironmentVariableW(L"FAKE_GAME_MARKERS", L"0");
            fixture.policy.timeouts.readiness_ms = 2000;
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::not_ready &&
                      of_role(result.lines, L"injector").empty(),
                  "previous session's log is not readiness");
        }
        {
            auto fixture = make_fixture(base, L"nowindow", fake);
            SetEnvironmentVariableW(L"FAKE_GAME_WINDOW", L"0");
            fixture.policy.timeouts.readiness_ms = 2000;
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::not_ready &&
                      of_role(result.lines, L"injector").empty(),
                  "log without a window is not readiness");
        }
        {
            auto fixture = make_fixture(base, L"exits", fake);
            SetEnvironmentVariableW(L"FAKE_GAME_MARKERS", L"0");
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"800");
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::exited_early &&
                      of_role(result.lines, L"injector").empty() && result.notices == 0,
                  "game closed in the loading screen: no injection, no notice");
        }
        {
            auto fixture = make_fixture(base, L"corefail", fake);
            SetEnvironmentVariableW(L"FAKE_INJECT_FAIL_DLL", L"capie_core.dll");
            const auto result = run(fixture, L"dowser.exe", L"");
            const auto injections = of_role(result.lines, L"injector");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::failed &&
                      injections.size() == 1 && injections[0].extra.ends_with(L"\\capie_core.dll") &&
                      result.notices == 1,
                  "core refused: editor never started, one notice");
            check(!exists(fixture.policy.crash_sentinel), "refused step with the game alive: no crash sentinel");
        }
        {
            auto fixture = make_fixture(base, L"editorfail", fake);
            SetEnvironmentVariableW(L"FAKE_INJECT_FAIL_DLL", L"capie_editor.dll");
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::failed &&
                      of_role(result.lines, L"injector").size() == 2,
                  "editor refused: failure reported after both steps");
        }
        {  // Outer deadline: the launcher stops waiting but keeps the injector's handles until it ends.
            auto fixture = make_fixture(base, L"timeout", fake);
            SetEnvironmentVariableW(L"FAKE_INJECT_SLEEP_MS", L"6000");
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"3000");
            fixture.policy.timeouts.helper_ms = 500;
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::failed &&
                      of_role(result.lines, L"injector").size() == 1,
                  "injector timeout: failed, editor never started");
            check(result.elapsed_ms >= 6000, "guardian waited for the late injector to end on its own");
        }
        {
            auto fixture = make_fixture(base, L"twice", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"twice");
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"2500");
            const auto result = run(fixture, L"dowser.exe", L"");
            const auto games = of_role(result.lines, L"hoi4");
            check(result.report.games.size() == 2 && games.size() == 2 &&
                      result.report.games[0].outcome == cl::GameOutcome::loaded &&
                      result.report.games[1].outcome == cl::GameOutcome::loaded &&
                      of_role(result.lines, L"injector").size() == 4,
                  "second game from the same Paradox Launcher: handled again");
        }
        {  // The game in the tree is a different file (same bytes) than the hoi4.exe pinned before the start.
            auto fixture = make_fixture(base, L"foreign", fake);
            const auto other = fixture.root + L"\\other";
            CreateDirectoryW(other.c_str(), nullptr);
            CopyFileW((fixture.game_dir + L"\\hoi4.exe").c_str(), (other + L"\\hoi4.exe").c_str(), FALSE);
            const auto result = run(fixture, L"dowser.exe", L"", other + L"\\hoi4.exe");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::foreign_image &&
                      of_role(result.lines, L"injector").empty() && result.notices == 1,
                  "game image is not the pinned file: no injection, one notice");
        }
        {  // A second game started while the first runs is never touched, even after the first ends.
            auto fixture = make_fixture(base, L"parallel", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"parallel");
            SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"3000");
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.games.size() == 1 && result.report.games[0].outcome == cl::GameOutcome::loaded &&
                      of_role(result.lines, L"hoi4").size() == 2 && of_role(result.lines, L"injector").size() == 2,
                  "concurrent second game ignored");
        }
        {
            auto fixture = make_fixture(base, L"discovery", fake);
            SetEnvironmentVariableW(L"FAKE_DOWSER_MODE", L"none");
            fixture.policy.timeouts.discovery_ms = 300;
            const auto result = run(fixture, L"dowser.exe", L"");
            check(result.report.discovery_expired && result.report.games.empty(), "no game in time: search ends");
        }
    } catch (const std::exception& error) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception: %s\n", error.what()));
    }
    static_cast<void>(std::printf("launcher flow tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
