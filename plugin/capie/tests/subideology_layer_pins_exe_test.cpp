/**
 * @file subideology_layer_pins_exe_test.cpp
 * @brief Checks the ruling-party ideology name layer pins (`subideology_layer_pins.hpp`) and its RTTI table against the pinned `hoi4.exe` FILE.
 *
 * Maps the file's sections into a private buffer at their RVAs and runs the production check
 * `pins::first_failed_pin` with data reads at the preferred base (the vtable slot holds a
 * preferred-base pointer), then `core::first_wrong_class` on `subideology_vtable_classes`. Then flips
 * one byte of a signature, of instruction pins, of a target displacement and of the slot, and
 * expects the matching id. Without an argument it only checks that every table entry decodes.
 * No process is touched; the EXE is read as a file.
 */
#include "../src/core/game_build.hpp"
#include "../src/core/rtti.hpp"
#include "../src/features/country/subideology_layer_pins.hpp"

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

namespace pins = capie::country::subideology::pins;
using capie::core::uptr;

/// @brief Memory policy over the mapped image at the preferred base.
struct MappedMemory {
    const std::vector<unsigned char>* image{};  ///< Mapped image (index = RVA).

    /**
     * @brief Reads bytes at a preferred-base address.
     * @param address Address.
     * @param out Destination.
     * @param size Byte count.
     * @return `false` outside the image.
     */
    bool read(uptr address, void* out, std::size_t size) const noexcept {
        if (address < capie::core::game_preferred_base) {
            return false;
        }
        const auto rva = address - capie::core::game_preferred_base;
        if (rva > image->size() || size > image->size() - rva) {
            return false;
        }
        std::memcpy(out, image->data() + rva, size);
        return true;
    }
};

// Corrupted RVAs are literal expected values of the test cases.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Checks that every table entry decodes and the table sizes match.
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
    check(std::size(pins::signatures) == 1 && std::size(pins::instruction_pins) == 11 &&
              std::size(pins::target_pins) == 7 && std::size(pins::slot_pins) == 1,
          "table sizes 1 / 11 / 7 / 1");
    for (const auto& entry : capie::core::subideology_vtable_classes) {
        check(std::strncmp(entry.mangled, ".?AV", 4) == 0 && entry.vtable_rva != 0, std::string("shape ") + entry.id);
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
 * @brief Runs the production checks on the mapped file and on corrupted copies.
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
    const MappedMemory memory{&image};
    const auto base = capie::core::game_preferred_base;
    const char* failed = pins::first_failed_pin(text, memory, base);
    check(failed == nullptr, std::string("all ideology-name-layer pins pass; first failure: ") + (failed ? failed : "none"));
    const char* wrong = capie::core::first_wrong_class(memory, base, capie::core::subideology_vtable_classes);
    check(wrong == nullptr, std::string("ideology-name RTTI table; first wrong: ") + (wrong ? wrong : "none"));

    const auto flip = [&](std::size_t rva, const char* expected) {
        image[rva] ^= 0x01U;
        const char* result = pins::first_failed_pin(text, memory, base);
        check(result != nullptr && std::string(result) == expected,
              std::string("corrupt byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    flip(0x1622B90 + 44, "subideology.panel");        // +5F0 of the idler read
    flip(0x1622BBA + 15, "panel.idler_tag");           // slot 0xA0
    flip(0x1622BE0 + 3, "panel.ruling_party");         // +0xD0
    flip(0x1622C65 + 3, "panel.ideology_object");      // +0x18
    flip(0x66A4E6 + 3, "ideology.key");                // capacity +0x30
    flip(0x12302BC + 7, "party.ctor_ideology");        // party +0x18
    flip(0x121D992 + 3, "ideology_group.ctor_key");    // token +0x18
    flip(0xB8E373 + 1, "idler.shown_tag");             // 0x524
    flip(0x1622C6A, "panel.calls_localise");
    flip(0x22AE0FA, "formatter.calls_lookup");
    flip(0xB8E36F, "idler.shown_tag_state");
    flip(0x29E5828 + 0xA0, "idler.shown_tag_slot");
    check(pins::first_failed_pin(text, memory, base) == nullptr, "restored image passes again");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main(int argc, char** argv) {
    try {
        table_tests();
        if (argc > 1) {
            exe_tests(argv[1]);
        } else {
            static_cast<void>(std::printf("ideology name layer pins: no EXE given, table checks only\n"));
        }
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("ideology name layer pins exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
