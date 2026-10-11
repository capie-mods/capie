/**
 * @file pe_image.hpp
 * @brief Locates the `.text` section of the loaded game image with bounded reads.
 *
 * Signature checks need the start and size of `.text` in memory. This file reads the
 * DOS header, the NT headers and the section table of the main module, checking every
 * read with `readable()` and every size against `SizeOfImage`. A malformed or foreign
 * image yields an error code; nothing is dereferenced past what was checked.
 *
 * It does not verify the executable hash (see `sha256.hpp`) and it does not look at
 * other sections. Both DLLs call it after the hash check, so on the pinned executable
 * every bounds check passes; on a damaged image it refuses with 11 or 12.
 */
#pragma once
#include "init_codes.hpp"
#include "memory.hpp"

#include <cstring>

namespace capie::core {

/// @brief Largest accepted `e_lfanew`; real images use a few hundred bytes.
inline constexpr LONG max_nt_header_offset = 65536;
/// @brief Largest accepted section count (PE limit is 96).
inline constexpr WORD max_section_count = 96;
/// @brief Section name of the code section, padded to 8 bytes as in the section table.
inline constexpr char text_section_name[IMAGE_SIZEOF_SHORT_NAME] = {'.', 't', 'e', 'x', 't', '\0', '\0', '\0'};

/// @brief Location of `.text` in the loaded image.
struct TextSection {
    const unsigned char* base{};  ///< Image base (module handle of the game).
    std::uint32_t rva{};          ///< Section RVA (`VirtualAddress`).
    std::uint32_t size{};         ///< Section size in memory (`Misc.VirtualSize`).
};

/**
 * @brief Finds `.text` in the image at `base`.
 * @param base Loaded image base (normally `GetModuleHandleW(nullptr)`).
 * @param out Receives the section location on success.
 * @return `InitCode::ok`, `bad_pe_header` (11) or `missing_text` (12).
 * @note Thread: any; reads only. When several sections are named `.text` the last one
 *       wins, matching the accepted binaries.
 */
inline InitCode locate_text(const unsigned char* base, TextSection& out) noexcept {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > max_nt_header_offset) {
        return InitCode::bad_pe_header;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.NumberOfSections > max_section_count) {
        return InitCode::bad_pe_header;
    }
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    if (!readable(sections, nt->FileHeader.NumberOfSections * sizeof(*sections))) {
        return InitCode::bad_pe_header;
    }
    const IMAGE_SECTION_HEADER* text = nullptr;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sections[i].Name, text_section_name, sizeof(text_section_name)) == 0) {
            text = sections + i;
        }
    }
    if (text == nullptr || text->VirtualAddress > nt->OptionalHeader.SizeOfImage ||
        text->Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - text->VirtualAddress ||
        !readable(base + text->VirtualAddress, text->Misc.VirtualSize)) {
        return InitCode::missing_text;
    }
    out = TextSection{base, text->VirtualAddress, text->Misc.VirtualSize};
    return InitCode::ok;
}

}  // namespace capie::core
