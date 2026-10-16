/** @file macos_log_test.cpp
 * @brief Real temporary-file logging, privacy channels and concurrent whole lines; no game.
 */
#include "../src/core/log.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
}
int main() {
    namespace log = capie::core::logging;
    namespace platform = capie::core::platform;
    log::write_log("before initialization");
    char path[] = "/tmp/capie-log-test.XXXXXX";
    const auto fd = mkstemp(path);
    if (fd < 0) return 1;
    unlink(path); // Only this owned fixture; the fd retains it until close.
    log::log_file = fd;
    log::log_line(log::Channel::debug, "PRIVATE_DEBUG_PAYLOAD");
    log::log_line(log::Channel::lifecycle, "lifecycle");
    std::atomic<unsigned long> other_id{0};
    const auto main_id = platform::thread_id();
    std::thread identity([&] { other_id = platform::thread_id(); });
    identity.join();
    check(main_id != 0 && other_id != 0 && main_id != other_id, "distinct native thread ids");
    const auto before = platform::monotonic_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(platform::monotonic_ms() >= before, "monotonic timestamps");
    std::vector<std::thread> writers;
    for (unsigned i = 0; i < 4; ++i) writers.emplace_back([] {
        for (unsigned j = 0; j < 100; ++j) log::write_log("whole_line\r\n");
    });
    for (auto& writer : writers) writer.join();
    log::log_file = -1;
    check(lseek(fd, 0, SEEK_SET) == 0, "seek owned log");
    std::string contents;
    char buffer[4096];
    ssize_t size;
    while ((size = read(fd, buffer, sizeof(buffer))) > 0) contents.append(buffer, static_cast<std::size_t>(size));
    close(fd);
    check(size == 0, "complete log read");
    check((contents.find("PRIVATE_DEBUG_PAYLOAD") != std::string::npos) == log::debug_diagnostics,
          "debug channel follows build flavor");
    unsigned lines = 0;
    bool whole = true;
    for (std::size_t pos = 0; pos < contents.size();) {
        const auto end = contents.find("\r\n", pos);
        if (end == std::string::npos) { whole = false; break; }
        const auto line = contents.substr(pos, end - pos);
        if (line.starts_with("whole_line ")) {
            ++lines;
            whole = whole && line.find("monotonic_ms=") != std::string::npos &&
                    line.find("thread=") != std::string::npos && line.find("whole_line", 1) == std::string::npos;
        } else whole = whole && (line.starts_with("lifecycle ") || line.starts_with("PRIVATE_DEBUG_PAYLOAD "));
        pos = end + 2;
    }
    check(whole && lines == 400, "concurrent records and suffixes stay whole");
    check(log::stored_bytes(-1, 5) == 0 && log::stored_bytes(12, 5) == 4 && log::stored_bytes(2, 0) == 0,
          "format truncation bounds");
    log::write_log("after close");
    std::printf("macOS log: %u checks, %u failures (debug=%d)\n", checks, failures, log::debug_diagnostics);
    return failures ? 1 : 0;
}
