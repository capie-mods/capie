#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rnl {
struct Error : std::runtime_error {
    int code;
    std::wstring message;
    Error(int value, std::wstring text) : std::runtime_error("launcher error"), code(value), message(std::move(text)) {}
};
class Handle {
    HANDLE value_{};
public:
    Handle() noexcept = default;
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
};
inline std::wstring windows_error(DWORD value) { return L" (Windows " + std::to_wstring(value) + L")"; }
inline std::wstring quote_arg(const std::wstring& value) {
    std::wstring result(1, L'"');
    std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result.push_back(ch);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}
inline std::vector<wchar_t> command_line(const std::vector<std::wstring>& args) {
    std::wstring result;
    for (const auto& arg : args) {
        if (arg.find(L'\0') != std::wstring::npos) throw Error(2, L"Argument zawiera NUL.");
        if (!result.empty()) result.push_back(L' ');
        result += quote_arg(arg);
    }
    if (result.size() >= 32767) throw Error(2, L"Zbyt dluga sciezka lub komenda.");
    std::vector<wchar_t> buffer(result.begin(), result.end());
    buffer.push_back(L'\0');
    return buffer;
}
inline bool absolute_local_path(const std::wstring& path) noexcept {
    const std::size_t start = path.rfind(L"\\\\?\\", 0) == 0 ? 4 : 0;
    return path.size() > start + 2 && ((path[start] >= L'A' && path[start] <= L'Z') ||
        (path[start] >= L'a' && path[start] <= L'z')) && path[start + 1] == L':' &&
        (path[start + 2] == L'\\' || path[start + 2] == L'/');
}
struct FileId {
    DWORD volume{}, high{}, low{};
    bool operator==(const FileId&) const = default;
};
inline FileId file_id(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw Error(20, L"Nie mozna potwierdzic tozsamosci pliku.");
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
inline std::wstring final_path(HANDLE file) {
    const DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!size || size >= 32767) throw Error(20, L"Nie mozna ustalic pelnej sciezki pliku.");
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1);
    const DWORD written = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= buffer.size()) throw Error(20, L"Sciezka pliku ulegla zmianie.");
    return {buffer.data(), written};
}
inline std::string hash_handle(HANDLE file) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) throw Error(20, L"Nie mozna odczytac pliku.");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::string result;
    const auto cleanup = [&]() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        cleanup(); throw Error(20, L"SHA256 niedostepne.");
    }
    std::array<unsigned char, 65536> buffer{};
    bool good = true;
    for (;;) {
        DWORD bytes{};
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) { good = false; break; }
        if (!bytes) break;
        if (BCryptHashData(hash, buffer.data(), bytes, 0) < 0) { good = false; break; }
    }
    std::array<unsigned char, 32> digest{};
    good = good && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    cleanup();
    if (!good) throw Error(20, L"Blad odczytu SHA256.");
    constexpr char hex[] = "0123456789abcdef";
    result.reserve(64);
    for (const auto ch : digest) { result.push_back(hex[ch >> 4]); result.push_back(hex[ch & 15]); }
    return result;
}
inline bool read_at(HANDLE file, std::uint64_t offset, void* target, DWORD count) noexcept {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || offset > static_cast<std::uint64_t>(size.QuadPart) ||
        count > static_cast<std::uint64_t>(size.QuadPart) - offset || offset > INT64_MAX) return false;
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD actual{};
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, target, count, &actual, nullptr) && actual == count;
}
inline bool pe_x64(HANDLE file, bool dll) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return read_at(file, 0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew <= 65536 &&
        read_at(file, static_cast<std::uint64_t>(dos.e_lfanew), &nt, sizeof(nt)) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.NumberOfSections > 0 &&
        nt.FileHeader.NumberOfSections <= 96 &&
        ((nt.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0) == dll;
}
struct PinnedFile {
    Handle file;
    std::wstring path;
    FileId identity;
    std::string digest;
    static PinnedFile open(const std::wstring& input) {
        if (!absolute_local_path(input)) throw Error(2, L"Wymagana jest absolutna sciezka na dysku lokalnym.");
        PinnedFile result;
        result.file.reset(CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!result.file) throw Error(20, L"Brak pliku lub plik jest zajety: " + input + windows_error(GetLastError()));
        result.path = final_path(result.file.get());
        result.identity = file_id(result.file.get());
        result.digest = hash_handle(result.file.get());
        return result;
    }
    bool still_same_path() const noexcept {
        try {
            Handle reopened(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr));
            return reopened && file_id(reopened.get()) == identity && final_path(reopened.get()) == path;
        } catch (...) { return false; }
    }
};
struct BundleEntry { const wchar_t* name; const char* sha256; bool dll; };
inline PinnedFile verify_file(const std::wstring& path, const std::string& expected, bool dll, int error) {
    try {
        auto file = PinnedFile::open(path);
        if (file.digest != expected || !pe_x64(file.file.get(), dll)) throw Error(error, L"Nieobslugiwany lub zmieniony plik: " + path);
        return file;
    } catch (const Error& failure) { throw Error(error, failure.message); }
}
enum class HelperState { completed, timed_out, failed_wait, not_created };
struct HelperResult { HelperState state{HelperState::not_created}; DWORD code{99}; std::string output; };
inline bool may_continue(const HelperResult& result, bool own_game_alive) noexcept {
    return result.state == HelperState::completed && result.code == 0 && own_game_alive;
}
} // namespace rnl
