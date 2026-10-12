/**
 * @file file_pin.hpp
 * @brief Read-only file pins: open once, hash that same handle, keep it open so the file cannot change.
 *
 * The launcher trusts four files: the game executable and the three CAPIE binaries. For
 * each it opens the file with `GENERIC_READ` and `FILE_SHARE_READ` only, so while the
 * handle lives nobody (short of an administrator) can write, rename or delete it. It then
 * hashes the content through that same handle, so the bytes that were hashed are the bytes
 * that stay on disk. It remembers the final path and the volume/file id; before every
 * later step the path is opened again and must still name the same file.
 *
 * `pe_x64` is a bounded header check (x64, DLL or EXE as expected); it never maps or runs
 * the file. This is the accepted launcher's file guard, reorganised; the hashing itself is
 * CAPIE's `core/sha256.hpp`.
 */
#pragma once
#include "../core/sha256.hpp"
#include "config.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace capie::launcher {

/// @brief Largest accepted `e_lfanew` (PE header offset).
inline constexpr LONG max_pe_header_offset = 65536;
/// @brief Largest accepted section count.
inline constexpr WORD max_pe_sections = 96;
/// @brief Characters of a long path buffer.
inline constexpr DWORD long_path_chars = 32768;

/// @brief Volume serial number and file index: the identity of a file independent of its path.
struct FileId {
    DWORD volume{};  ///< volume serial number
    DWORD high{};    ///< file index, high part
    DWORD low{};     ///< file index, low part
    /// @brief Field-wise equality.
    bool operator==(const FileId&) const = default;
};

/**
 * @brief Whether a path is an absolute path on a local drive (`C:\...` or `\\?\C:\...`).
 * @param path Path to check.
 * @return `false` for relative, drive-relative (`C:x`) and UNC paths.
 */
inline bool absolute_local_path(std::wstring_view path) noexcept {
    const std::size_t start = path.starts_with(L"\\\\?\\") ? 4 : 0;
    if (path.size() <= start + 2) {
        return false;
    }
    const wchar_t drive = path[start];
    const bool letter = (drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z');
    return letter && path[start + 1] == L':' && (path[start + 2] == L'\\' || path[start + 2] == L'/');
}

/**
 * @brief Identity of an open file.
 * @param file Open handle.
 * @return Volume and file index.
 * @throws LauncherError when the handle is not a regular file.
 */
inline FileId file_id(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file, &info) == 0 || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        throw LauncherError(ExitCode::invalid_bundle, L"Cannot confirm the identity of a file.");
    }
    return {.volume = info.dwVolumeSerialNumber, .high = info.nFileIndexHigh, .low = info.nFileIndexLow};
}

/**
 * @brief Normalised final path of an open file (junctions such as `current\` resolved).
 * @param file Open handle.
 * @return DOS path, possibly with the `\\?\` prefix.
 * @throws LauncherError when the path cannot be read.
 */
inline std::wstring final_path(HANDLE file) {
    const DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0 || size >= long_path_chars) {
        throw LauncherError(ExitCode::invalid_bundle, L"Cannot read the full path of a file.");
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1);
    const DWORD written = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= buffer.size()) {
        throw LauncherError(ExitCode::invalid_bundle, L"The path of a file changed while it was read.");
    }
    return {buffer.data(), written};
}

/**
 * @brief SHA-256 of the whole file behind a handle, read from offset 0.
 * @param file Readable handle (the position is moved).
 * @return 64 lower-case hex characters.
 * @throws LauncherError on a read or hashing error.
 */
inline std::string hash_handle(HANDLE file) {
    const LARGE_INTEGER zero{};
    core::Sha256Digest digest{};
    if (SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) == 0 || !core::sha256_handle(file, digest)) {
        throw LauncherError(ExitCode::invalid_bundle, L"Cannot hash a file.");
    }
    return core::to_hex(digest);
}

/**
 * @brief Reads `count` bytes at `offset`, only when they lie inside the file.
 * @param file Readable handle.
 * @param offset Byte offset.
 * @param target Destination.
 * @param count Byte count.
 * @return `true` when exactly `count` bytes were read.
 */
inline bool read_at(HANDLE file, std::uint64_t offset, void* target, DWORD count) noexcept {
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == 0 || size.QuadPart < 0) {
        return false;
    }
    const auto length = static_cast<std::uint64_t>(size.QuadPart);
    if (offset > length || count > length - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max())) {
        return false;
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD actual = 0;
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != 0 &&
           ReadFile(file, target, count, &actual, nullptr) != 0 && actual == count;
}

