/**
 * @file window_pins_exe_test.cpp
 * @brief Checks the scripted-window layout pins (`window_pins.hpp`) against the pinned `hoi4.exe` FILE.
 *
 * Without the EXE: every pin decodes, and the displacement inside each store equals the layout
 * constant it proves (`core/game_layout.hpp`), so a changed constant fails here even offline.
 * With the EXE: maps the file's sections into a private buffer at their RVAs, runs the production
 * check `window_pins::first_failed_pin` on it, then flips one byte of each pin and expects its id.
 * No process is touched; the EXE is read as a file.
 */
#include "../src/core/game_build.hpp"
#include "../src/features/editor/window_pins.hpp"

#include <array>
#include <cstdint>
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

namespace pins = capie::editor::window_pins;
namespace off = capie::core::off;
using capie::core::uptr;

// Byte positions inside the pins are literal expected values of the test cases.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

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

/// @brief Where a pin encodes a layout constant: pin id, byte position of the 32-bit displacement, constant.
struct Field {
    const char* id;        ///< Pin id.
    std::size_t at;        ///< Byte position of the displacement in the pin.
    std::uintptr_t value;  ///< Layout constant it must equal.
};

/// @brief The layout constants each store proves.
constexpr Field fields[] = {
    {"tick.window_desired", 2, off::window_desired},       {"window_ctor.idler", 3, off::window_idler},
    {"window_ctor.model", 3, off::window_model},           {"window_ctor.manager", 3, off::window_manager},
    {"window_ctor.root_zero", 5, off::window_root},        {"window_ctor.parent_zero", 3, off::window_parent},
    {"window_ctor.parent_sg_zero", 3, off::window_parent_sg}, {"window_ctor.context", 3, off::window_context},
    {"window_ctor.parent_seen", 3, off::window_parent_seen},
};

/**
 * @brief Every pin decodes, and each displacement is the layout constant it proves.
 */
void table_tests() {
    for (const auto& field : fields) {
        const capie::core::InstructionPin* pin = nullptr;
        for (const auto& entry : pins::instruction_pins) {
            pin = std::string(entry.id) == field.id ? &entry : pin;
        }
        std::array<unsigned char, capie::core::max_pattern_bytes> bytes{};
        const auto size = pin != nullptr ? capie::core::decode_hex(pin->hex, bytes.data(), bytes.size()) : 0;
        std::uint32_t displacement = 0;
        if (size >= field.at + sizeof(displacement)) {
            std::memcpy(&displacement, bytes.data() + field.at, sizeof(displacement));
        }
        check(size != 0 && displacement == field.value, std::string("pin encodes its layout constant: ") + field.id);
    }
    check(std::size(fields) == pins::expected_instruction_pins, "every instruction pin has a layout constant");
    check(off::window_desired == off::window_parent_seen + 1, "parent-seen word covers the desired byte");
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
    const MappedMemory memory{&image};
    const auto base = capie::core::game_preferred_base;
    const char* failed = pins::first_failed_pin(text, memory, base);
    check(failed == nullptr, std::string("all window pins pass; first failure: ") + (failed ? failed : "none"));

    const auto flip = [&](std::size_t rva, const char* expected) {
        image[rva] ^= 0x01U;
        const char* result = pins::first_failed_pin(text, memory, base);
        check(result != nullptr && std::string(result) == expected,
              std::string("corrupt byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    for (const auto& pin : pins::instruction_pins) {
        flip(pin.rva + 2, pin.id);
    }
    for (const auto& pin : pins::target_pins) {
        flip(pin.displacement_rva, pin.id);
    }
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
            static_cast<void>(std::printf("window pins: no EXE given, table checks only\n"));
        }
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("window pins exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
