#include "launcher_core.hpp"
#include <shellapi.h>
#include <cstring>
#include <iostream>

namespace {
unsigned checks{};
void require(bool condition, const char* name) {
    ++checks;
    if (!condition) throw std::runtime_error(name);
}
void write_file(const std::wstring& path, const void* data, DWORD size) {
    rnl::Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD actual{};
    require(file && WriteFile(file.get(), data, size, &actual, nullptr) && actual == size, "create private fixture");
}
void arguments() {
    const std::vector<std::wstring> expected = {L"C:\\Program Files\\RenameAnythingLauncher.exe", L"", L"simple",
        L"C:\\Users\\Zolc QA\\hoi4.exe", L"C:\\Zażółć gęślą\\", L"a\"b", L"slash\\\"quote", L"two\\\\\"quotes\"",
        L"ends\\\\", L"&|;`$() %PATH%", L"--game"};
    auto line = rnl::command_line(expected);
    int count{};
    auto* actual = CommandLineToArgvW(line.data(), &count);
    require(actual && static_cast<std::size_t>(count) == expected.size(), "argv count");
    for (int i = 0; i < count; ++i) require(actual[i] == expected[static_cast<std::size_t>(i)], "argv roundtrip");
    LocalFree(actual);
    try { auto rejected = rnl::command_line({L"test", std::wstring(32767, L'x')}); require(false, "overlong rejected"); }
    catch (const rnl::Error&) { require(true, "overlong rejected"); }
    try { auto rejected = rnl::command_line({std::wstring(L"a\0b", 3)}); require(false, "NUL rejected"); }
    catch (const rnl::Error&) { require(true, "NUL rejected"); }
    require(rnl::absolute_local_path(L"C:\\game\\hoi4.exe"), "absolute local accepted");
    require(rnl::absolute_local_path(L"\\\\?\\C:\\game\\hoi4.exe"), "extended local accepted");
    require(!rnl::absolute_local_path(L"hoi4.exe") && !rnl::absolute_local_path(L"C:hoi4.exe") &&
        !rnl::absolute_local_path(L"\\\\server\\game\\hoi4.exe"), "relative/network rejected");
}
void file_guards(const std::wstring& directory) {
    const auto path = directory + L"\\same-handle.txt";
    const auto renamed = directory + L"\\same-handle-renamed.txt";
    write_file(path, "abc", 3);
    {
        auto pin = rnl::PinnedFile::open(path);
        require(pin.digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "same-handle known SHA256");
        require(rnl::hash_handle(pin.file.get()) == pin.digest, "same handle can hash twice");
        require(pin.still_same_path(), "file identity path recheck");
        rnl::Handle reader(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        require(reader && rnl::file_id(reader.get()) == pin.identity, "matching reopened identity");
        rnl::Handle writer(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        require(!writer && GetLastError() == ERROR_SHARING_VIOLATION, "pin blocks concurrent write");
        require(!DeleteFileW(path.c_str()), "pin blocks deletion");
        require(!MoveFileExW(path.c_str(), renamed.c_str(), 0), "pin blocks replacement/rename");
        require(!rnl::pe_x64(pin.file.get(), false), "non-PE rejected");
        // Read the inheritance flag without altering it or relying on handle values.
        DWORD flags{};
        require(GetHandleInformation(pin.file.get(), &flags) && !(flags & HANDLE_FLAG_INHERIT), "original pin noninheritable");
    }
    require(DeleteFileW(path.c_str()) != 0, "fixture removable after pin closes");
    try { auto rejected = rnl::PinnedFile::open(path); require(false, "missing file rejected"); }
    catch (const rnl::Error&) { require(true, "missing file rejected"); }
}
void pe_guards(const std::wstring& directory) {
    const auto path = directory + L"\\metadata-only.exe";
    std::array<unsigned char, 1024> bytes{};
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 128;
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64; nt.FileHeader.NumberOfSections = 1;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    std::memcpy(bytes.data(), &dos, sizeof(dos)); std::memcpy(bytes.data() + 128, &nt, sizeof(nt));
    write_file(path, bytes.data(), static_cast<DWORD>(bytes.size()));
    { auto pin = rnl::PinnedFile::open(path); require(rnl::pe_x64(pin.file.get(), false), "bounded AMD64 PE metadata");
      require(!rnl::pe_x64(pin.file.get(), true), "EXE cannot replace DLL"); }
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    std::memcpy(bytes.data() + 128, &nt, sizeof(nt));
    write_file(path, bytes.data(), static_cast<DWORD>(bytes.size()));
    { auto pin = rnl::PinnedFile::open(path); require(!rnl::pe_x64(pin.file.get(), false), "x86 rejected"); }
    dos.e_lfanew = 65537; std::memcpy(bytes.data(), &dos, sizeof(dos));
    write_file(path, bytes.data(), static_cast<DWORD>(bytes.size()));
    { auto pin = rnl::PinnedFile::open(path); require(!rnl::pe_x64(pin.file.get(), false), "oversized NT offset rejected"); }
    write_file(path, bytes.data(), 4);
    { auto pin = rnl::PinnedFile::open(path); require(!rnl::pe_x64(pin.file.get(), false), "truncated PE rejected"); }
    require(DeleteFileW(path.c_str()) != 0, "PE fixture cleanup");
}
void stage_policy() {
    using namespace rnl;
    require(may_continue({HelperState::completed, 0, {}}, true), "only completed zero and own live game continue");
    require(!may_continue({HelperState::completed, 0, {}}, false), "game exit prevents helper2");
    for (const auto code : {DWORD(10), DWORD(11), DWORD(14), DWORD(15), DWORD(99)})
        require(!may_continue({HelperState::completed, code, {}}, true), "nonzero prevents helper2");
    for (const auto state : {HelperState::timed_out, HelperState::failed_wait, HelperState::not_created})
        require(!may_continue({state, 0, {}}, true), "uncertain zero cannot continue");
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || !rnl::absolute_local_path(argv[1])) return 2;
    try {
        if (!CreateDirectoryW(argv[1], nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return 2;
        arguments(); file_guards(argv[1]); pe_guards(argv[1]); stage_policy();
        std::cout << "Launcher component tests PASS: " << checks << " checks; no child/game/UI/config operations.\n";
        return 0;
    } catch (const std::exception& failure) { std::cerr << "FAIL: " << failure.what() << '\n'; return 1; }
}
