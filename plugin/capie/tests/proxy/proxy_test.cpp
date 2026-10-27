/** @file proxy_test.cpp @brief Real Windows forwarding, pins and local ABI tests; no game is started. */
#include "../../src/proxy/loader.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <winver.h>

namespace {
int checks = 0;
int failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

template <class Function>
Function proc(HMODULE module, const char* name) {
    const auto found = reinterpret_cast<Function>(GetProcAddress(module, name));
    if (found == nullptr) {
        throw std::runtime_error(std::string("missing export ") + name);
    }
    return found;
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("fixture write failed");
    }
}

void activation_tests(const std::wstring& scratch) {
    namespace cp = capie::proxy;
    check(cp::has_activation_flag(L"hoi4.exe -capie"), "exact flag");
    check(cp::has_activation_flag(L"\"C:\\Game Folder\\hoi4.exe\" \"-capie\" -debug"), "quoted flag");
    for (const auto* text : {L"hoi4.exe", L"hoi4.exe -capie=no", L"hoi4.exe --capie", L"hoi4.exe -CAPIE",
                             L"hoi4.exe path-capie", L"-capie", L"hoi4.exe \"x -capie\""}) {
        check(!cp::has_activation_flag(text), "lookalike arguments stay disabled");
    }
    const auto marker = std::filesystem::path(scratch) / cp::activation_file;
    check(!cp::config_enabled(scratch), "missing config disabled");
    write_bytes(marker, cp::activation_marker);
    check(cp::config_enabled(scratch), "LF marker enabled");
    write_bytes(marker, "CAPIE_PROXY_ENABLED_V1\r\n");
    check(cp::config_enabled(scratch), "CRLF marker enabled");
    for (const auto* text : {"", "1", "true", "CAPIE_PROXY_ENABLED_V1", "CAPIE_PROXY_ENABLED_V1\nextra",
                             "\xEF\xBB\xBF" "CAPIE_PROXY_ENABLED_V1\n"}) {
        write_bytes(marker, text);
        check(!cp::config_enabled(scratch), "malformed config disabled");
    }
    write_bytes(marker, std::string(128, 'X'));
    check(!cp::config_enabled(scratch), "oversized config disabled");
}