/**
 * @brief Bounded check that a file is an x64 PE image of the expected kind.
 * @param file Readable handle.
 * @param dll `true` when a DLL is expected, `false` for an EXE.
 * @return `true` for a plausible AMD64 PE32+ header of that kind.
 */
inline bool pe_x64(HANDLE file, bool dll) noexcept {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return read_at(file, 0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
           dos.e_lfanew <= max_pe_header_offset &&
           read_at(file, static_cast<std::uint64_t>(dos.e_lfanew), &nt, sizeof(nt)) &&
           nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.NumberOfSections > 0 &&
           nt.FileHeader.NumberOfSections <= max_pe_sections &&
           ((nt.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0) == dll;
}

/**
 * @brief Opens a file read-only with sharing that forbids writing, renaming and deleting it.
 * @param path Path.
 * @return Handle (invalid on failure; `GetLastError` is set).
 */
inline Handle open_pinned(const std::wstring& path) {
    return Handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
}

/// @brief A file held open read-only, with its final path, identity and hash.
struct PinnedFile {
    Handle file;         ///< the pin
    std::wstring path;   ///< final path at pin time
    FileId identity;     ///< volume/file id at pin time
    std::string digest;  ///< SHA-256 of the pinned content

    /**
     * @brief Pins and hashes a file.
     * @param input Absolute local path.
     * @return The pin.
     * @throws LauncherError when the path is not absolute/local or the file cannot be opened or hashed.
     */
    static PinnedFile open(const std::wstring& input) {
        if (!absolute_local_path(input)) {
            throw LauncherError(ExitCode::usage, L"An absolute path on a local drive is required.");
        }
        PinnedFile result;
        result.file = open_pinned(input);
        if (!result.file) {
            throw LauncherError(ExitCode::invalid_bundle,
                                L"Missing or busy file: " + input + windows_error(GetLastError()));
        }
        result.path = final_path(result.file.get());
        result.identity = file_id(result.file.get());
        result.digest = hash_handle(result.file.get());
        return result;
    }

    /**
     * @brief Whether the remembered path still names the pinned file.
     * @return `false` on any error.
     */
    [[nodiscard]] bool still_same_path() const noexcept {
        try {
            const Handle reopened(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL, nullptr));
            return reopened && file_id(reopened.get()) == identity && final_path(reopened.get()) == path;
        } catch (...) {
            return false;
        }
    }
};

/**
 * @brief Pins a file and requires an exact hash and PE kind.
 * @param path Absolute path.
 * @param expected_sha256 Lower-case hex digest.
 * @param dll Expected PE kind.
 * @param failure Exit code of the error thrown on mismatch.
 * @return The pin.
 * @throws LauncherError with `failure` on any problem.
 */
inline PinnedFile verify_file(const std::wstring& path, const std::string& expected_sha256, bool dll,
                              ExitCode failure) {
    try {
        auto pinned = PinnedFile::open(path);
        if (pinned.digest != expected_sha256 || !pe_x64(pinned.file.get(), dll)) {
            throw LauncherError(failure, L"Unsupported or modified file: " + path);
        }
        return pinned;
    } catch (const LauncherError& error) {
        throw LauncherError(failure, error.message());
    }
}

/// @brief The three pinned CAPIE bundle files, in `BundleManifest` order.
using Bundle = std::array<PinnedFile, bundle_size>;

/**
 * @brief Pins and verifies the CAPIE bundle under `<root>\runtime\`.
 * @param policy Bundle root and manifest.
 * @return The pins.
 * @throws LauncherError (invalid_bundle) on any mismatch.
 */
inline Bundle verify_bundle(const Policy& policy) {
    Bundle result;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& entry = policy.bundle[i];
        result[i] = verify_file(policy.bundle_root + L"\\" + runtime_dir_name + L"\\" + entry.name, entry.sha256,
                                entry.dll, ExitCode::invalid_bundle);
    }
    return result;
}

/**
 * @brief Whether the bundle's runtime folder accepts a new file (the editor DLL writes its log there).
 * @param policy Bundle root.
 * @return `true` when a temporary file could be created (it is deleted on close).
 */
inline bool runtime_writable(const Policy& policy) {
    const auto path = policy.bundle_root + L"\\" + runtime_dir_name + L"\\.capie-write-test-" +
                      std::to_wstring(GetCurrentProcessId());
    const Handle probe(CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    return static_cast<bool>(probe);
}

}  // namespace capie::launcher
