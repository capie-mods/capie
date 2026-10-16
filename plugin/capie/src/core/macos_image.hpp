/**
 * @file macos_image.hpp
 * @brief Read-only x86_64 Mach-O validation and exact HOI4 macOS file-profile check.
 *
 * This standalone parser consumes file bytes, never maps executable pages and never
 * enables hooks. The profile hash is required by verify_game; parse alone proves only
 * structure. Runtime slide is caller-supplied and must eventually come from the loaded
 * main image, not from an assumed fixed address. Per-feature instruction pins remain
 * necessary after this check. Windows code does not include this header.
 */
#pragma once
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/loader.h>
#include <mach/machine.h>
#include <mach/vm_prot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace capie::core::macos::image {

/// @brief Exact Steam HOI4 1.19.3.0 macOS executable, recorded in docs/macos/re-notes.md.
inline constexpr char game_sha256[] = "09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb";
/// @brief Preferred base of the pinned executable's __TEXT segment.
inline constexpr std::uint64_t preferred_base = 0x100000000;
/// @brief Upper bound on retained segments; exceeding it fails closed.
inline constexpr std::size_t max_segments = 64;

/** @brief Select the unique executable in a stable dyld image snapshot; inserted libraries may precede it. */
template <class HeaderAt>
bool executable_index(std::uint32_t count, HeaderAt header_at, std::uint32_t& index) noexcept {
    bool found = false;
    std::uint32_t selected = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto* header = header_at(i);
        if (!header) return false;
        if (header->filetype != MH_EXECUTE) continue;
        if (found) return false;
        selected = i;
        found = true;
    }
    if (found) index = selected;
    return found;
}

/**
 * @brief Checks a byte range without overflow.
 * @param offset Starting offset. @param size Byte length. @param extent Container length.
 * @return True when the complete range lies inside the container.
 */
inline bool contains(std::uint64_t offset, std::uint64_t size, std::uint64_t extent) noexcept {
    return offset <= extent && size <= extent - offset;
}

/**
 * @brief Applies a signed slide without signed overflow or unsigned wrapping.
 * @param address Unslid address. @param slide Signed slide. @param out Receives slid address.
 * @return True if the resulting address is representable.
 */
inline bool apply_slide(std::uint64_t address, std::int64_t slide, std::uintptr_t& out) noexcept {
    static_assert(sizeof(std::uintptr_t) == 8);
    if (slide >= 0) {
        const auto amount = static_cast<std::uint64_t>(slide);
        if (address > std::numeric_limits<std::uintptr_t>::max() - amount) {
            return false;
        }
        out = address + amount;
    } else {
        const auto amount = static_cast<std::uint64_t>(-(slide + 1)) + 1;
        if (address < amount) {
            return false;
        }
        out = address - amount;
    }
    return true;
}

/// @brief Validated file and virtual ranges for one segment.
struct Segment {
    /// @brief Unslid virtual address.
    std::uint64_t address{};
    /// @brief Virtual extent including zero-fill.
    std::uint64_t memory_size{};
    /// @brief Starting file offset.
    std::uint64_t file_offset{};
    /// @brief File-backed extent.
    std::uint64_t file_size{};
    /// @brief Initial VM protection requested by the file; not a live-page protection query.
    vm_prot_t initprot{};
};

/// @brief Bounded description of validated file segments.
struct Image {
    /// @brief Retained segments; only the first count are valid.
    std::array<Segment, max_segments> segments{};
    /// @brief Number of validated segments.
    std::size_t count{};
    /// @brief Caller-supplied checked relocation slide.
    std::int64_t slide{};

