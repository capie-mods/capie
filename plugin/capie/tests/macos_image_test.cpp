/**
 * @file macos_image_test.cpp
 * @brief Offline Mach-O parser/profile tests; optionally reads a game file, never executes it.
 */
#include "../src/core/macos_image.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace {
/// @brief Number of assertions executed.
unsigned checks = 0;
/// @brief Number of failed assertions.
unsigned failures = 0;
/** @brief Records a result. @param ok Result. @param label Failure description. */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
/**
 * @brief Writes a POD fixture field.
 * @tparam T POD type. @param bytes Fixture. @param offset Byte offset. @param value Field.
 */
template <class T>
void put(std::vector<unsigned char>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
/** @brief Creates a minimal two-segment executable. @return Mutable bytes. */
std::vector<unsigned char> fixture() {
    std::vector<unsigned char> bytes(512);
    mach_header_64 header{};
    header.magic = MH_MAGIC_64;
    header.cputype = CPU_TYPE_X86_64;
    header.cpusubtype = CPU_SUBTYPE_X86_64_ALL;
    header.filetype = MH_EXECUTE;
    header.ncmds = 2;
    header.sizeofcmds = 2 * sizeof(segment_command_64);
    put(bytes, 0, header);
    segment_command_64 text{};
    text.cmd = LC_SEGMENT_64;
    text.cmdsize = sizeof(text);
    std::memcpy(text.segname, "__TEXT", 7);
    text.vmaddr = 0x100000000;
    text.vmsize = 4096;
    text.filesize = 256;
    text.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    text.maxprot = text.initprot;
    put(bytes, sizeof(header), text);
    segment_command_64 data = text;
    std::memset(data.segname, 0, sizeof(data.segname));
    std::memcpy(data.segname, "__DATA", 7);
    data.vmaddr += 4096;
    data.fileoff = 256;
    data.initprot = VM_PROT_READ | VM_PROT_WRITE;
    data.maxprot = data.initprot;
    put(bytes, sizeof(header) + sizeof(text), data);
    return bytes;
}
}  // namespace