void forwarding_tests(const std::wstring& proxy_path) {
    std::array<wchar_t, 32768> buffer{};
    const UINT length = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    const auto system_dir = std::wstring(buffer.data(), length);
    const auto system = LoadLibraryExW((system_dir + L"\\version.dll").c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto proxy = LoadLibraryExW(proxy_path.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(system != nullptr && proxy != nullptr && system != proxy, "load separate system and proxy modules");
    if (system == nullptr || proxy == nullptr) {
        return;
    }
    constexpr std::array names = {
        "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
        "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
        "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
        "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto named = GetProcAddress(proxy, names[index]);
        const auto ordinal = GetProcAddress(proxy, reinterpret_cast<LPCSTR>(index + 1));
        check(named != nullptr && named == ordinal, "all 17 names and ordinals match");
    }
    const auto file_w = system_dir + L"\\kernel32.dll";
    const auto file_a = capie::launcher::to_utf8(file_w);
    DWORD ignored = 0;
    const auto size_w = proc<decltype(&GetFileVersionInfoSizeW)>(proxy, "GetFileVersionInfoSizeW");
    SetLastError(12345);
    const auto expected = proc<decltype(&GetFileVersionInfoSizeW)>(system, "GetFileVersionInfoSizeW")(file_w.c_str(), &ignored);
    const DWORD expected_error = GetLastError();
    SetLastError(12345);
    const auto actual_size = size_w(file_w.c_str(), &ignored);
    const DWORD actual_error = GetLastError();
    check(actual_size == expected && expected != 0, "W version info size");
    check(actual_error == expected_error, "first resolution preserves successful call last error");
    check(proc<decltype(&GetFileVersionInfoSizeA)>(proxy, "GetFileVersionInfoSizeA")(file_a.c_str(), &ignored) ==
          proc<decltype(&GetFileVersionInfoSizeA)>(system, "GetFileVersionInfoSizeA")(file_a.c_str(), &ignored), "A version info size");
    for (const DWORD flags : {DWORD{0}, DWORD{FILE_VER_GET_NEUTRAL}, DWORD{FILE_VER_GET_LOCALISED}}) {
        check(proc<decltype(&GetFileVersionInfoSizeExW)>(proxy, "GetFileVersionInfoSizeExW")(flags, file_w.c_str(), &ignored) ==
              proc<decltype(&GetFileVersionInfoSizeExW)>(system, "GetFileVersionInfoSizeExW")(flags, file_w.c_str(), &ignored), "ExW size");
        check(proc<decltype(&GetFileVersionInfoSizeExA)>(proxy, "GetFileVersionInfoSizeExA")(flags, file_a.c_str(), &ignored) ==
              proc<decltype(&GetFileVersionInfoSizeExA)>(system, "GetFileVersionInfoSizeExA")(flags, file_a.c_str(), &ignored), "ExA size");
    }
    std::vector<unsigned char> actual(expected), reference(expected);
    check(proc<decltype(&GetFileVersionInfoW)>(proxy, "GetFileVersionInfoW")(file_w.c_str(), 0, expected, actual.data()) != 0,
          "version info W writes buffer");
    check(proc<decltype(&GetFileVersionInfoW)>(system, "GetFileVersionInfoW")(file_w.c_str(), 0, expected, reference.data()) != 0,
          "system version info W");
    // Version APIs may place process-specific pointers in padding; compare the root ABI data.
    void* actual_root = nullptr;
    void* reference_root = nullptr;
    UINT actual_len = 0;
    UINT reference_len = 0;
    check(proc<decltype(&VerQueryValueW)>(proxy, "VerQueryValueW")(actual.data(), L"\\", &actual_root, &actual_len) != 0 &&
          proc<decltype(&VerQueryValueW)>(system, "VerQueryValueW")(reference.data(), L"\\", &reference_root, &reference_len) != 0 &&
          actual_len == reference_len && std::memcmp(actual_root, reference_root, actual_len) == 0, "query W root output");
    check(proc<decltype(&GetFileVersionInfoA)>(proxy, "GetFileVersionInfoA")(file_a.c_str(), 0, expected, actual.data()) != 0,
          "version info A");
    check(proc<decltype(&VerQueryValueA)>(proxy, "VerQueryValueA")(actual.data(), "\\", &actual_root, &actual_len) != 0,
          "query A root output");
    check(proc<decltype(&GetFileVersionInfoExW)>(proxy, "GetFileVersionInfoExW")(FILE_VER_GET_NEUTRAL, file_w.c_str(), 0,
          expected, actual.data()) != 0, "five-argument ExW forwarding");
    check(proc<decltype(&GetFileVersionInfoExA)>(proxy, "GetFileVersionInfoExA")(FILE_VER_GET_NEUTRAL, file_a.c_str(), 0,
          expected, actual.data()) != 0, "five-argument ExA forwarding");
    std::array<wchar_t, 128> lang_w{}, lang_ref_w{};
    std::array<char, 128> lang_a{}, lang_ref_a{};
    check(proc<decltype(&VerLanguageNameW)>(proxy, "VerLanguageNameW")(1033, lang_w.data(), 128) ==
          proc<decltype(&VerLanguageNameW)>(system, "VerLanguageNameW")(1033, lang_ref_w.data(), 128) && lang_w == lang_ref_w,
          "forwarded kernel32 W export");
    check(proc<decltype(&VerLanguageNameA)>(proxy, "VerLanguageNameA")(1033, lang_a.data(), 128) ==
          proc<decltype(&VerLanguageNameA)>(system, "VerLanguageNameA")(1033, lang_ref_a.data(), 128) && lang_a == lang_ref_a,
          "forwarded kernel32 A export");
    // Exercise eight parameters and output pointers on the stack with harmless nonexistent paths.
    std::array<wchar_t, 260> current{}, destination{}, ref_current{}, ref_destination{};
    UINT current_len = 260, dest_len = 260, ref_current_len = 260, ref_dest_len = 260;
    const DWORD find = proc<decltype(&VerFindFileW)>(proxy, "VerFindFileW")(0, L"capie-nonexistent-file", system_dir.c_str(),
        system_dir.c_str(), current.data(), &current_len, destination.data(), &dest_len);
    const DWORD ref_find = proc<decltype(&VerFindFileW)>(system, "VerFindFileW")(0, L"capie-nonexistent-file", system_dir.c_str(),
        system_dir.c_str(), ref_current.data(), &ref_current_len, ref_destination.data(), &ref_dest_len);
    check(find == ref_find && current_len == ref_current_len && dest_len == ref_dest_len && current == ref_current &&
          destination == ref_destination, "eight-argument VerFindFileW outputs");
    std::array<char, 260> current_a{}, destination_a{}, ref_current_a{}, ref_destination_a{};
    current_len = dest_len = ref_current_len = ref_dest_len = 260;
    const auto dir_a = capie::launcher::to_utf8(system_dir);
    const DWORD find_a = proc<decltype(&VerFindFileA)>(proxy, "VerFindFileA")(0, "capie-nonexistent-file", dir_a.c_str(),
        dir_a.c_str(), current_a.data(), &current_len, destination_a.data(), &dest_len);
    const DWORD ref_find_a = proc<decltype(&VerFindFileA)>(system, "VerFindFileA")(0, "capie-nonexistent-file", dir_a.c_str(),
        dir_a.c_str(), ref_current_a.data(), &ref_current_len, ref_destination_a.data(), &ref_dest_len);
    check(find_a == ref_find_a && current_len == ref_current_len && dest_len == ref_dest_len && current_a == ref_current_a &&
          destination_a == ref_destination_a, "eight-argument VerFindFileA outputs");
    std::array<wchar_t, 260> install_w{}, ref_install_w{};
    std::array<char, 260> install_a{}, ref_install_a{};
    UINT install_len = 260, ref_install_len = 260;
    const DWORD install = proc<decltype(&VerInstallFileW)>(proxy, "VerInstallFileW")(0, L"capie-nonexistent-file-0089",
        L"capie-nonexistent-file-0089", system_dir.c_str(), system_dir.c_str(), system_dir.c_str(), install_w.data(), &install_len);
    const DWORD ref_install = proc<decltype(&VerInstallFileW)>(system, "VerInstallFileW")(0, L"capie-nonexistent-file-0089",
        L"capie-nonexistent-file-0089", system_dir.c_str(), system_dir.c_str(), system_dir.c_str(), ref_install_w.data(), &ref_install_len);
    check(install == ref_install && install != 0 && install_w == ref_install_w && install_len == ref_install_len,
          "eight-argument VerInstallFileW failure outputs");
    install_len = ref_install_len = 260;
    const DWORD install_result_a = proc<decltype(&VerInstallFileA)>(proxy, "VerInstallFileA")(0, "capie-nonexistent-file-0089",
        "capie-nonexistent-file-0089", dir_a.c_str(), dir_a.c_str(), dir_a.c_str(), install_a.data(), &install_len);
    const DWORD ref_install_result_a = proc<decltype(&VerInstallFileA)>(system, "VerInstallFileA")(0, "capie-nonexistent-file-0089",
        "capie-nonexistent-file-0089", dir_a.c_str(), dir_a.c_str(), dir_a.c_str(), ref_install_a.data(), &ref_install_len);
    check(install_result_a == ref_install_result_a && install_result_a != 0 && install_a == ref_install_a && install_len == ref_install_len,
          "eight-argument VerInstallFileA failure outputs");
    SetLastError(12345);
    static_cast<void>(size_w(L"C:\\capie-nonexistent-file", &ignored));
    const DWORD proxy_error = GetLastError();
    SetLastError(12345);
    static_cast<void>(proc<decltype(&GetFileVersionInfoSizeW)>(system, "GetFileVersionInfoSizeW")(L"C:\\capie-nonexistent-file", &ignored));
    check(proxy_error == GetLastError(), "failure last-error forwarding");
    std::array<std::thread, 8> threads;
    std::array<bool, 8> thread_results{};
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index] {
            DWORD unused = 0;
            thread_results[index] = size_w(file_w.c_str(), &unused) == expected;
        });
    }
    for (auto& thread : threads) { thread.join(); }
    for (const bool result : thread_results) { check(result, "concurrent forwarding"); }
    check(GetModuleHandleW(L"capie_core.dll") == nullptr && GetModuleHandleW(L"capie_editor.dll") == nullptr,
          "non-game host loads no CAPIE DLLs");
    // The startup proxy intentionally retains its module; process termination cleans it up.
}

