/** @file playset_recorder.cpp
 * @brief Harmless staged-child recorder: read configured playset, never execute the game.
 * Writes only booleans to a new /tmp/capie-playset-proof-PID.json with mode 0600.
 */
#include "../../src/launcher/macos_child_policy.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

/** @brief Record whether the child received the normal real playset path and could parse it. */
int main(int argc, char** argv) {
    const auto marker = capie::launcher::macos_child::config_start(argc, argv);
    if (!marker || !std::getenv("HOME")) {
        return 64;
    }
    const std::string expected = std::string(std::getenv("HOME")) +
        "/Documents/Paradox Interactive/Hearts of Iron IV/dlc_load.json";
    const char* path = argv[*marker + 3];
    const bool same_path = expected == path;
    // Do not read a supplied path unless it is the expected real HOI4 playset.
    const int input = same_path ? open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
    struct stat metadata {};
    bool readable = input >= 0 && fstat(input, &metadata) == 0 && S_ISREG(metadata.st_mode);
    std::string json;
    std::array<char, 4096> buffer {};
    while (readable) {
        const auto count = read(input, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 || json.size() + static_cast<std::size_t>(count) > 1024 * 1024) {
            readable = false;
            break;
        }
        if (count == 0) {
            break;
        }
        json.append(buffer.data(), static_cast<std::size_t>(count));
    }
    if (input >= 0) {
        close(input);
    }
    const bool valid = readable && capie::launcher::macos_child::valid_utf8(json) &&
        capie::launcher::parse_enabled_mods(json).has_value();
    const bool enabled = valid && capie::launcher::macos_child::enabled(json);
    std::array<char, 128> output {};
    std::snprintf(output.data(), output.size(), "/tmp/capie-playset-proof-%d.json", getpid());
    const int fd = open(output.data(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return 73;
    }
    const int result = dprintf(fd,
        "{\"same_real_playset_path\":%s,\"readable\":%s,\"valid\":%s,\"capie_enabled\":%s}\n",
        same_path ? "true" : "false", readable ? "true" : "false",
        valid ? "true" : "false", enabled ? "true" : "false");
    std::array<char, 128> ancestry {};
    std::snprintf(ancestry.data(), ancestry.size(), "/tmp/capie-playset-parent-%d.txt", getpid());
    const int parent_fd = open(ancestry.data(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (parent_fd >= 0) {
        dprintf(parent_fd, "%d %d\n", getpid(), getppid());
        close(parent_fd);
    }
    close(fd);
    return result < 0 ? 74 : 0;
}