/** @brief Runs fixtures and optional exact-file verification. @param argc Count. @param argv Arguments. @return Test status. */
int main(int argc, char** argv) {
    mach_header loaded[3]{};
    loaded[0].filetype = loaded[1].filetype = MH_DYLIB;
    loaded[2].filetype = MH_EXECUTE;
    const auto header_at = [&](std::uint32_t i) { return &loaded[i]; };
    std::uint32_t selected = 99;
    check(capie::core::macos::image::executable_index(3, header_at, selected) && selected == 2,
          "inserted libraries may precede unique executable");
    check(!capie::core::macos::image::executable_index(2, header_at, selected), "missing executable refused");
    loaded[0].filetype = MH_EXECUTE;
    check(!capie::core::macos::image::executable_index(3, header_at, selected), "ambiguous executable refused");
    check(!capie::core::macos::image::executable_index(1,
          [](std::uint32_t) -> const mach_header* { return nullptr; }, selected), "null image refused");
    namespace image = capie::core::macos::image;
    auto bytes = fixture();
    image::Image parsed{};
    check(image::parse(bytes, 0, parsed), "valid x86_64 executable");
    check(parsed.count == 2, "two segments");
    check(parsed.executable_range(0x100000008, 8), "text pin lies in executable segment");
    check(!parsed.executable_range(0x100001008, 8), "data cannot be a code pin");
    check(!parsed.executable_range(0x100000100, 1), "executable zero-fill cannot be a code pin");
    check(!parsed.executable_range(0x100000000, 0), "empty executable pin rejected");
    std::size_t offset = 99;
    std::uintptr_t runtime = 0;
    check(parsed.runtime_virtual_address(0x100001200, 8, runtime) && runtime == 0x100001200,
          "readable BSS target accepted");
    check(!parsed.runtime_virtual_address(0x100001200, 8, runtime, true), "data BSS is not executable");
    check(parsed.runtime_virtual_address(0x100000008, 8, runtime, true), "read-execute target accepted");
    check(!parsed.runtime_virtual_address(0x100002000, 1, runtime), "target past VM segment rejected");
    check(!parsed.runtime_virtual_address(0x100001fff, 2, runtime), "target crossing VM end rejected");
    check(!parsed.runtime_virtual_address(0x100001200, 0, runtime), "empty virtual target rejected");
    auto unreadable = parsed;
    unreadable.segments[1].initprot = 0;
    check(!unreadable.runtime_virtual_address(0x100001200, 8, runtime), "unreadable target rejected");
    check(parsed.file_range(0x100000008, 8, offset) && offset == 8, "translate file-backed VA");
    check(parsed.file_range(0x100001008, 8, offset) && offset == 264, "translate second segment");
    check(!parsed.file_range(0x100000100, 1, offset), "refuse zero-fill tail");
    check(!parsed.file_range(0x1000000ff, 2, offset), "refuse cross-file-range");
    check(!parsed.file_range(0x100000000, 0, offset), "refuse empty pin");
    check(!parsed.file_range(std::numeric_limits<std::uintptr_t>::max(), 2, offset), "refuse range overflow");
    check(image::parse(bytes, 4096, parsed) && parsed.runtime_address(0x100000008, 8, runtime) &&
              runtime == 0x100001008, "positive slide");
    check(image::parse(bytes, -4096, parsed) && parsed.runtime_address(0x100000008, 8, runtime) &&
              runtime == 0xfffff008, "negative slide without underflow");
    check(!image::parse(bytes, std::numeric_limits<std::int64_t>::min(), parsed), "reject slide underflow");
    check(!image::verify_game(bytes, 0, parsed), "synthetic file cannot pass pinned hash");
    check(parsed.count == 0, "failed verification clears output");
    const auto mutate = [&](std::size_t where, auto value, const char* label) {
        auto changed = fixture();
        put(changed, where, value);
        check(!image::parse(changed, 0, parsed) && parsed.count == 0, label);
    };
    mutate(offsetof(mach_header_64, magic), std::uint32_t{MH_CIGAM_64}, "reject swapped endian");
    mutate(offsetof(mach_header_64, cputype), cpu_type_t{CPU_TYPE_ARM64}, "reject arm64 game");
    mutate(offsetof(mach_header_64, filetype), std::uint32_t{MH_DYLIB}, "reject dylib");
    mutate(offsetof(mach_header_64, ncmds), std::uint32_t{4097}, "reject excessive command count");
    mutate(offsetof(mach_header_64, sizeofcmds), std::uint32_t{500}, "reject commands past file");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, cmdsize), std::uint32_t{0}, "reject zero command size");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, cmdsize), std::uint32_t{9}, "reject unaligned command size");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, nsects), std::uint32_t{1}, "reject missing section structure");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, filesize), std::uint64_t{513}, "reject segment past file");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, vmsize), std::uint64_t{128}, "reject file larger than memory");
    mutate(sizeof(mach_header_64) + offsetof(segment_command_64, vmaddr), std::uint64_t{0x200000000}, "reject wrong preferred base");
    mutate(sizeof(mach_header_64) + sizeof(segment_command_64) + offsetof(segment_command_64, vmaddr),
           std::uint64_t{0x100000080}, "reject overlapping VM segments");
    mutate(sizeof(mach_header_64) + sizeof(segment_command_64) + offsetof(segment_command_64, fileoff),
           std::uint64_t{128}, "reject overlapping file segments");
    mutate(sizeof(mach_header_64) + sizeof(segment_command_64) + offsetof(segment_command_64, vmaddr),
           std::numeric_limits<std::uint64_t>::max() - 7, "reject segment address overflow");
    check(!image::parse(std::span<const unsigned char>(bytes.data(), 31), 0, parsed), "reject truncated header");
    mutate(offsetof(mach_header_64, ncmds), std::uint32_t{1}, "reject unconsumed load commands");
    auto high = fixture();
    put(high, sizeof(mach_header_64) + sizeof(segment_command_64) + offsetof(segment_command_64, vmaddr),
        std::numeric_limits<std::uint64_t>::max() - 8192);
    check(!image::parse(high, 8192, parsed), "reject positive slide range overflow");
    auto section_bytes = fixture();
    segment_command_64 text_segment{};
    segment_command_64 data_segment{};
    std::memcpy(&text_segment, section_bytes.data() + sizeof(mach_header_64), sizeof(text_segment));
    std::memcpy(&data_segment, section_bytes.data() + sizeof(mach_header_64) + sizeof(text_segment), sizeof(data_segment));
    text_segment.cmdsize += sizeof(section_64);
    text_segment.nsects = 1;
    text_segment.filesize = 288;
    data_segment.fileoff = 288;
    data_segment.filesize = 224;
    put(section_bytes, offsetof(mach_header_64, sizeofcmds),
        static_cast<std::uint32_t>(2 * sizeof(segment_command_64) + sizeof(section_64)));
    put(section_bytes, sizeof(mach_header_64), text_segment);
    const auto section_offset = sizeof(mach_header_64) + sizeof(segment_command_64);
    section_64 section{};
    section.addr = image::preferred_base + 256;
    section.size = 16;
    section.offset = 256;
    put(section_bytes, section_offset, section);
    put(section_bytes, section_offset + sizeof(section), data_segment);
    check(image::parse(section_bytes, 0, parsed), "accept bounded section");
    section.offset = 280;
    put(section_bytes, section_offset, section);
    check(!image::parse(section_bytes, 0, parsed), "reject section beyond file-backed segment");
    section.flags = S_ZEROFILL;
    section.addr = image::preferred_base + 4000;
    section.offset = 0;
    put(section_bytes, section_offset, section);
    check(image::parse(section_bytes, 0, parsed), "accept VM-only zero-fill section");
    section.addr = image::preferred_base + 4090;
    put(section_bytes, section_offset, section);
    check(!image::parse(section_bytes, 0, parsed), "reject section beyond VM segment");
    const std::array<unsigned char, 3> abc{'a', 'b', 'c'};
    check(image::sha256_matches(abc, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "SHA-256 known vector");
    check(!image::sha256_matches(abc, image::game_sha256), "SHA-256 mismatch");
    if (argc == 2) {
        std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
        check(stream.is_open(), "open provided game file read-only");
        const auto length = stream.tellg();
        // Bound this test's allocation even when the supplied path is not the game.
        check(length > 0 && length <= 1024 * 1024 * 1024, "file fits one GiB test limit");
        if (length <= 0 || length > 1024 * 1024 * 1024) {
            return 1;
        }
        std::vector<unsigned char> game(static_cast<std::size_t>(length));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(game.data()), static_cast<std::streamsize>(game.size()));
        check(stream.good() && stream.gcount() == static_cast<std::streamsize>(game.size()), "complete game file read");
        check(image::verify_game(game, 0, parsed), "actual game exact hash and Mach-O profile");
        if (!game.empty()) {
            game.back() ^= 1;
            check(!image::verify_game(game, 0, parsed), "actual image mutation fails closed");
        }
    } else if (argc == 1) {
        std::puts("SKIP actual game file: no path supplied; synthetic tests only");
    } else {
        std::fprintf(stderr, "usage: macos_image_test [game-file]\n");
        return 2;
    }
    std::printf("macOS image: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