void payload_tests(const std::wstring& scratch, const std::wstring& fixtures, std::wstring_view mode = L"success") {
    namespace cp = capie::proxy;
    namespace fs = std::filesystem;
    const auto current = fs::path(scratch) / L"current";
    fs::create_directories(current / L"runtime");
    constexpr std::array names{L"capie_core.dll", L"capie_editor.dll"};
    std::array<std::string, 2> hashes;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto destination = current / L"runtime" / names[index];
        fs::copy_file(fs::path(fixtures) / names[index], destination, fs::copy_options::overwrite_existing);
        hashes[index] = capie::core::sha256_file(destination.c_str());
    }
    std::array<wchar_t, 32768> exe{};
    const DWORD size = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    const auto sentinel = fs::path(scratch) / cp::init_sentinel_file;
    cp::Policy policy{{exe.data(), size}, current.wstring(), capie::core::sha256_file(exe.data()), hashes,
                      sentinel.wstring()};
    // Crash-loop breaker: a sentinel from a start that died inside RNInitialize skips CAPIE once.
    check(capie::launcher::arm_sentinel(policy.sentinel, "previous start"), "plant proxy sentinel");
    check(cp::load_payload(policy).result == cp::LoadResult::crash_guard_skipped, "sentinel left behind skips CAPIE");
    check(!fs::exists(sentinel), "skipped start removes the sentinel");
    check(GetModuleHandleW(names[0]) == nullptr && GetModuleHandleW(names[1]) == nullptr,
          "skipped start maps no CAPIE DLL");
    auto invalid = policy;
    invalid.game_hash = std::string(64, '0');
    check(cp::load_payload(invalid).result == cp::LoadResult::unsupported_game, "unknown executable rejected");
    invalid = policy;
    invalid.dll_hashes[1] = std::string(64, '0');
    check(cp::load_payload(invalid).result == cp::LoadResult::invalid_bundle, "tampered editor rejected");
    check(GetModuleHandleW(names[0]) == nullptr, "no partial core mapping before complete verification");
    invalid = policy;
    invalid.current_dir = L"relative";
    check(cp::load_payload(invalid).result == cp::LoadResult::invalid_bundle, "relative payload path rejected");
    {
        auto verified = cp::verify_payload(policy);
        const auto file = current / L"runtime" / names[0];
        const HANDLE writer = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(writer == INVALID_HANDLE_VALUE, "verified DLL cannot be rewritten");
        if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); }
        check(verified.dlls[0].still_same_path(), "resolved path identity is stable");
    }
    check(!fs::exists(sentinel), "refused verification never writes the sentinel");
    const auto trace = fs::path(scratch) / L"initialization-order.txt";
    check(SetEnvironmentVariableW(L"CAPIE_PROXY_TEST_TRACE", trace.c_str()) != 0, "set fixture trace");
    // The fixture fails (code 80) unless the sentinel exists while RNInitialize runs.
    check(SetEnvironmentVariableW(L"CAPIE_PROXY_TEST_SENTINEL", sentinel.c_str()) != 0, "set fixture sentinel");
    const auto outcome = cp::load_payload(policy);
    const auto expected = mode == L"core_failure" ? cp::LoadResult::core_init_failed
                         : mode == L"editor_failure" ? cp::LoadResult::editor_init_failed
                         : mode == L"missing_export" ? cp::LoadResult::initializer_missing : cp::LoadResult::loaded;
    const DWORD expected_code = mode == L"core_failure" ? 31 : mode == L"editor_failure" ? 32 : 0;
    check(outcome.result == expected && outcome.init_code == expected_code, "local ABI initialization result and code");
    check(!fs::exists(sentinel), "initializers returned: sentinel removed");
    std::ifstream input(trace, std::ios::binary);
    const std::string order((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    check(order == (mode == L"missing_export" ? "" : mode == L"core_failure" ? "C" : "CE"),
          "initialization order respects export and prior-init failures");
    check(GetModuleHandleW(names[0]) != nullptr && GetModuleHandleW(names[1]) != nullptr,
          "mapped modules retained after success or initializer failure");
    for (const auto* name : names) {
        const auto file = current / L"runtime" / name;
        const HANDLE writer = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(writer == INVALID_HANDLE_VALUE, "payload pins retained until process exit");
        if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); }
    }
    check(cp::load_payload(policy).result == cp::LoadResult::co_load_refused, "existing CAPIE module refused");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4 && argc != 5) { return 2; }
    try {
        std::filesystem::create_directories(argv[1]);
        if (argc == 4) {
            activation_tests(argv[1]);
            forwarding_tests(argv[2]);
        }
        payload_tests(argv[1], argv[3], argc == 5 ? std::wstring_view(argv[4]) : L"success");
    } catch (const std::exception& error) {
        check(false, error.what());
    }
    std::printf("proxy_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
