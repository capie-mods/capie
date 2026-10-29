/**
 * @file host_pins_exe_test.cpp
 * @brief Checks the host probe pins (`host_pins.hpp`) against the pinned `hoi4.exe` FILE.
 *
 * Maps the file's sections into a private buffer at their RVAs and runs the production check
 * `pins::first_failed_pin` on it, with data reads at the preferred base (vtable slots in the file
 * hold preferred-base pointers). Then flips one byte of a signature, of instruction pins, of the
 * RIP displacement and of a vtable slot and expects the matching id. Without an argument it only
 * checks that every table entry decodes. No process is touched; the EXE is read as a file.
 */
#include "../src/core/game_build.hpp"
#include "../src/features/host/host_pins.hpp"

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

namespace pins = capie::host::pins;
using capie::core::uptr;

// Corrupted RVAs are literal expected values of the test cases.
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
    const MappedMemory memory{&image};
    const auto base = capie::core::game_preferred_base;
    const char* failed = pins::first_failed_pin(text, memory, base);
    check(failed == nullptr, std::string("all host pins pass; first failure: ") + (failed ? failed : "none"));

    const auto flip = [&](std::size_t rva, const char* expected) {
        image[rva] ^= 0x01U;
        const char* result = pins::first_failed_pin(text, memory, base);
        check(result != nullptr && std::string(result) == expected,
              std::string("corrupt byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    flip(0x1F1A40 + 33 + 3, "host.get_local_human");  // displacement 0x104 inside the signature
    flip(0x17F880 + 3, "host.get_session");           // displacement 0x380
    flip(0x148D9AF + 3, "host.get_machine_id");        // displacement 0x84
    flip(0x22C3AB0 + 2, "host.session_init");
    flip(0x22D8E90 + 3, "host.is_from_host");  // host machine id 1
    flip(0x22C3AE4 + 2, "session.is_server");
    flip(0x22C3AEA + 3, "session.mode");
    flip(0x22C3AF2 + 6, "session.machine_id_1");
    flip(0x22C3B06 + 3, "session.backend");
    flip(0x1D638CF + 8, "lobby.session_is_server");
    flip(0x1F1A6F + 3, "gamestate.humans");
    flip(0x1F1A7C + 3, "human.machine_id");
    flip(0x1F1A8D + 3, "human.stride");
    flip(0x1A4169B + 3, "human.name");
    flip(0xC101D3 + 3, "human.ctor_machine_id");
    flip(0x19A6223, "app.global");
    flip(0x2796308 + 0x88, "app.get_session_slot");
    flip(0x29E5838 + 0x88, "ingame_idler.session_slot");
    check(pins::first_failed_pin(text, memory, base) == nullptr, "restored image passes again");
    // A second copy of GetMachineId's body exists (E56DDE): the plain body alone is not unique.
    capie::core::Pattern body{};
    check(capie::core::decode_pattern("8B81A4000000C3CC", body) && !capie::core::unique_at(text, 0xB54190, body),
          "GetMachineId body alone is not unique (padding byte needed)");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main(int argc, char** argv) {
    try {
        table_tests();
        if (argc > 1) {
            exe_tests(argv[1]);
        } else {
            static_cast<void>(std::printf("host pins: no EXE given, table checks only\n"));
        }
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("host pins exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
