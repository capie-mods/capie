/**
 * @file launcher_unit_test.cpp
 * @brief Component tests of the CAPIE launcher: command lines, file pins, game location, readiness, ancestry.
 *
 * Pure functions are driven with synthetic data; Windows behaviour (argument parsing,
 * file sharing, job membership) is checked against the real API with files and processes
 * the test creates itself in its scratch folder. No game, Steam or injector is involved.
 * Usage: `launcher_unit_test.exe SCRATCH_DIR FAKE_PROCESS_EXE`.
 */
#include "../../src/launcher/command_line.hpp"
#include "../../src/launcher/config.hpp"
#include "../../src/launcher/file_pin.hpp"
#include "../../src/launcher/game_location.hpp"
#include "../../src/launcher/injection.hpp"
#include "../../src/launcher/launcher_log.hpp"
#include "../../src/launcher/playset_gate.hpp"
#include "../../src/launcher/process_tree.hpp"
#include "../../src/launcher/readiness.hpp"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <shellapi.h>
#include <stdexcept>
#include <string>
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

/**
 * @brief Whether a callable throws LauncherError with a given code.
 * @param body Callable.
 * @param code Expected code.
 * @return `true` when it threw that code.
 */
template <typename Body>
bool throws_code(Body body, cl::ExitCode code) {
    try {
        body();
    } catch (const cl::LauncherError& error) {
        return error.code() == code;
    }
    return false;
}

/**
 * @brief Splits a full command line with the system parser.
 * @param line Command line.
 * @return Arguments.
 */
std::vector<std::wstring> system_split(const std::wstring& line) {
    int count = 0;
    LPWSTR* parts = CommandLineToArgvW(line.c_str(), &count);
    std::vector<std::wstring> answer;
    for (int i = 0; parts != nullptr && i < count; ++i) {
        answer.emplace_back(parts[i]);
    }
    LocalFree(static_cast<void*>(parts));
    return answer;
}

/**
 * @brief Writes a file.
 * @param path Path.
 * @param bytes Content.
 */
