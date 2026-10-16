/** @file macos_child_parent.mm
 * @brief Native external-staging parent; invokes original dowser without shell or Python.
 * Runtime/installation trust and concurrency acceptance remain separate from this entry point.
 */
#include "macos_child_staging.hpp"
#include <libproc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <vector>
#include <string_view>

namespace {
/// @brief Observed launcher collision; -1 means enumeration failed, 0 none, 1 present.
int launcher_running() {
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) return -1;
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 128);
    const int count = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (count <= 0 || static_cast<std::size_t>(count) >= pids.size() * sizeof(pid_t)) return -1;
    for (std::size_t i = 0; i < static_cast<std::size_t>(count) / sizeof(pid_t); ++i) {
        char path[PROC_PIDPATHINFO_MAXSIZE] {};
        if (pids[i] > 0 && proc_pidpath(pids[i], path, sizeof(path)) > 0 &&
            std::string_view(path).ends_with("/Contents/MacOS/Paradox Launcher")) return 1;
    }
    return 0;
}
/// @brief Compare canonical path components, avoiding string-prefix containment mistakes.
bool inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto p = path.begin();
    for (const auto& part : root) {
        if (p == path.end() || *p++ != part) return false;
    }
    return true;
}
/// @brief Bounded regular-file read; no FIFO blocking and no game/user-data writes.
NSData* read_settings(const std::filesystem::path& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return nil;
    struct stat info {};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 1024 * 1024) {
        close(fd); return nil;
    }
    NSMutableData* data = [NSMutableData data];
    char buffer[4096];
    bool valid = true;
    for (;;) {
        const auto size = read(fd, buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR) continue;
        if (size < 0 || (size > 0 && data.length + static_cast<std::size_t>(size) > 1024 * 1024)) {
            valid = false; break;
        }
        if (size == 0) break;
        [data appendBytes:buffer length:static_cast<std::size_t>(size)];
    }
    close(fd);
    return valid ? data : nil;
}
/// @brief Write a new settings file completely; a failure leaves only owned staging incomplete.
bool write_settings(const std::filesystem::path& path, NSData* data) {
    if (!data) return false;
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::size_t at = 0;
    const auto* bytes = static_cast<const char*>(data.bytes);
    while (at < data.length) {
        const auto n = write(fd, bytes + at, data.length - at);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return false; }
        at += static_cast<std::size_t>(n);
    }
    return close(fd) == 0;
}
}

/** @brief Stage and exec original dowser. @return 64 config, 71 preparation, 72 exec, 73 collision. */
int main(int argc, char** argv) {
    const bool paired = argc == 9 && std::string_view(argv[6]) == "--capie-runtime";
    if (argc != 6 && !paired) {
        std::fputs("usage: parent GAME CHILD DYLIB SHA256 NEW_STAGING_DIRECTORY [--capie-runtime RUNTIME SHA256]\n", stderr); return 64;
    }
    @autoreleasepool {
        @try {
            try {
                namespace stage = capie::launcher::macos_child::staging;
                for (const int index : {1, 2, 3, 5}) {
                    if (!stage::absolute([NSString stringWithUTF8String:argv[index]])) return 64;
                }
                const int running = launcher_running();
                if (running != 0) {
                    std::fputs("CAPIE parent: launcher instance present or enumeration failed; nothing started\n", stderr);
                    return 73;
                }
                const auto game = std::filesystem::canonical(argv[1]);
                const auto destination = std::filesystem::weakly_canonical(argv[5]);
                if (inside(destination, game)) return 64;
                NSDictionary* source = stage::parse(read_settings(game / "launcher-settings.json"));
                NSDictionary* settings = stage::prepare(source, [NSString stringWithUTF8String:game.c_str()],
                    NSHomeDirectory(), [NSString stringWithUTF8String:argv[2]],
                    [NSString stringWithUTF8String:argv[3]], [NSString stringWithUTF8String:argv[4]],
                    paired ? [NSString stringWithUTF8String:argv[7]] : nil,
                    paired ? [NSString stringWithUTF8String:argv[8]] : nil);
                if (!settings) return 64;
                const auto data = std::filesystem::weakly_canonical([settings[@"gameDataPath"] UTF8String]);
                if (inside(destination, data)) return 64;
                if (mkdir(destination.c_str(), 0700) != 0) return 71;
                NSData* encoded = [NSJSONSerialization dataWithJSONObject:settings options:NSJSONWritingPrettyPrinted error:nil];
                if (!write_settings(destination / "launcher-settings.json", encoded)) return 71;
                const auto dowser = (game / "dowser").string();
                const auto staging = destination.string();
                char* arguments[] = {const_cast<char*>(dowser.c_str()), const_cast<char*>("--pdxlGameDir"),
                    const_cast<char*>(staging.c_str()), const_cast<char*>("--gameDir"),
                    const_cast<char*>(staging.c_str()), nullptr};
                if (chdir(game.c_str()) != 0) return 72;
                execv(dowser.c_str(), arguments);
                std::perror("CAPIE parent: exec"); return 72;
            } catch (...) {
                std::fputs("CAPIE parent: preparation failed; nothing started\n", stderr); return 71;
            }
        } @catch (NSException* exception) {
            static_cast<void>(exception);
            std::fputs("CAPIE parent: invalid settings; nothing started\n", stderr); return 64;
        }
    }
}
