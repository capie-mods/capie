#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string>

namespace rn {
inline constexpr char exe_sha256[] = "7dc947be34970da1e1c787bcddbe7f62b610ff258aebf8f06aa8b348f3a031d5";

inline std::string sha256_file(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string answer;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        std::array<unsigned char, 65536> chunk{};
        DWORD bytes = 0;
        bool ok = true;
        while (true) {
            if (!ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &bytes, nullptr)) { ok = false; break; }
            if (!bytes) break;
            if (BCryptHashData(hash, chunk.data(), bytes, 0) < 0) { ok = false; break; }
        }
        std::array<unsigned char, 32> digest{};
        if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
            constexpr char digits[] = "0123456789abcdef";
            for (const auto c : digest) { answer += digits[c >> 4]; answer += digits[c & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return answer;
}
}
