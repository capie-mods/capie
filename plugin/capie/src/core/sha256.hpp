/**
 * @file sha256.hpp
 * @brief SHA-256 of files through Windows CNG (`bcrypt.lib`).
 *
 * Used to pin the game executable (DLLs, injector) and the debug DLL (controller). The
 * digest is always rendered as 64 lower-case hex characters so it can be compared with
 * the constants in `game_build.hpp`.
 *
 * It reads files only. It does not cache digests: the caller hashes the file it is about
 * to trust, at the moment it trusts it. An empty string means "could not hash", and every
 * caller treats that as a mismatch (fail-closed).
 */
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // std::numeric_limits<T>::max() must not hit the windows.h macro
#endif
#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <string>

namespace capie::core {

/// @brief Size of one buffered file read while hashing.
inline constexpr DWORD sha256_chunk_bytes = 65536;
/// @brief Size of a SHA-256 digest in bytes.
inline constexpr std::size_t sha256_digest_bytes = 32;
/// @brief Lower-case hexadecimal digits used to render digests.
inline constexpr char hex_digits[] = "0123456789abcdef";

/// @brief Mask of the low four bits of a byte (one hex digit).
inline constexpr unsigned low_nibble_mask = 0x0F;

/// @brief Raw SHA-256 digest.
using Sha256Digest = std::array<unsigned char, sha256_digest_bytes>;

/**
 * @brief Hashes the remaining content of an open file handle from its current position.
 * @param file Readable file handle; the caller seeks first when it needs the whole file.
 * @param digest Receives the digest on success.
 * @return `true` when the whole file was read and hashed.
 * @note Thread: any. Does not close `file`.
 */
inline bool sha256_handle(HANDLE file, Sha256Digest& digest) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::array<unsigned char, sha256_chunk_bytes> chunk{};  // stack buffer: re-entrant, as before
        ok = true;
        for (;;) {
            DWORD bytes = 0;
            if (ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &bytes, nullptr) == 0) {
                ok = false;
                break;
            }
            if (bytes == 0) {
                break;
            }
            if (BCryptHashData(hash, chunk.data(), bytes, 0) < 0) {
                ok = false;
                break;
            }
        }
        ok = ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    }
    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    return ok;
}

/**
 * @brief Renders a digest as 64 lower-case hex characters.
 * @param digest Raw digest.
 * @return Hex string.
 */
inline std::string to_hex(const Sha256Digest& digest) {
    std::string answer;
    answer.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        answer += hex_digits[byte >> 4];
        answer += hex_digits[byte & low_nibble_mask];
    }
    return answer;
}

/**
 * @brief SHA-256 of a file by path.
 * @param path File path.
 * @return 64 lower-case hex characters, or an empty string when the file could not be read.
 * @note The file is opened with full sharing so a running game executable can be hashed.
 */
inline std::string sha256_file(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    Sha256Digest digest{};
    const bool ok = sha256_handle(file, digest);
    CloseHandle(file);
    return ok ? to_hex(digest) : std::string{};
}

}  // namespace capie::core
