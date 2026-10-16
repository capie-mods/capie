/** @file macos_probe.cpp
 * @brief Opt-in read-only diagnostic initializer. No hooks, activation, threads or CAPIE API exports.
 *
 * CAPIE_DIAGNOSTIC_DIR must name an existing private directory owned by this user.
 * The sole output is an exclusive mode-0600 JSON file there, without argv or executable paths.
 * Runtime token snapshots do not establish completion of game initialization.
 */
#include "../core/macos_image.hpp"
#include "../core/macos_memory.hpp"
#include "../core/macos_pins.hpp"
#include "../core/macos_profile.generated.hpp"
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
namespace image = capie::core::macos::image;
namespace pins = capie::core::macos::pins;
/// @brief Fully bounded report with no game strings or paths.
struct Report {
    const char* status = "foreign_or_unreadable_executable"; ///< Earliest failed identity gate.
    bool exact_sha = false; ///< Exact file profile matched.
    bool loaded_identity = false; ///< Main base and header/load commands matched the file.
    bool lookup[3] = {}; ///< Three exact fixed 14-byte prefixes.
    bool token_read[2] = {}; ///< Snapshot readability, not initialized-value acceptance.
    std::uint32_t tokens[2] = {}; ///< Raw current values, only meaningful when read=true.
    unsigned profile_checks = 0; ///< Attempted checks, including relative-target guards.
    unsigned profile_matches = 0; ///< Complete successful checks only.
    int profile_first_failure = -1; ///< Index in bytes, pointers, relatives order; no game data.
};
/** @brief Check an exact file mapping and loaded main image, then read a small diagnostic subset.
 * @param bytes Read-only main executable bytes. @param header Dyld main header. @param slide Actual dyld slide.
 * @param report Output snapshot, never an activation decision.
 */
void inspect(std::span<const unsigned char> bytes, const mach_header* header,
             std::int64_t slide, Report& report) noexcept {
    image::Image profile;
    if (!image::verify_game(bytes, slide, profile)) {
        return;
    }
    report.exact_sha = true;
    report.status = "loaded_identity_mismatch";
    std::uintptr_t expected_base = 0;
    if (!profile.runtime_address(0x100000000, sizeof(mach_header_64), expected_base) ||
        expected_base != reinterpret_cast<std::uintptr_t>(header)) {
        return;
    }
    mach_header_64 file_header{};
    std::memcpy(&file_header, bytes.data(), sizeof(file_header));
    constexpr std::size_t max_header = 131072;
    const std::size_t size = sizeof(file_header) + file_header.sizeofcmds;
    if (size > max_header || size > bytes.size()) {
        return;
    }
    std::array<unsigned char, max_header> loaded{};
    const capie::core::macos::ProcessMemory memory;
    if (!memory.read(expected_base, loaded.data(), size) ||
        std::memcmp(loaded.data(), bytes.data(), size) != 0) {
        return;
    }
    report.loaded_identity = true;
    constexpr unsigned char prefix_n[] = {
        0x55, 0x48, 0x89, 0xe5, 0x49, 0xb8, 0x25, 0x23, 0x22, 0x84, 0xe4, 0x9c, 0xf2, 0xcb};
    constexpr unsigned char prefix_z[] = {
        0x55, 0x48, 0x89, 0xe5, 0x48, 0xbe, 0x25, 0x23, 0x22, 0x84, 0xe4, 0x9c, 0xf2, 0xcb};
    report.lookup[0] = pins::bytes_match(memory, profile, {0x10288da90, prefix_n});
    report.lookup[1] = pins::bytes_match(memory, profile, {0x10288d420, prefix_z});
    report.lookup[2] = pins::bytes_match(memory, profile, {0x10288e4c0, prefix_z});
    const auto record = [&report](bool matched) {
        if (matched) {
            ++report.profile_matches;
        } else if (report.profile_first_failure < 0) {
            report.profile_first_failure = static_cast<int>(report.profile_checks);
        }
        ++report.profile_checks;
    };
    for (const auto& pin : capie::core::macos::profile::bytes) {
        record(pins::bytes_match(memory, profile, pin));
    }
    for (const auto& pin : capie::core::macos::profile::pointers) {
        record(pins::pointer_matches(memory, profile, pin));
    }
    for (const auto& pin : capie::core::macos::profile::relatives) {
        record(pins::relative_matches(memory, profile, pin));
    }
    constexpr std::uintptr_t token_addresses[] = {0x1035af598, 0x1035ae4b8};
    for (std::size_t i = 0; i < 2; ++i) {
        std::uintptr_t address = 0;
        report.token_read[i] = profile.runtime_virtual_address(token_addresses[i], 4, address) &&
                              memory.read(address, &report.tokens[i], 4);
    }
    report.status = report.lookup[0] && report.lookup[1] && report.lookup[2] &&
                    report.profile_checks != 0 && report.profile_checks == report.profile_matches ?
                    "full_static_profile_matches_not_activation" : "static_profile_mismatch";
}
/** @brief Read only the file dyld identifies as the main executable.
 * @param report Output identity and static/read-only observations.
 */