void write_file(const std::wstring& path, const std::string& bytes) {
    const cl::Handle file(
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD written = 0;
    check(file && WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != 0 &&
              written == bytes.size(),
          "fixture write");
}

/// @brief Tokenizer must agree with CommandLineToArgvW.
void tokenizer_matches_system() {
    const std::array<std::wstring, 14> tails = {
        L"",
        L" a b  c",
        L" \"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Hearts of Iron IV\\dowser.exe\" \"-- \"",
        L" \"a\\\"b\" c",
        L" slash\\\\\"quote\" x",
        L" two\\\\\\\"q\"",
        L" ends\\\\",
        L" \"\" empty",
        L" \"a\"\"b\" c",
        L" \"unterminated arg",
        L" \t tab\tsep ",
        L" Zażółć \"gęślą jaźń\"",
        L" &|;`$() %PATH%",
        L" a\"b c\"d e"};
    for (const auto& tail : tails) {
        const std::wstring line = L"C:\\x\\launcher.exe" + tail;
        const auto expected = system_split(line);
        const auto tokens = cl::tokenize(line, std::wstring_view(L"C:\\x\\launcher.exe").size());
        bool same = tokens.size() + 1 == expected.size();
        for (std::size_t i = 0; same && i < tokens.size(); ++i) {
            same = tokens[i].value == expected[i + 1];
        }
        check(same, "tokenizer agrees with CommandLineToArgvW");
        if (!same) {
            static_cast<void>(std::printf("  case: %s\n", cl::to_utf8(tail).c_str()));
            for (const auto& token : tokens) {
                static_cast<void>(std::printf("  ours: [%s]\n", cl::to_utf8(token.value).c_str()));
            }
            for (const auto& part : expected) {
                static_cast<void>(std::printf("  system: [%s]\n", cl::to_utf8(part).c_str()));
            }
        }
    }
}

/**
 * @brief The wrapped command of a parsed invocation.
 * @param invocation Parsed command line.
 * @return The wrapped command.
 * @throws std::runtime_error when there is none (counted as a failure by main).
 */
template <typename Invocation>
auto& wrapped_of(Invocation& invocation) {
    if (!invocation.wrapped.has_value()) {
        throw std::runtime_error("no wrapped command");
    }
    return *invocation.wrapped;
}

/// @brief Option and wrapped-command parsing.
void invocation_parsing() {
    const std::wstring steam =
        L"\"C:\\Users\\Q A\\AppData\\Local\\RenameAnything\\current\\RenameAnythingLauncher.exe\" "
        L"\"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Hearts of Iron IV\\dowser.exe\" \"-- \" -debug_mode";
    const auto parsed = cl::parse_invocation(steam);
    check(parsed.wrapped.has_value(), "steam form is wrapper mode");
    check(wrapped_of(parsed).program ==
              L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Hearts of Iron IV\\dowser.exe",
          "program decoded");
    check(wrapped_of(parsed).tail == L" \"-- \" -debug_mode", "tail verbatim");
    const auto rebuilt = cl::wrapped_command_line(wrapped_of(parsed).program, wrapped_of(parsed).tail);
    const auto original_args = system_split(steam);
    const auto rebuilt_args = system_split(rebuilt);
    check(rebuilt_args.size() + 1 == original_args.size() &&
              std::equal(rebuilt_args.begin(), rebuilt_args.end(), original_args.begin() + 1),
          "child sees the same argv as under Steam");
    check(cl::wrapped_command_line(L"C:\\g\\hoi4.exe", L"") == L"\"C:\\g\\hoi4.exe\"", "no tail");
    check(cl::wrapped_command_line(L"C:\\g\\hoi4.exe", L"-x") == L"\"C:\\g\\hoi4.exe\" -x", "tail separated");
    check(throws_code([] { static_cast<void>(cl::wrapped_command_line(L"C:\\g\"\\hoi4.exe", L"")); },
                      cl::ExitCode::usage),
          "quote in program refused");
    check(
        throws_code([] { static_cast<void>(cl::wrapped_command_line(L"C:\\g\\hoi4.exe", std::wstring(33000, L'x'))); },
                    cl::ExitCode::usage),
        "over-long command refused");

    const auto options = cl::parse_invocation(L"l.exe --no-dialogs C:\\g\\hoi4.exe --help");
    check(options.options.no_dialogs && options.wrapped && wrapped_of(options).tail == L" --help",
          "options only before the program; later -- tokens belong to the game");
    check(cl::parse_invocation(L"l.exe").options.game.empty() && !cl::parse_invocation(L"l.exe").wrapped,
          "no arguments = shortcut mode");
    const auto verify = cl::parse_invocation(L"l.exe --game \"C:\\a b\\hoi4.exe\" --verify-only");
    check(verify.options.verify_only && verify.options.game == L"C:\\a b\\hoi4.exe", "verify-only parsed");
    check(cl::parse_invocation(L"\"C:\\x y\\l.exe\" --help").options.help, "help parsed");
    for (const wchar_t* bad : {L"l.exe --bogus", L"l.exe --help --help", L"l.exe --help --no-dialogs",
                               L"l.exe --verify-only", L"l.exe --game C:\\g\\hoi4.exe C:\\g\\dowser.exe",
                               L"l.exe --game", L"l.exe --verify-only --game C:\\g\\hoi4.exe C:\\g\\hoi4.exe",
                               L"l.exe --no-dialogs --no-dialogs", L"l.exe --help C:\\g\\hoi4.exe"}) {
        check(throws_code([bad] { static_cast<void>(cl::parse_invocation(bad)); }, cl::ExitCode::usage),
              "contradictory or unknown options refused");
    }
}

/// @brief Unquoted program paths with spaces.
void unquoted_resolution() {
    const std::wstring raw = L"l.exe C:\\Program Files\\HOI 4\\dowser.exe -- x";
    auto parsed = cl::parse_invocation(raw);
    const cl::ExistsFn exists = [](const std::wstring& path) {
        return path == L"C:\\Program Files\\HOI 4\\dowser.exe";
    };
    cl::resolve_program(raw, wrapped_of(parsed), exists);
    check(wrapped_of(parsed).program == L"C:\\Program Files\\HOI 4\\dowser.exe" && wrapped_of(parsed).tail == L" -- x",
          "unquoted path with spaces resolved to the existing dowser.exe");
    auto other = cl::parse_invocation(L"l.exe C:\\Program Files\\evil.exe C:\\g\\dowser.exe");
    cl::resolve_program(L"l.exe C:\\Program Files\\evil.exe C:\\g\\dowser.exe", wrapped_of(other),
                        [](const std::wstring&) { return true; });
    check(wrapped_of(other).program == L"C:\\Program Files\\evil.exe C:\\g\\dowser.exe",
          "only a prefix ending in dowser.exe/hoi4.exe can be chosen");
    auto quoted = cl::parse_invocation(L"l.exe \"C:\\a b\\x.exe\" C:\\g\\hoi4.exe");
    cl::resolve_program(L"l.exe \"C:\\a b\\x.exe\" C:\\g\\hoi4.exe", wrapped_of(quoted),
                        [](const std::wstring&) { return true; });
    check(wrapped_of(quoted).program == L"C:\\a b\\x.exe", "quoted program never re-resolved");
    auto mixed = cl::parse_invocation(L"l.exe C:\\a \"b\\dowser.exe");
    cl::resolve_program(L"l.exe C:\\a \"b\\dowser.exe", wrapped_of(mixed), [](const std::wstring&) { return true; });
    check(wrapped_of(mixed).program == L"C:\\a", "a candidate that crosses a quote is never chosen");
    auto none = cl::parse_invocation(L"l.exe C:\\a b\\dowser.exe");
    cl::resolve_program(L"l.exe C:\\a b\\dowser.exe", wrapped_of(none), [](const std::wstring&) { return false; });
    check(wrapped_of(none).program == L"C:\\a", "nothing existing: program unchanged (refused later)");
    check(cl::names_game_program(L"C:\\G\\DOWSER.EXE") && cl::names_game_program(L"c:\\g\\Hoi4.exe") &&
              !cl::names_game_program(L"C:\\g\\hoi4.exe.bat") && !cl::names_game_program(L"C:\\g\\xhoi4.exe") &&
              !cl::names_game_program(L"hoi4.exe"),
          "game program names");
}

/// @brief quote_arg / command_line round trip (accepted launcher cases).
void argument_round_trip() {
    const std::vector<std::wstring> expected = {L"C:\\Program Files\\capie_injector.exe",
                                                L"",
                                                L"simple",
                                                L"C:\\Users\\Zolc QA\\hoi4.exe",
                                                L"C:\\Zażółć gęślą\\",
                                                L"a\"b",
                                                L"slash\\\"quote",
                                                L"two\\\\\"quotes\"",
                                                L"ends\\\\",
                                                L"&|;`$() %PATH%"};
    auto line = cl::command_line(expected);
    check(system_split(line.data()) == expected, "argv round trip");
    check(throws_code([] { static_cast<void>(cl::command_line({std::wstring(L"a\0b", 3)})); }, cl::ExitCode::usage),
          "NUL refused");
    check(cl::absolute_local_path(L"C:\\game\\hoi4.exe") && cl::absolute_local_path(L"\\\\?\\C:\\game\\hoi4.exe") &&
              !cl::absolute_local_path(L"hoi4.exe") && !cl::absolute_local_path(L"C:hoi4.exe") &&
              !cl::absolute_local_path(L"\\\\server\\share\\hoi4.exe"),
          "absolute local paths only");
}

/**
 * @brief A minimal AMD64 PE header file.
 * @param dll Set IMAGE_FILE_DLL.
 * @param machine Machine field.
 * @return Bytes.
 */
std::string pe_bytes(bool dll, WORD machine) {
    constexpr LONG header_offset = 128;
    std::string bytes(1024, '\0');
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = header_offset;
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = machine;
    nt.FileHeader.NumberOfSections = 1;
    nt.FileHeader.Characteristics = dll ? IMAGE_FILE_DLL : 0;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    std::memcpy(bytes.data(), &dos, sizeof(dos));
    std::memcpy(bytes.data() + header_offset, &nt, sizeof(nt));
    return bytes;
}

/// @brief Pins block writers, hash the pinned bytes, detect replacement.
void file_pins(const std::wstring& dir) {
    const auto path = dir + L"\\pin.txt";
    write_file(path, "abc");
    {
        auto pin = cl::PinnedFile::open(path);
        check(pin.digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "same-handle SHA-256");
        check(cl::hash_handle(pin.file.get()) == pin.digest, "hash twice through one handle");
        check(pin.still_same_path(), "path still names the pinned file");
        const cl::Handle writer(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
        check(!writer && GetLastError() == ERROR_SHARING_VIOLATION, "pin blocks writers");
        check(DeleteFileW(path.c_str()) == 0, "pin blocks delete");
        check(MoveFileExW(path.c_str(), (dir + L"\\moved.txt").c_str(), 0) == 0, "pin blocks rename");
        DWORD flags = 0;
        check(GetHandleInformation(pin.file.get(), &flags) != 0 && (flags & HANDLE_FLAG_INHERIT) == 0,
              "pin is not inheritable");
        check(!cl::pe_x64(pin.file.get(), false), "text is not a PE");
    }
    check(DeleteFileW(path.c_str()) != 0, "file free after the pin closes");
    write_file(path, "abc");
    auto pin = cl::PinnedFile::open(path);
    pin.file.reset();
    DeleteFileW(path.c_str());
    write_file(path, "abc");
    check(!pin.still_same_path(), "a replaced file (same content, new identity) is detected");
    check(throws_code([&] { static_cast<void>(cl::PinnedFile::open(dir + L"\\missing.txt")); },
                      cl::ExitCode::invalid_bundle),
          "missing file refused");
    check(throws_code([] { static_cast<void>(cl::PinnedFile::open(L"relative.txt")); }, cl::ExitCode::usage),
          "relative path refused");

    const auto pe = dir + L"\\image.bin";
    write_file(pe, pe_bytes(true, IMAGE_FILE_MACHINE_AMD64));
    {
        const auto image = cl::PinnedFile::open(pe);
        check(cl::pe_x64(image.file.get(), true) && !cl::pe_x64(image.file.get(), false), "DLL kind enforced");
    }
    write_file(pe, pe_bytes(false, IMAGE_FILE_MACHINE_I386));
    {
        const auto image = cl::PinnedFile::open(pe);
        check(!cl::pe_x64(image.file.get(), false), "x86 refused");
    }
    write_file(pe, pe_bytes(false, IMAGE_FILE_MACHINE_AMD64).substr(0, 200));
    {
        const auto image = cl::PinnedFile::open(pe);
        check(!cl::pe_x64(image.file.get(), false), "truncated PE refused");
    }
    write_file(pe, pe_bytes(false, IMAGE_FILE_MACHINE_AMD64));
    const auto digest = cl::PinnedFile::open(pe).digest;
    check(cl::verify_file(pe, digest, false, cl::ExitCode::unsupported_game).digest == digest, "verify accepts");
    check(throws_code(
              [&] {
                  static_cast<void>(cl::verify_file(pe, std::string(64, '0'), false, cl::ExitCode::unsupported_game));
              },
              cl::ExitCode::unsupported_game),
          "verify refuses another hash with the given code");
    check(throws_code([&] { static_cast<void>(cl::verify_file(pe, digest, true, cl::ExitCode::invalid_bundle)); },
                      cl::ExitCode::invalid_bundle),
          "verify refuses the wrong PE kind");
}

/// @brief Wrapped program validation and shortcut-mode helpers.
void game_location(const std::wstring& dir) {
    const auto game_dir = dir + L"\\Hearts of Iron IV";
    CreateDirectoryW(game_dir.c_str(), nullptr);
    write_file(game_dir + L"\\dowser.exe", "x");
    check(throws_code([&] { static_cast<void>(cl::validate_wrapped_program(game_dir + L"\\dowser.exe")); },
                      cl::ExitCode::usage),
          "dowser.exe without hoi4.exe refused");
    write_file(game_dir + L"\\hoi4.exe", "x");
    write_file(game_dir + L"\\other.exe", "x");
    check(cl::validate_wrapped_program(game_dir + L"\\dowser.exe") == game_dir + L"\\hoi4.exe", "dowser accepted");
    check(cl::validate_wrapped_program(game_dir + L"\\HOI4.EXE") == game_dir + L"\\hoi4.exe", "hoi4 accepted");
    check(throws_code([&] { static_cast<void>(cl::validate_wrapped_program(game_dir + L"\\other.exe")); },
                      cl::ExitCode::usage),
          "other program refused");
    check(throws_code([] { static_cast<void>(cl::validate_wrapped_program(L"dowser.exe")); }, cl::ExitCode::usage),
          "relative program refused");
    check(throws_code([&] { static_cast<void>(cl::validate_wrapped_program(dir + L"\\none\\hoi4.exe")); },
                      cl::ExitCode::usage),
          "missing program refused");
    check(cl::default_program(game_dir + L"\\hoi4.exe") == game_dir + L"\\dowser.exe", "shortcut starts dowser");
    DeleteFileW((game_dir + L"\\dowser.exe").c_str());
    check(cl::default_program(game_dir + L"\\hoi4.exe") == game_dir + L"\\hoi4.exe", "no dowser: game itself");

    const std::string vdf =
        "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
        "\t\t\"apps\" { \"394360\" \"1\" }\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\Gry Steam\"\n"
        "\t}\n\t\"2\" { \"path\" broken }\n}\n";
    const auto paths = cl::parse_library_folders(vdf);
    check(paths.size() == 2 && paths[0] == L"C:\\Program Files (x86)\\Steam" && paths[1] == L"D:\\Gry Steam",
          "libraryfolders.vdf paths");
    check(cl::parse_library_folders("").empty() && cl::parse_library_folders(R"("path" "unterminated)").empty(),
          "malformed VDF yields nothing");

    SetEnvironmentVariableW(L"LOCALAPPDATA", dir.c_str());
    check(cl::app_data_folder(true) == dir + L"\\RenameAnything", "app data follows %LOCALAPPDATA%");
    check(cl::remembered_game().empty(), "nothing remembered yet");
    cl::remember_game(game_dir + L"\\hoi4.exe");
    check(cl::remembered_game() == game_dir + L"\\hoi4.exe", "remembered path round trip");
    write_file(dir + L"\\RenameAnything\\hoi4.path", "garbage");
    check(cl::remembered_game().empty(), "malformed remembered path ignored");
}

/// @brief Log rotation and notice texts.
void log_and_notices(const std::wstring& dir) {
    const auto path = dir + L"\\launcher.log";
    write_file(path, std::string(cl::log_rotate_bytes + 1, 'x'));
    cl::Log log(path);
    log.write(cl::Level::info, L"zażółć");
    WIN32_FILE_ATTRIBUTE_DATA rotated{};
    check(GetFileAttributesExW((path + L".1").c_str(), GetFileExInfoStandard, &rotated) != 0 &&
              rotated.nFileSizeLow == cl::log_rotate_bytes + 1,
          "oversized log rotated to .1");
    WIN32_FILE_ATTRIBUTE_DATA fresh{};
    check(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fresh) != 0 && fresh.nFileSizeLow < 200,
          "new log started");
    for (int notice = 0; notice <= static_cast<int>(cl::last_notice); ++notice) {
        check(!cl::notice_text(static_cast<cl::Notice>(notice), true).empty() &&
                  !cl::notice_text(static_cast<cl::Notice>(notice), false).empty(),
              "every notice has PL and EN text");
    }
    cl::Notifier quiet(log, false);
    quiet.notify(cl::Notice::unknown_game_build, L"detail");
    check(quiet.count() == 1, "notice counted without a dialog");
}

/// @brief Marker counting and the readiness decision table.
void readiness_rules(const std::wstring& dir) {
    check(cl::count_marker("Executing History from a\nExecuting History from b\n", cl::ready_log_marker) == 2,
          "two markers");
    check(cl::count_marker("Executing History fro", cl::ready_log_marker) == 0, "partial marker");
    check(cl::count_marker("abc", "") == 0, "empty marker");
    using cl::Step;
    const cl::Observation all{.alive = true, .window_ready = true, .log_ready = true};
    const cl::Observation no_log{.alive = true, .window_ready = true, .log_ready = false};
    const cl::Observation no_window{.alive = true, .window_ready = false, .log_ready = true};
    const cl::Observation dead{.alive = false, .window_ready = true, .log_ready = true};
    check(cl::evaluate(all, false, false, false) == Step::start_settle, "conditions start the settle period");
    check(cl::evaluate(all, true, false, false) == Step::keep_waiting, "settling");
    check(cl::evaluate(all, true, true, false) == Step::ready, "ready after settle");
    check(cl::evaluate(all, false, true, false) == Step::start_settle, "never ready without a settle period");
    check(cl::evaluate(no_log, false, false, false) == Step::keep_waiting, "window alone is not ready");
    check(cl::evaluate(no_window, false, false, false) == Step::keep_waiting, "log alone is not ready");
    check(cl::evaluate(no_log, true, true, false) == Step::keep_waiting, "condition lost during settle");
    check(cl::evaluate(dead, true, true, false) == Step::exited, "exit wins");
    check(cl::evaluate(no_log, false, false, true) == Step::timed_out, "deadline");
    check(cl::evaluate(all, true, true, true) == Step::ready, "ready at the deadline still counts");

    const auto log = dir + L"\\game.log";
    write_file(log, "Executing History from 1\nExecuting History from 2\n");
    check(cl::game_log_ready(log, 0), "fresh log with markers");
    check(!cl::game_log_ready(log, cl::now_ticks() + 600000000ULL), "log older than the game refused");
    write_file(log, "Executing History from 1\n");
    check(!cl::game_log_ready(log, 0), "one marker is not enough");
    check(!cl::game_log_ready(dir + L"\\missing.log", 0), "missing log");
    check(!cl::window_responsive(GetCurrentProcessId(), 100), "console test process has no visible window");
}

/// @brief dlc_load.json formats and the CAPIE switch-mod decision.
void playset_rules(const std::wstring& dir) {
    using cl::PlaysetState;
    const cl::ModIdentity local{.workshop_id = {}, .descriptors = {"mod/capie.mod"}};
    const cl::ModIdentity workshop{.workshop_id = "3500000001", .descriptors = {"mod/capie.mod"}};
    const auto state = [](std::string_view json, const cl::ModIdentity& identity) {
        return cl::evaluate_playset(json, identity).state;
    };
    // Real Paradox Launcher shape (one line, Workshop entries, empty DLC list).
    const std::string real =
        R"({"enabled_mods":["mod/ugc_3274762952.mod","mod/capie.mod","mod/ugc_820260968.mod"],"disabled_dlcs":[]})";
    check(state(real, local) == PlaysetState::enabled, "local descriptor enabled");
    check(state(R"({"enabled_mods":["mod/ugc_3274762952.mod"],"disabled_dlcs":[]})", local) == PlaysetState::disabled,
          "other mods only: disabled");
    check(state(R"({"enabled_mods":[],"disabled_dlcs":[]})", local) == PlaysetState::disabled, "vanilla playset");
    check(state("{\n  \"disabled_dlcs\" : [ \"dlc/x\" ],\r\n\t\"enabled_mods\" : [\n    \"mod/capie.mod\"\n  ]\n}\n",
                local) == PlaysetState::enabled,
          "pretty-printed, keys in any order");
    check(state("\xEF\xBB\xBF{\"enabled_mods\":[\"mod/capie.mod\"]}", local) == PlaysetState::enabled, "UTF-8 BOM");
    check(state(R"({"enabled_mods":["MOD\\CAPIE.MOD"]})", local) == PlaysetState::enabled,
          "backslash and case insensitive");
    check(state(R"({"enabled_mods":["mod\/capie.mod"]})", local) == PlaysetState::enabled, "escaped slash");
    check(state(R"({"enabled_mods":["mod/\u0063apie.mod"]})", local) == PlaysetState::enabled, "unicode escape");
    check(state(R"({"enabled_mods":["mod/capie.mod.bak","mod/my_capie.mod","capie.mod"]})", local) ==
              PlaysetState::disabled,
          "near names do not match");
    check(state(R"({"enabled_mods":["mod/ugc_3500000001.mod"]})", workshop) == PlaysetState::enabled,
          "Workshop id matches");
    check(state(R"({"enabled_mods":["mod/ugc_3500000001.mod"]})", local) == PlaysetState::disabled,
          "Workshop entry without a configured id");
    check(state(R"({"enabled_mods":["mod/ugc_35000000010.mod"]})", workshop) == PlaysetState::disabled,
          "longer Workshop id does not match");
    check(
        state(
            R"({"enabled_mods":["mod/capie.mod"],"meta":{"a":[1,-2.5e3,true,false,null,{"b":"\u00e9 \ud83d\ude00 é"}]}})",
            local) == PlaysetState::enabled,
        "unknown nested keys skipped");
    // Uncertain input: vanilla through `unknown`.
    const std::array<std::string_view, 16> broken = {
        "",
        "   ",
        "[]",
        R"({"disabled_dlcs":[]})",
        R"({"enabled_mods":"mod/capie.mod"})",
        R"({"enabled_mods":["mod/capie.mod",1]})",
        R"({"enabled_mods":["mod/capie.mod"])",
        R"({"enabled_mods":["mod/capie.mod"]} trailing)",
        R"({"enabled_mods":["mod/capie.mod"],})",
        R"({"enabled_mods":["mod/capie.mod"]}{})",
        R"({"enabled_mods":[],"enabled_mods":["mod/capie.mod"]})",
        R"({"enabled_mods":["mod/capie.mod\x"]})",
        R"({"enabled_mods":["\ud800"]})",
        R"({'enabled_mods':['mod/capie.mod']})",
        R"({"x":01,"enabled_mods":["mod/capie.mod"]})",
        "{\"enabled_mods\":[\"mod/capie\n.mod\"]}",
    };
    for (const auto& json : broken) {
        check(state(json, local) == PlaysetState::unknown, "uncertain dlc_load.json is unknown");
    }
    std::string deep = R"({"x":)";
    for (int i = 0; i < 40; ++i) {
        deep += "[";
    }
    for (int i = 0; i < 40; ++i) {
        deep += "]";
    }
    deep += R"(,"enabled_mods":["mod/capie.mod"]})";
    check(state(deep, local) == PlaysetState::unknown, "nesting beyond the limit refused");
    check(state(real, cl::ModIdentity{}) == PlaysetState::unknown, "no identity configured");
    check(state(real, cl::ModIdentity{.workshop_id = "12a", .descriptors = {""}}) == PlaysetState::unknown,
          "malformed identity is not usable");
    check(
        cl::capie_mod_identity().descriptors.size() == 1 && cl::capie_mod_identity().descriptors[0] == "mod/capie.mod",
        "production identity: local descriptor");
    const auto production = cl::capie_mod_identity();
    check(production.workshop_id == "3819929471", "production identity: Workshop ID of the CAPIE switch mod");
    // Paradox Launcher form of a subscribed Workshop item, next to other Workshop mods.
    check(state(R"({"enabled_mods":["mod/ugc_3274762952.mod","mod/ugc_3819929471.mod"],"disabled_dlcs":[]})",
                production) == PlaysetState::enabled,
          "production identity: subscribed Workshop item enabled");
    check(state(R"({"enabled_mods":["mod/capie.mod"],"disabled_dlcs":[]})", production) == PlaysetState::enabled,
          "production identity: local copy enabled");
    check(state(R"({"enabled_mods":["mod/ugc_3274762952.mod","mod/ugc_381992947.mod"],"disabled_dlcs":[]})",
                production) == PlaysetState::disabled,
          "production identity: other Workshop items and a shorter ID do not match");
    // File access.
    const auto file = dir + L"\\dlc_load.json";
    write_file(file, real);
    check(cl::read_playset(file, local).state == PlaysetState::enabled, "file: enabled");
    write_file(file, "{\"enabled_mods\":[");
    check(cl::read_playset(file, local).state == PlaysetState::unknown, "file: truncated JSON");
    write_file(file, "");
    check(cl::read_playset(file, local).state == PlaysetState::unknown, "file: empty");
    write_file(file, R"({"enabled_mods":["mod/capie.mod"]})" + std::string(cl::dlc_load_read_limit, ' '));
    check(cl::read_playset(file, local).state == PlaysetState::unknown, "file: over the size limit");
    check(cl::read_playset(dir + L"\\missing\\dlc_load.json", local).state == PlaysetState::unknown, "file: missing");
    check(cl::read_playset(L"", local).state == PlaysetState::unknown, "file: no path");
    check(cl::read_playset(dir, local).state == PlaysetState::unknown, "file: a folder");
    check(!cl::read_playset(dir + L"\\missing.json", local).reason.empty(), "unknown carries a reason");
    check(cl::default_dlc_load().ends_with(L"\\Paradox Interactive\\Hearts of Iron IV\\dlc_load.json"),
          "production dlc_load.json under Documents");
}

/// @brief Parent-chain rules with synthetic process facts.
void ancestry_rules() {
    std::map<DWORD, cl::ProcessFacts> table;
    const cl::FactsFn facts = [&table](DWORD pid) -> std::optional<cl::ProcessFacts> {
        const auto found = table.find(pid);
        return found == table.end() ? std::nullopt : std::optional<cl::ProcessFacts>(found->second);
    };
    table = {{10, {.parent = 9, .created = 100, .in_tree = false}}, {9, {.parent = 1, .created = 90, .in_tree = true}}};
    check(cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "direct parent in tree");
    table = {{10, {.parent = 9, .created = 100}},
             {9, {.parent = 8, .created = 90}},
             {8, {.parent = 1, .created = 80, .in_tree = true}}};
    check(cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "grandparent in tree");
    table = {{10, {.parent = 9, .created = 100}}, {9, {.parent = 1, .created = 110, .in_tree = true}}};
    check(!cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "parent younger than child (PID reuse) refused");
    table = {{10, {.parent = 9, .created = 100}}};
    check(!cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "exited parent refused");
    table = {{10, {.parent = 10, .created = 100}}};
    check(!cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "self parent refused");
    table = {{10, {.parent = 0, .created = 100}}};
    check(!cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "no parent");
    table = {{20, {.parent = 19, .created = 100}}, {19, {.parent = 18, .created = 90}},
             {18, {.parent = 17, .created = 80}},  {17, {.parent = 16, .created = 70}},
             {16, {.parent = 15, .created = 60}},  {15, {.parent = 1, .created = 50, .in_tree = true}}};
    check(!cl::descends_from_tree(20, facts, cl::max_ancestry_depth), "chain longer than the bound refused");
    table = {{10, {.parent = 9, .created = 100}}, {9, {.parent = 8, .created = 90}}, {8, {.parent = 1, .created = 80}}};
    check(!cl::descends_from_tree(10, facts, cl::max_ancestry_depth), "chain without a tree member refused");
    check(!cl::descends_from_tree(10, [](DWORD) { return std::optional<cl::ProcessFacts>{}; }, 4), "unknown candidate");
}

/// @brief Step policy of the injector sequence.
void injection_rules() {
    using cl::HelperState;
    check(cl::may_continue({.state = HelperState::completed, .code = 0, .output = {}}, true), "success continues");
    check(!cl::may_continue({.state = HelperState::completed, .code = 0, .output = {}}, false), "lost game stops");
    check(!cl::may_continue({.state = HelperState::completed, .code = 14, .output = {}}, true), "nonzero stops");
    check(!cl::may_continue({.state = HelperState::timed_out, .code = 0, .output = {}}, true), "timeout stops");
    check(!cl::may_continue({.state = HelperState::failed_wait, .code = 0, .output = {}}, true), "wait failure stops");
    check(!cl::may_continue({.state = HelperState::not_created, .code = 0, .output = {}}, true), "not created stops");
    check(cl::injection_order[0] == cl::bundle_core && cl::injection_order[1] == cl::bundle_editor,
          "core before editor");
    check(cl::describe({.state = HelperState::completed, .code = 14, .output = "refused"}).find(L"14") !=
              std::wstring::npos,
          "description carries the code");
}

/// @brief Job membership with real processes.
void job_membership(const std::wstring& dir, const std::wstring& fake) {
    const auto game_dir = dir + L"\\jobgame";
    CreateDirectoryW(game_dir.c_str(), nullptr);
    const auto hoi4 = game_dir + L"\\hoi4.exe";
    CopyFileW(fake.c_str(), hoi4.c_str(), FALSE);
    SetEnvironmentVariableW(L"FAKE_GAME_LIFETIME_MS", L"1500");
    SetEnvironmentVariableW(L"FAKE_GAME_MARKERS", L"0");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION outside{};
    std::wstring line = L"\"" + hoi4 + L"\"";
    check(CreateProcessW(hoi4.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &outside) !=
              0,
          "unrelated process started");
    const cl::Handle outside_process(outside.hProcess);
    const cl::Handle outside_thread(outside.hThread);
    cl::Log log(L"");
    auto tree = cl::ProcessTree::start({.application = hoi4, .command_line = line, .working_directory = {}}, log);
    check(tree.job_attached(), "job attached");
    check(tree.contains(tree.root()), "started program is in the tree");
    check(!tree.contains(outside_process.get()), "unrelated process with the same image is not in the tree");
    const auto members = tree.member_pids();
    check(members.size() == 1 && members[0] == tree.root_stamp().pid, "job lists exactly the started program");
    check(tree.active(), "tree active while the program runs");
    const std::vector<cl::ProcessStamp> none;
    const auto found = cl::find_game(tree, none);
    check(found && found->stamp == tree.root_stamp() && found->via_job, "find_game returns the tree member only");
    check(found && cl::find_game(tree, {found->stamp}) == std::nullopt, "a handled game is never returned again");
    WaitForSingleObject(tree.root(), INFINITE);
    check(!tree.active(), "tree inactive after the program ended");
    WaitForSingleObject(outside_process.get(), INFINITE);
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
        static_cast<void>(std::printf("usage: launcher_unit_test SCRATCH FAKE_EXE\n"));
        return 2;
    }
    const std::wstring dir = argv[1];
    CreateDirectoryW(dir.c_str(), nullptr);
    try {
        tokenizer_matches_system();
        invocation_parsing();
        unquoted_resolution();
        argument_round_trip();
        file_pins(dir);
        game_location(dir);
        log_and_notices(dir);
        readiness_rules(dir);
        playset_rules(dir);
        ancestry_rules();
        injection_rules();
        job_membership(dir, argv[2]);
    } catch (const std::exception& error) {
        ++failures;
        static_cast<void>(std::printf("FAIL unexpected exception: %s\n", error.what()));
    }
    static_cast<void>(std::printf("launcher unit tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
