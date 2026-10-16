/**
 * @file macos_child_main.cpp
 * @brief Development Paradox child launcher; runtime must independently verify every hook pin.
 */
#include "macos_child_policy.hpp"

#include <CommonCrypto/CommonDigest.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::size_t playset_limit = 1024 * 1024;
constexpr char game_hash[] = "09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb";

/// @brief Unique read-only regular-file owner; nonblocking open rejects FIFOs without waiting.
struct File {
    int fd = -1;

    explicit File(const char* path) {
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        struct stat st{};
        if (fd >= 0 && (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))) {
            close(fd);
            fd = -1;
        }
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
        if (fd >= 0) {
            close(fd);
        }
    }
};

/// @brief Hash one regular file; any open/read/hash error returns an empty mismatch.
std::string hash_file(const char* path) {
    File file(path);
    if (file.fd < 0) {
        return {};
    }
    CC_SHA256_CTX context{};
    if (CC_SHA256_Init(&context) != 1) {
        return {};
    }
    std::array<char, 65536> block{};
    for (;;) {
        const auto count = read(file.fd, block.data(), block.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {};
        }
        if (count == 0) {
            break;
        }
        if (CC_SHA256_Update(&context, block.data(), static_cast<CC_LONG>(count)) != 1) {
            return {};
        }
    }
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256_Final(digest.data(), &context) != 1) {
        return {};
    }
    std::string hex;
    for (auto byte : digest) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 15];
    }
    return hex;
}

/// @brief Bounded playset read; empty return is invalid and disables injection.
std::string read_playset(const char* path) {
    File file(path);
    if (file.fd < 0) {
        return {};
    }
    std::string text;
    std::array<char, 4096> block{};
    for (;;) {
        const auto count = read(file.fd, block.data(), block.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {};
        }
        if (count == 0) {
            return text;
        }
        if (text.size() + static_cast<std::size_t>(count) > playset_limit) {
            return {};
        }
        text.append(block.data(), static_cast<std::size_t>(count));
    }
}

/// @brief Read a loader environment value without logging it.
const char* env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}
}  // namespace

/**
 * @brief Check the playset/artifacts, preserve game argv and cwd, then replace this process.
 * @return 64 for configuration, 71 for inability to prepare environment, 72 for chdir/exec failure.
 * Gate/hash failures launch without this CAPIE library. Fatal setup failures do not claim vanilla launch.
 */
int main(int argc, char** argv) {
    const auto marker = capie::launcher::macos_child::config_start(argc, argv);
    if (!marker) {
        std::fputs(
            "usage: child [game arguments] --capie-dev-child GAME GAME_CWD DLC_LOAD DYLIB DYLIB_SHA256 [--capie-runtime RUNTIME RUNTIME_SHA256] -- [game arguments]\n",
            stderr);
        return 64;
    }
    const int start = *marker;
    const int delimiter = *capie::launcher::macos_child::config_delimiter(argc, argv, start);
    const bool paired = delimiter == start + 9;
    if (paired && argv[start + 7][0] != '/')
        return 64;
    for (int i = start + 1; i <= start + 4; ++i) {
        if (argv[i][0] != '/') {
            return 64;
        }
    }
    try {
        const std::string_view library(argv[start + 4]);
        const std::string_view runtime = paired ? argv[start + 7] : "";
        struct stat runtime_identity{};
        const bool have_runtime =
            paired && stat(argv[start + 7], &runtime_identity) == 0 && S_ISREG(runtime_identity.st_mode);
        bool inject = library.find(':') == library.npos &&
                      (!paired || (runtime != library && runtime.find(':') == runtime.npos)) &&
                      capie::launcher::macos_child::enabled(read_playset(argv[start + 3])) &&
                      capie::launcher::macos_child::artifacts_verified(
                          {hash_file(argv[start + 1]), hash_file(argv[start + 4]),
                           paired ? hash_file(argv[start + 7]) : std::string{}},
                          {game_hash, argv[start + 5], paired ? argv[start + 8] : ""}, paired);
        struct stat own_identity{};
        const bool have_identity = stat(argv[start + 4], &own_identity) == 0 && S_ISREG(own_identity.st_mode);
        if (paired && have_identity && have_runtime && own_identity.st_dev == runtime_identity.st_dev &&
            own_identity.st_ino == runtime_identity.st_ino)
            inject = false;
        // Relative insertion paths resolve after chdir, against the child's game cwd.
        // stat follows symlinks and also recognizes hard links; this is a snapshot,
        // not a guarantee against concurrent replacement before dyld loads the child.
        const auto own_alias = [&](std::string_view candidate) {
            if (!have_identity && !have_runtime) {
                return false;
            }
            std::string path(candidate);
            if (path.front() != '/') {
                path = std::string(argv[start + 2]) + '/' + path;
            }
            struct stat identity{};
            return stat(path.c_str(), &identity) == 0 && ((have_identity && identity.st_dev == own_identity.st_dev &&
                                                           identity.st_ino == own_identity.st_ino) ||
                                                          (have_runtime && identity.st_dev == runtime_identity.st_dev &&
                                                           identity.st_ino == runtime_identity.st_ino));
        };
        const auto dyld =
            paired ? capie::launcher::macos_child::libraries_pair(env("DYLD_INSERT_LIBRARIES"),
                                                                  env("STEAM_DYLD_INSERT_LIBRARIES"), library, runtime,
                                                                  inject, own_alias)
                   : capie::launcher::macos_child::libraries(
                         env("DYLD_INSERT_LIBRARIES"), env("STEAM_DYLD_INSERT_LIBRARIES"), library, inject, own_alias);
        const int env_result =
            dyld.empty() ? unsetenv("DYLD_INSERT_LIBRARIES") : setenv("DYLD_INSERT_LIBRARIES", dyld.c_str(), 1);
        if (env_result != 0) {
            std::perror("CAPIE child: environment");
            return 71;
        }
        if (!inject) {
            std::fputs("CAPIE child: gate/artifact check disabled CAPIE; launching without this library\n", stderr);
        }
        if (chdir(argv[start + 2]) != 0) {
            std::perror("CAPIE child: game cwd");
            return 72;
        }
        std::vector<char*> args;
        args.push_back(argv[start + 1]);
        for (int i = 1; i < start; ++i) {
            args.push_back(argv[i]);
        }
        for (int i = delimiter + 1; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        args.push_back(nullptr);
        execv(argv[start + 1], args.data());
        std::perror("CAPIE child: exec");
        return 72;
    } catch (...) {
        // Allocation failure cannot safely reconstruct foreign loader entries or argv.
        std::fputs("CAPIE child: could not prepare launch; game was not started\n", stderr);
        return 71;
    }
}