void probe(Report& report) noexcept {
    std::uint32_t index = 0;
    if (!image::executable_index(_dyld_image_count(), _dyld_get_image_header, index)) return;
    const auto* header = _dyld_get_image_header(index);
    const char* name = _dyld_get_image_name(index);
    if (!header || !name) {
        return;
    }
    const int descriptor = open(name, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return;
    }
    struct stat info{};
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 32 || info.st_size > 1073741824) {
        close(descriptor);
        return;
    }
    const auto size = static_cast<std::size_t>(info.st_size);
    void* mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    if (mapping != MAP_FAILED) {
        inspect({static_cast<const unsigned char*>(mapping), size}, header, _dyld_get_image_vmaddr_slide(index), report);
        munmap(mapping, size);
    }
    close(descriptor);
}
/** @brief Produce a bounded private diagnostic only when explicitly requested by environment. */
__attribute__((constructor)) void initialise() {
    const char* directory = std::getenv("CAPIE_DIAGNOSTIC_DIR");
    if (!directory || directory[0] != '/') {
        return;
    }
    const int folder = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (folder < 0) {
        return;
    }
    struct stat info{};
    if (fstat(folder,&info) != 0 || info.st_uid != geteuid() || (info.st_mode & 0077) != 0) {
        close(folder);
        return;
    }
    char filename[64];
    std::snprintf(filename, sizeof(filename), "capie-probe-%ld.json", static_cast<long>(getpid()));
    const int output = openat(folder, filename, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(folder);
    if (output < 0) {
        return;
    }
    Report report;
    probe(report);
    char text[1024];
    const int length = std::snprintf(text, sizeof(text),
        "{\"diagnostic_only\":true,\"status\":\"%s\",\"exact_sha\":%s,\"loaded_identity\":%s, "
        "\"lookup_prefix_matches\":[%s,%s,%s],\"token_expected\":\"not_checked_after_init\", "
        "\"profile_checks\":%u,\"profile_matches\":%u,\"profile_first_failure\":%d, "
        "\"token_reads\":[{\"read\":%s,\"value\":%u}, {\"read\":%s,\"value\":%u}]}\n",
        report.status, report.exact_sha ? "true" : "false", report.loaded_identity ? "true" : "false",
        report.lookup[0] ? "true" : "false", report.lookup[1] ? "true" : "false",
        report.lookup[2] ? "true" : "false",
        report.profile_checks, report.profile_matches, report.profile_first_failure,
        report.token_read[0] ? "true" : "false", report.tokens[0],
        report.token_read[1] ? "true" : "false", report.tokens[1]);
    if (length > 0 && static_cast<std::size_t>(length) < sizeof(text)) {
        std::size_t done = 0;
        while (done < static_cast<std::size_t>(length)) {
            const auto written = write(output, text+done, static_cast<std::size_t>(length)-done);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                break;
            }
            done += static_cast<std::size_t>(written);
        }
    }
    close(output);
}
}
