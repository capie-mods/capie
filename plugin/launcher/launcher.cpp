// Development candidate. Only the separately reviewed bundled executables run.
#include "launcher_core.hpp"
#include "manifest.generated.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <tlhelp32.h>

namespace {
using namespace rnl;
constexpr DWORD startup_timeout_ms = 120000;
constexpr DWORD helper_timeout_ms = 90000;
constexpr std::size_t output_limit = 16384;
struct Options { bool help{}, verify{}; std::wstring game; };
Options parse_args() {
    int count{};
    auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) throw Error(2, L"Nie mozna odczytac argumentow.");
    Options result;
    bool game_seen = false;
    try {
        for (int i = 1; i < count; ++i) {
            const std::wstring value(args[i]);
            if (value == L"--help" && !result.help) result.help = true;
            else if (value == L"--verify-only" && !result.verify) result.verify = true;
            else if (value == L"--game" && !game_seen && i + 1 < count) { game_seen = true; result.game = args[++i]; }
            else throw Error(2, L"Nieznany lub powtorzony argument.");
        }
        if (result.help && (result.verify || !result.game.empty() || count != 2)) throw Error(2, L"--help nie przyjmuje innych argumentow.");
        if (result.verify && result.game.empty()) throw Error(2, L"--verify-only wymaga --game ABS.");
    } catch (...) { LocalFree(args); throw; }
    LocalFree(args);
    return result;
}
bool noninteractive_requested() noexcept {
    int count{};
    auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return true;
    bool result = false;
    for (int i = 1; i < count; ++i) {
        if (wcscmp(args[i], L"--help") == 0 || wcscmp(args[i], L"--verify-only") == 0) result = true;
    }
    LocalFree(args);
    return result;
}
void print_line(const std::wstring& text, bool error = false) noexcept {
    const HANDLE destination = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (!destination || destination == INVALID_HANDLE_VALUE) return;
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return;
    try {
        std::string bytes(static_cast<std::size_t>(count), '\0');
        if (!WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, bytes.data(), count, nullptr, nullptr)) return;
        bytes.back() = '\n';
        DWORD actual{};
        WriteFile(destination, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr);
    } catch (...) {}
}
std::wstring own_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) throw Error(20, L"Nie mozna ustalic katalogu launchera.");
    return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path().wstring();
}
std::wstring config_directory(bool create) {
    PWSTR base{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) throw Error(20, L"LocalAppData niedostepny.");
    std::wstring result;
    try { result = std::wstring(base) + L"\\RenameAnything"; }
    catch (...) { CoTaskMemFree(base); throw; }
    CoTaskMemFree(base);
    if (create && !CreateDirectoryW(result.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        throw Error(20, L"Nie mozna utworzyc katalogu preferencji.");
    return result;
}
std::wstring remembered_game() noexcept {
    try {
        Handle file(CreateFileW((config_directory(false) + L"\\hoi4.path").c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        LARGE_INTEGER size{};
        if (!file || !GetFileSizeEx(file.get(), &size) || size.QuadPart < 6 || size.QuadPart > 65538 || size.QuadPart % 2) return {};
        std::vector<wchar_t> data(static_cast<std::size_t>(size.QuadPart) / sizeof(wchar_t));
        DWORD actual{};
        if (!ReadFile(file.get(), data.data(), static_cast<DWORD>(size.QuadPart), &actual, nullptr) ||
            actual != size.QuadPart || data[0] != 0xFEFF || data.back() != L'\0') return {};
        std::wstring path(data.data() + 1, data.size() - 2);
        return absolute_local_path(path) && path.find(L'\0') == std::wstring::npos ? path : std::wstring{};
    } catch (...) { return {}; }
}
void remember_game(const std::wstring& path) noexcept {
    // Preferences are optional. Failure never changes launch safety or game files.
    try {
        const auto dir = config_directory(true);
        const auto temporary = dir + L"\\hoi4.path." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) return;
        std::wstring data(1, 0xFEFF); data += path; data.push_back(L'\0');
        DWORD actual{};
        const DWORD count = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        const bool good = WriteFile(file.get(), data.data(), count, &actual, nullptr) && actual == count && FlushFileBuffers(file.get());
        file.reset();
        if (!good || !MoveFileExW(temporary.c_str(), (dir + L"\\hoi4.path").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            DeleteFileW(temporary.c_str());
    } catch (...) {}
}
std::wstring choose_game(const std::wstring& initial) {
    std::vector<wchar_t> path(32768);
    if (initial.size() < path.size()) std::copy(initial.begin(), initial.end(), path.begin());
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Hearts of Iron IV (hoi4.exe)\0hoi4.exe\0Applications (*.exe)\0*.exe\0\0";
    dialog.lpstrFile = path.data(); dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Rename Anything: wybierz hoi4.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) return path.data();
    const DWORD status = CommDlgExtendedError();
    if (status) throw Error(2, L"Nie mozna otworzyc wyboru pliku." + windows_error(status));
    return {};
}
using Bundle = std::array<PinnedFile, 4>;
Bundle verify_bundle(const std::wstring& root) {
    Bundle result;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = verify_file(root + L"\\runtime\\" + manifest::files[i].name, manifest::files[i].sha256, manifest::files[i].dll, 20);
    return result;
}
PinnedFile verify_game(const std::wstring& path) {
    if (!absolute_local_path(path) || _wcsicmp(std::filesystem::path(path).filename().c_str(), L"hoi4.exe"))
        throw Error(10, L"Wybierz absolutna sciezke do hoi4.exe na dysku lokalnym.");
    return verify_file(path, manifest::game_sha256, false, 10);
}
void verify_runtime_writable(const std::wstring& root) {
    // Frozen editor creates its native log beside its DLL; test only our directory.
    const auto path = root + L"\\runtime\\.rnl-write-test-" + std::to_wstring(GetCurrentProcessId());
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!file) throw Error(20, L"Rozpakuj paczke do zapisywalnego wlasnego katalogu; runtime nie moze zapisac logu.");
}
bool any_game_running() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw Error(30, L"Nie mozna sprawdzic dzialajacych procesow.");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) throw Error(30, L"Nie mozna odczytac listy procesow.");
    do { if (_wcsicmp(entry.szExeFile, L"hoi4.exe") == 0) return true; }
    while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw Error(30, L"Niepelna lista procesow.");
    return false;
}
struct Session {
    PinnedFile game;
    Bundle bundle;
    Handle process;
    DWORD pid{};
    FILETIME created{};
    std::array<Handle, 2> helpers;
    bool alive() const noexcept { return process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT; }
    bool own_identity() const noexcept {
        try {
            FILETIME start{}, end{}, kernel{}, user{};
            if (!alive() || GetProcessId(process.get()) != pid ||
                !GetProcessTimes(process.get(), &start, &end, &kernel, &user) ||
                CompareFileTime(&start, &created) != 0 || !game.still_same_path()) return false;
            std::vector<wchar_t> path(32768); DWORD size = static_cast<DWORD>(path.size());
            if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) return false;
            Handle image(CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!image || file_id(image.get()) != game.identity) return false;
            return std::all_of(bundle.begin(), bundle.end(), [](const PinnedFile& file) { return file.still_same_path(); });
        } catch (...) { return false; }
    }
    void guardian() const noexcept {
        // No timeout means no release of PID/file pins while any late helper lives.
        // This begins only AFTER the one failure message. Never terminate anything.
        std::array<HANDLE, 3> handles{}; DWORD count{};
        if (process) handles[count++] = process.get();
        for (const auto& helper : helpers) if (helper) handles[count++] = helper.get();
        if (!count) return;
        while (WaitForMultipleObjects(count, handles.data(), TRUE, INFINITE) != WAIT_OBJECT_0) Sleep(1000);
    }
};
void start_game(Session& session) {
    if (!session.game.still_same_path()) throw Error(30, L"Sciezka gry ulegla zmianie.");
    auto command = command_line({session.game.path});
    const auto directory = std::filesystem::path(session.game.path).parent_path().wstring();
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(session.game.path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        directory.c_str(), &startup, &process)) throw Error(30, L"Nie mozna uruchomic gry." + windows_error(GetLastError()));
    // Adopt immediately with no allocation/throw between CreateProcess and ownership.
    session.process.reset(process.hProcess); session.pid = process.dwProcessId;
    Handle thread(process.hThread);
    FILETIME end{}, kernel{}, user{};
    if (!GetProcessTimes(session.process.get(), &session.created, &end, &kernel, &user))
        throw Error(30, L"Nie mozna potwierdzic czasu utworzenia nowej gry.");
}
class AttributeList {
    std::vector<unsigned char> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
public:
    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;
    explicit AttributeList(const std::array<HANDLE, 6>& handles) {
        SIZE_T bytes{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) throw Error(40, L"Nie mozna przygotowac listy uchwytow helpera.");
        storage_.resize(bytes); list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) { list_ = nullptr; throw Error(40, L"Blad listy uchwytow helpera."); }
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(handles.data()), sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(list_); list_ = nullptr;
            throw Error(40, L"Nie mozna ograniczyc dziedziczenia uchwytow helpera.");
        }
    }
    ~AttributeList() { if (list_) DeleteProcThreadAttributeList(list_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
};
void drain_pipe(HANDLE pipe, std::string& output) {
    for (unsigned i = 0; i < 8; ++i) {
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) return;
        char data[4096]; DWORD actual{};
        if (!ReadFile(pipe, data, (std::min)(available, static_cast<DWORD>(sizeof(data))), &actual, nullptr) || !actual) return;
        const auto retained = (std::min)(static_cast<std::size_t>(actual), output_limit - output.size());
        output.append(data, retained); // Continue draining after truncation; never backpressure.
    }
}
HelperResult run_helper(Session& session, std::size_t step) {
    if (!session.own_identity()) throw Error(40, L"Nowy proces gry nie przeszedl kontroli tozsamosci.");
    const std::size_t dll_index = step == 0 ? 0 : 2;
    const std::size_t exe_index = dll_index + 1;
    auto command = command_line({session.bundle[exe_index].path, std::to_wstring(session.pid), session.bundle[dll_index].path});
    std::array<Handle, 5> pins;
    std::array<HANDLE, 6> inherited{};
    for (std::size_t i = 0; i < pins.size(); ++i) {
        const HANDLE original = i == 0 ? session.game.file.get() : session.bundle[i - 1].file.get();
        HANDLE duplicate{};
        if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
            throw Error(40, L"Nie mozna przypiac plikow dla helpera.");
        pins[i].reset(duplicate); inherited[i] = duplicate;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &security, 0)) throw Error(40, L"Nie mozna utworzyc diagnostyki helpera.");
    Handle pipe_read(read), pipe_write(write);
    if (!SetHandleInformation(pipe_read.get(), HANDLE_FLAG_INHERIT, 0)) throw Error(40, L"Nie mozna ograniczyc pipe helpera.");
    inherited[5] = pipe_write.get();
    AttributeList attributes(inherited);
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullptr;
    startup.StartupInfo.hStdOutput = pipe_write.get(); startup.StartupInfo.hStdError = pipe_write.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION child{};
    // Recheck after all preflight allocation/argv/handle preparation.
    if (!session.own_identity()) throw Error(40, L"Proces lub sciezka zmienily sie przed helperem.");
    const auto directory = std::filesystem::path(session.bundle[exe_index].path).parent_path().wstring();
    if (!CreateProcessW(session.bundle[exe_index].path.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(), &startup.StartupInfo, &child))
        throw Error(40, L"Nie mozna uruchomic stalego helpera." + windows_error(GetLastError()));
    session.helpers[step].reset(child.hProcess); // Nothrow; guardian also owns late child.
    Handle thread(child.hThread); pipe_write.reset();
    HelperResult result;
    const ULONGLONG started = GetTickCount64();
    for (;;) {
        drain_pipe(pipe_read.get(), result.output);
        const DWORD wait = WaitForSingleObject(session.helpers[step].get(), 50);
        if (wait == WAIT_OBJECT_0) {
            drain_pipe(pipe_read.get(), result.output);
            result.state = GetExitCodeProcess(session.helpers[step].get(), &result.code) ? HelperState::completed : HelperState::failed_wait;
            return result;
        }
        if (wait != WAIT_TIMEOUT) { result.state = HelperState::failed_wait; return result; }
        if (GetTickCount64() - started >= helper_timeout_ms) { result.state = HelperState::timed_out; return result; }
    }
}
std::wstring helper_description(const HelperResult& result) {
    std::wstring text = result.state == HelperState::timed_out ? L"Timeout helpera; stan niepewny." :
        L"Helper zakonczyl sie kodem " + std::to_wstring(result.code) + L".";
    if (!result.output.empty()) {
        text += L"\n\n";
        for (const auto ch : result.output) text.push_back(static_cast<unsigned char>(ch));
    }
    return text;
}
int gui_launch(Options options) {
    Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\RenameAnythingLauncherCandidate"));
    if (!mutex) throw Error(30, L"Nie mozna przygotowac launchera.");
    if (GetLastError() == ERROR_ALREADY_EXISTS) throw Error(30, L"Launcher juz pracuje. Zamknij poprzednia gre przed kolejna proba.");
    const auto root = own_directory();
    auto bundle = verify_bundle(root);
    verify_runtime_writable(root);
    if (options.game.empty()) {
        options.game = remembered_game();
        if (!options.game.empty()) {
            try { auto probe = verify_game(options.game); }
            catch (...) { options.game.clear(); }
        }
        if (options.game.empty()) options.game = choose_game({});
    }
    if (options.game.empty()) return 0; // Explicit file-dialog Cancel.
    Session session{verify_game(options.game), std::move(bundle), {}, 0, {}, {}};
    if (any_game_running()) throw Error(30, L"HOI4 juz dziala. Zamknij gre samodzielnie; launcher nie dolacza do istniejacego procesu.");
    remember_game(session.game.path);
    std::wstring stage = L"Start gry";
    try {
        start_game(session);
        const DWORD idle = WaitForInputIdle(session.process.get(), startup_timeout_ms);
        if (idle != 0 || !session.own_identity()) throw Error(30, L"Gra nie osiagnela sprawdzonego punktu startu; nie wykonano kolejnej iniekcji.");
        for (std::size_t step = 0; step < session.helpers.size(); ++step) {
            stage = step == 0 ? L"Country DLL" : L"Inline editor DLL";
            const auto result = run_helper(session, step);
            if (!may_continue(result, session.own_identity())) throw Error(40, helper_description(result));
        }
        return 0; // Both helpers naturally completed RNInitialize with code0.
    } catch (const Error& failure) {
        try {
            const auto message = stage + L":\n" + failure.message +
                L"\n\nNie udalo sie uruchomic calej obslugi nazw. Gra pozostaje otwarta. "
                L"Zamknij ja przed ponowna proba. Launcher zachowa ciche uchwyty do konca gry i helperow. "
                L"Nie probuje ponownie i nie zamyka gry.";
            MessageBoxW(nullptr, message.c_str(), L"Rename Anything: uruchomienie nieudane", MB_OK | MB_ICONERROR);
        } catch (...) {
            MessageBoxW(nullptr, L"Blad uruchomienia obslugi nazw. Zamknij gre przed ponowna proba. "
                L"Launcher nie ponawia operacji i zachowa uchwyty do naturalnego konca gry i helperow.",
                L"Rename Anything", MB_OK | MB_ICONERROR);
        }
        session.guardian();
        return failure.code;
    } catch (...) {
        MessageBoxW(nullptr, L"Nieoczekiwany blad launchera. Gra pozostaje otwarta. Zamknij ja przed ponowna proba. "
            L"Launcher zachowa uchwyty do naturalnego konca gry i helperow; nie ponawia operacji.",
            L"Rename Anything", MB_OK | MB_ICONERROR);
        session.guardian();
        return 50;
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool noninteractive = noninteractive_requested();
    try {
        const auto options = parse_args();
        if (options.help) {
            print_line(L"Rename Anything launcher DEVELOPMENT CANDIDATE\n"
                L"Double-click: choose/remember hoi4.exe, launch a fresh game and two fixed modules.\n"
                L"--game ABS --verify-only: read-only checks; no process, dialog or user config writes.\n"
                L"--game ABS: launch with this exact supported game. --help: this text.\n"
                L"No arbitrary PID/DLL/command/attach. Runtime launch and product integration PENDING.");
            return 0;
        }
        if (options.verify) {
            const auto bundle = verify_bundle(own_directory());
            const auto game = verify_game(options.game);
            print_line(L"Verification passed: exact pinned host and four fixed bundle files. No launch or configuration writes.");
            return 0;
        }
        return gui_launch(options);
    } catch (const Error& failure) {
        if (noninteractive) print_line(failure.message, true);
        else MessageBoxW(nullptr, failure.message.c_str(), L"Rename Anything", MB_OK | MB_ICONERROR);
        return failure.code;
    } catch (...) {
        if (noninteractive) print_line(L"Unexpected launcher error.", true);
        else MessageBoxW(nullptr, L"Nieoczekiwany blad launchera.", L"Rename Anything", MB_OK | MB_ICONERROR);
        return 50;
    }
}