    /**
     * @brief Resolves a nonempty pin range within one file-backed segment.
     * @param address Unslid VA. @param size Byte count. @param out Receives file offset.
     * @return True for a complete range with real file bytes, never zero-fill.
     */
    [[nodiscard]] bool file_range(std::uintptr_t address, std::size_t size, std::size_t& out) const noexcept {
        if (size == 0) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto& segment = segments[i];
            if (address >= segment.address && contains(address - segment.address, size, segment.file_size)) {
                out = static_cast<std::size_t>(segment.file_offset + address - segment.address);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Checks that a nonempty code pin lies inside file-backed executable bytes.
     * @param address Unslid VA. @param size Pin byte count.
     * @return True only for one segment declaring VM_PROT_EXECUTE in its initial protection.
     */
    [[nodiscard]] bool executable_range(std::uintptr_t address, std::size_t size) const noexcept {
        if (size == 0) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto& segment = segments[i];
            if ((segment.initprot & VM_PROT_EXECUTE) != 0 && address >= segment.address &&
                contains(address - segment.address, size, segment.file_size)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Resolves a readable virtual target, including a segment's zero-fill tail.
     * @param address Unslid VA. @param size Nonempty target range.
     * @param out Receives slid VA. @param executable Also require VM_PROT_EXECUTE.
     * @return True for a range in one readable segment with safe slide arithmetic.
     * @note Uses file-declared permissions, not a query of live process protections.
     */
    [[nodiscard]] bool runtime_virtual_address(std::uintptr_t address, std::size_t size,
                                               std::uintptr_t& out, bool executable = false) const noexcept {
        if (size == 0) {
            return false;
        }
        const auto required = VM_PROT_READ | (executable ? VM_PROT_EXECUTE : 0);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& segment = segments[i];
            if ((segment.initprot & required) == required && address >= segment.address &&
                contains(address - segment.address, size, segment.memory_size)) {
                return apply_slide(address, slide, out) &&
                       size <= std::numeric_limits<std::uintptr_t>::max() - out;
            }
        }
        return false;
    }

    /**
     * @brief Resolves a file-backed range to a checked loaded address.
     * @param address Unslid VA. @param size Pin byte count. @param out Receives slid VA.
     * @return True only when the range is file-backed and slide arithmetic is safe.
     */
    [[nodiscard]] bool runtime_address(std::uintptr_t address, std::size_t size, std::uintptr_t& out) const noexcept {
        std::size_t offset = 0;
        return file_range(address, size, offset) && apply_slide(address, slide, out) &&
               size <= std::numeric_limits<std::uintptr_t>::max() - out;
    }
};

/**
 * @brief Parses a thin little-endian x86_64 main executable with bounded segments and sections.
 * @param bytes Complete file contents. @param slide Runtime slide to validate.
 * @param out Receives image on success; cleared on every failure.
 * @return True for valid structure only; not a game-build identity check.
 */
inline bool parse(std::span<const unsigned char> bytes, std::int64_t slide, Image& out) noexcept {
    out = {};
    if (bytes.size() < sizeof(mach_header_64)) {
        return false;
    }
    mach_header_64 header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != MH_MAGIC_64 || header.cputype != CPU_TYPE_X86_64 || header.filetype != MH_EXECUTE ||
        header.ncmds == 0 || header.ncmds > 4096 ||
        !contains(sizeof(header), header.sizeofcmds, bytes.size())) {
        return false;
    }
    Image candidate{};
    candidate.slide = slide;
    bool have_text = false;
    const auto command_end = sizeof(header) + header.sizeofcmds;
    std::size_t cursor = sizeof(header);
    for (std::uint32_t i = 0; i < header.ncmds; ++i) {
        if (!contains(cursor, sizeof(load_command), command_end)) {
            return false;
        }
        load_command command{};
        std::memcpy(&command, bytes.data() + cursor, sizeof(command));
        if (command.cmdsize < sizeof(command) || command.cmdsize % 8 != 0 ||
            !contains(cursor, command.cmdsize, command_end)) {
            return false;
        }
        if (command.cmd == LC_SEGMENT_64) {
            if (command.cmdsize < sizeof(segment_command_64) || candidate.count == max_segments) {
                return false;
            }
            segment_command_64 segment{};
            std::memcpy(&segment, bytes.data() + cursor, sizeof(segment));
            if (segment.nsects > (command.cmdsize - sizeof(segment)) / sizeof(section_64) ||
                command.cmdsize != sizeof(segment) + segment.nsects * sizeof(section_64) ||
                segment.filesize > segment.vmsize || !contains(segment.fileoff, segment.filesize, bytes.size()) ||
                segment.vmsize > std::numeric_limits<std::uint64_t>::max() - segment.vmaddr) {
                return false;
            }
            const bool page_zero = std::memcmp(segment.segname, "__PAGEZERO\0\0\0\0\0\0", 16) == 0;
            if (page_zero && (segment.vmaddr != 0 || segment.filesize != 0 || segment.initprot != 0 ||
                              segment.maxprot != 0 || segment.nsects != 0)) {
                return false;
            }
            std::uintptr_t loaded = 0;
            if (!page_zero && (!apply_slide(segment.vmaddr, slide, loaded) ||
                               segment.vmsize > std::numeric_limits<std::uintptr_t>::max() - loaded)) {
                return false;
            }
            if (std::memcmp(segment.segname, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
                if (have_text || segment.vmaddr != preferred_base || segment.fileoff != 0 ||
                    segment.filesize < command_end || (segment.initprot & (VM_PROT_READ | VM_PROT_EXECUTE)) !=
                                                         (VM_PROT_READ | VM_PROT_EXECUTE)) {
                    return false;
                }
                have_text = true;
            }
            for (std::size_t j = 0; j < candidate.count; ++j) {
                const auto& previous = candidate.segments[j];
                if ((segment.vmsize != 0 && previous.memory_size != 0 &&
                     segment.vmaddr < previous.address + previous.memory_size &&
                     previous.address < segment.vmaddr + segment.vmsize) ||
                    (segment.filesize != 0 && previous.file_size != 0 &&
                     segment.fileoff < previous.file_offset + previous.file_size &&
                     previous.file_offset < segment.fileoff + segment.filesize)) {
                    return false;
                }
            }
            for (std::uint32_t j = 0; j < segment.nsects; ++j) {
                section_64 section{};
                std::memcpy(&section, bytes.data() + cursor + sizeof(segment) + j * sizeof(section), sizeof(section));
                const auto type = section.flags & SECTION_TYPE;
                const bool zero_fill = type == S_ZEROFILL || type == S_GB_ZEROFILL || type == S_THREAD_LOCAL_ZEROFILL;
                if (section.addr < segment.vmaddr || !contains(section.addr - segment.vmaddr, section.size, segment.vmsize) ||
                    (!zero_fill && (section.offset < segment.fileoff ||
                                    !contains(section.offset - segment.fileoff, section.size, segment.filesize)))) {
                    return false;
                }
            }
            candidate.segments[candidate.count++] = {
                segment.vmaddr, segment.vmsize, segment.fileoff, segment.filesize, segment.initprot};
        }
        cursor += command.cmdsize;
    }
    if (cursor != command_end || !have_text) {
        return false;
    }
    out = candidate;
    return true;
}

/**
 * @brief Hashes all bytes with system CommonCrypto and compares a lowercase SHA-256 string.
 * @param bytes Complete file bytes. @param expected Expected 64-character lowercase hexadecimal digest.
 * @return True only when every digest nibble matches.
 */
inline bool sha256_matches(std::span<const unsigned char> bytes, std::string_view expected) noexcept {
    if (expected.size() != CC_SHA256_DIGEST_LENGTH * 2) {
        return false;
    }
    CC_SHA256_CTX context{};
    if (CC_SHA256_Init(&context) != 1) {
        return false;
    }
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        const auto remaining = bytes.size() - cursor;
        const auto chunk = static_cast<CC_LONG>(remaining > std::numeric_limits<CC_LONG>::max()
                                                   ? std::numeric_limits<CC_LONG>::max() : remaining);
        if (CC_SHA256_Update(&context, bytes.data() + cursor, chunk) != 1) {
            return false;
        }
        cursor += chunk;
    }
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256_Final(digest.data(), &context) != 1) {
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (expected[2 * i] != hex[digest[i] >> 4] || expected[2 * i + 1] != hex[digest[i] & 15]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Verifies the exact pinned game hash and validated image structure together.
 * @param bytes Complete file bytes. @param slide Loaded main-image slide.
 * @param out Receives verified ranges; cleared on every failure.
 * @return True only for this exact build and safe address arithmetic; no hooks are enabled.
 */
inline bool verify_game(std::span<const unsigned char> bytes, std::int64_t slide, Image& out) noexcept {
    out = {};
    return sha256_matches(bytes, game_sha256) && parse(bytes, slide, out);
}

}  // namespace capie::core::macos::image
