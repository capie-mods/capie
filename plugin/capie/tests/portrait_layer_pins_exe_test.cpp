/**
 * @file portrait_layer_pins_exe_test.cpp
 * @brief Checks the portrait-layer pins (`portrait_layer_pins.hpp`) against the pinned `hoi4.exe` FILE.
 *
 * Maps the file's sections into a private buffer at their RVAs (no relocation: every pinned
 * byte range is free of DIR64 entries, checked with recore.py) and runs the
 * production check `pins::first_failed_pin` on it. Then flips one byte of a signature, of an
 * instruction pin and of a target displacement and expects the matching id. Without an
 * argument it only checks that every table entry decodes. No process is touched; the EXE is
 * read as a file.
 */
#include "../src/features/country/portrait_layer_pins.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int checks = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter
int failures = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): test counter

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Printed on failure.
 */
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label.c_str()));
    }
}

namespace pins = capie::country::portrait::pins;

// Corrupted RVAs are literal expected values of the test cases.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Checks that every table entry decodes.
 */
void table_tests() {
    for (const auto& signature : pins::signatures) {
        capie::core::Pattern pattern{};
        check(capie::core::decode_pattern(signature.hex, pattern), std::string("decodes ") + signature.id);
    }
    for (const auto& pin : pins::instruction_pins) {
        std::array<unsigned char, capie::core::max_pattern_bytes> bytes{};
        check(capie::core::decode_hex(pin.hex, bytes.data(), bytes.size()) != 0, std::string("decodes ") + pin.id);
    }
}

/**
 * @brief Maps the EXE file's sections at their RVAs into `image`.
 * @param path EXE path.
 * @param image Receives the mapped image.
 * @return `false` when the file is not a readable PE32+.
 */
bool map_file(const char* path, std::vector<unsigned char>& image) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<unsigned char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, file.data(), sizeof(dos));
    IMAGE_NT_HEADERS64 nt{};
    if (dos.e_lfanew <= 0 || static_cast<std::size_t>(dos.e_lfanew) + sizeof(nt) > file.size()) {
        return false;
    }
    std::memcpy(&nt, file.data() + dos.e_lfanew, sizeof(nt));
    image.assign(nt.OptionalHeader.SizeOfImage, 0);
    std::memcpy(image.data(), file.data(), nt.OptionalHeader.SizeOfHeaders);
    const std::size_t table = static_cast<std::size_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                              nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, file.data() + table + (i * sizeof(section)), sizeof(section));
        const std::size_t size =
            section.SizeOfRawData < section.Misc.VirtualSize ? section.SizeOfRawData : section.Misc.VirtualSize;
        if (section.PointerToRawData + size > file.size() || section.VirtualAddress + size > image.size()) {
            return false;
        }
        std::memcpy(image.data() + section.VirtualAddress, file.data() + section.PointerToRawData, size);
    }
    return true;
}

/**
 * @brief Runs the production pin check on the mapped file and on corrupted copies.
 * @param path EXE path.
 */
void exe_tests(const char* path) {
    std::vector<unsigned char> image;
    if (!map_file(path, image)) {
        check(false, "map the EXE file");
        return;
    }
    capie::core::TextSection text{};
    check(capie::core::locate_text(image.data(), text) == capie::core::InitCode::ok, "locate .text");
    const char* failed = pins::first_failed_pin(text);
    check(failed == nullptr, std::string("all portrait-layer pins pass; first failure: ") + (failed ? failed : "none"));

    const auto flip = [&](std::size_t rva, const char* expected) {
        image[rva] ^= 0x01U;
        const char* result = pins::first_failed_pin(text);
        check(result != nullptr && std::string(result) == expected,
              std::string("corrupt byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    flip(0x1439B80 + 5, "portrait.resolve");
    flip(0x1438E40 + 6, "portrait.valid");
    flip(0xB96487 + 4, "ui.size_small_is_zero");      // [rsp+0x90]
    flip(0xB964C8 + 4, "ui.character_argument");      // [rsp+0x20]
    flip(0x1439C29 + 3, "resolve.character_id");      // +0x0C
    flip(0x1438E6D + 3, "valid.name");                // +0x08
    flip(0x102AB25 + 3, "character.write_portraits");  // +0x78
    flip(0x102ADAB + 3, "character.write_variables");  // +0xD8
    flip(0x1029766 + 3, "character.read_variables");   // +0xD8
    flip(0x717370 + 3, "leader.politics");            // +0xF90
    flip(0x2FDC37 + 3, "leader.ruling_party");        // +0xD0
    flip(0x1230500 + 2, "leader.first_role");         // +0x7C
    flip(0x2C1410 + 3, "leader.character");           // +0x08
    flip(0x2FDCB4 + 4, "effect.civilian_large");      // size 1
    flip(0xB964E1, "ui.calls_resolve");
    flip(0xB964EC, "ui.calls_valid");
    flip(0x2FDC33, "leader.calls_politics");
    flip(0x123050E, "leader.first_role_jumps");
    flip(0x2FDCC0, "effect.writes_portrait");
    check(pins::first_failed_pin(text) == nullptr, "restored image passes again");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main(int argc, char** argv) {
    try {
        table_tests();
        if (argc > 1) {
            exe_tests(argv[1]);
        } else {
            static_cast<void>(std::printf("portrait layer pins: no EXE given, table checks only\n"));
        }
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("portrait layer pins exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
