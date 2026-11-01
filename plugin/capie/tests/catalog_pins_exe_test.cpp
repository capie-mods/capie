/**
 * @file catalog_pins_exe_test.cpp
 * @brief Checks the runtime-catalog pins (`catalog/catalog_pins.hpp`) against the pinned `hoi4.exe` FILE.
 *
 * Maps the file's sections at their RVAs (no relocation) and runs the production checks
 * `first_failed_vp_pin` and `first_failed_sprite_pin`. Then flips one byte per kind (signature,
 * instruction pin, target displacement) of each group and expects that id, while the other group
 * still passes. Without an argument it only checks that every table entry decodes.
 */
#include "../src/features/catalog/catalog_pins.hpp"
#include "../src/features/catalog/catalog_source_pins.hpp"

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

namespace pins = capie::catalog::pins;

// Corrupted RVAs are literal expected values of the test cases.
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/**
 * @brief Checks that every table entry decodes.
 */
void table_tests() {
    for (const auto* table : {&pins::vp_signatures[0], &pins::sprite_signatures[0]}) {
        capie::core::Pattern pattern{};
        check(capie::core::decode_pattern(table->hex, pattern), std::string("decodes ") + table->id);
    }
    for (const auto& pin : pins::vp_instruction_pins) {
        std::array<unsigned char, capie::core::max_pattern_bytes> bytes{};
        check(capie::core::decode_hex(pin.hex, bytes.data(), bytes.size()) != 0, std::string("decodes ") + pin.id);
    }
    for (const auto& pin : pins::sprite_instruction_pins) {
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
    const char* failed = pins::first_failed_vp_pin(text);
    check(failed == nullptr, std::string("all VP pins pass; first failure: ") + (failed ? failed : "none"));
    failed = pins::first_failed_sprite_pin(text);
    check(failed == nullptr, std::string("all sprite pins pass; first failure: ") + (failed ? failed : "none"));
    const auto flip = [&](std::size_t rva, const char* expected, bool vp_group) {
        image[rva] ^= 0x01U;
        const char* result = vp_group ? pins::first_failed_vp_pin(text) : pins::first_failed_sprite_pin(text);
        const char* other = vp_group ? pins::first_failed_sprite_pin(text) : pins::first_failed_vp_pin(text);
        check(result != nullptr && std::string(result) == expected && other == nullptr,
              std::string("corrupt byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    flip(0xEEF270 + 5, "vp.province_name", true);
    flip(0xBDE7F8 + 2, "scope.state_id", true);          // +0x468
    flip(0x58295F + 2, "scope.set_state", true);         // +0xA8
    flip(0x3E8FDD + 2, "state.count", true);             // +0x2D4
    flip(0x3E8FE8 + 3, "state.array", true);             // +0x2C8
    flip(0xA2A17A + 2, "state.province_count", true);    // +0x24
    flip(0xA2A18B + 3, "state.provinces", true);         // +0x18
    flip(0xA2A197 + 3, "province.vp_read", true);        // +0x38
    flip(0xEF2746 + 2, "province.vp_write", true);       // +0x38
    flip(0xEEEDAB + 2, "province.id", true);             // +0xA4
    flip(0xEEF288 + 3, "province.custom_name", true);    // +0x50
    flip(0xEEF28F + 3, "province.custom_string", true);  // +0x40
    flip(0x1439295 + 1, "tls.thread_forbid", true);      // +0x10
    flip(0x1438B9F + 3, "string.big_threshold", true);
    flip(0x1438BA8 + 3, "string.big_extra", true);
    flip(0x1438BB7 + 3, "string.big_check", true);
    flip(0xBDE811, "scope.calls_set_state", true);
    flip(0x143926F, "tls.index", true);
    flip(0x1438BC1, "string.calls_delete", true);
    flip(0x248B4F0 + 5, "sprite.release", false);
    flip(0x22A7F94 + 2, "sprite.lazy_flag", false);      // +0x1D3
    flip(0x22A7FA0 + 2, "sprite.texture_id", false);     // +0x1A0
    flip(0x22A7FBA + 3, "sprite.database", false);       // +0x48
    flip(0x22A7FBE + 3, "sprite.texmgr", false);         // +0x170
    flip(0x22A7FC9 + 4, "sprite.options", false);
    flip(0x22A7FDD + 3, "sprite.path", false);           // +0x70
    flip(0x22A7FE9 + 2, "sprite.store_id", false);
    flip(0x22A7FF9 + 3, "sprite.store_texture", false);
    flip(0x22A8049 + 3, "sprite.store_size", false);     // +0xA4
    flip(0x248B51D + 1, "sprite.release_nolock", false);
    flip(0x22A7FE5, "sprite.calls_add_texture", false);
    flip(0x22A7FF5, "sprite.calls_get_texture", false);
    flip(0x22A8013, "sprite.calls_texture_size", false);
    flip(0x1439234, "sprite.database_global", false);
    flip(0x1439239, "sprite.calls_find", false);
    flip(0x22A782D, "sprite.set_file_releases", false);
    flip(0x22A7850, "sprite.set_file_adds", false);
    failed = pins::first_failed_source_pin(text);
    check(failed == nullptr, std::string("all source pins pass; first failure: ") + (failed ? failed : "none"));
    const auto flip_source = [&](std::size_t rva, const char* expected) {
        image[rva] ^= 0x01U;
        const char* result = pins::first_failed_source_pin(text);
        check(result != nullptr && std::string(result) == expected,
              std::string("corrupt source byte at ") + std::to_string(rva) + " -> " + (result ? result : "none"));
        image[rva] ^= 0x01U;
    };
    flip_source(0x2560AA0 + 5, "files.get_files");
    flip_source(0x146808 + 4, "files.allocator_slot");   // +0x10 of the array
    flip_source(0x14682E + 4, "files.array");
    flip_source(0x146823 + 4, "files.depth");
    flip_source(0x14683D + 4, "files.count");            // +0xC
    flip_source(0x146845 + 3, "files.stride");           // 0x40
    flip_source(0x2560AF5 + 3, "files.clears");
    flip_source(0x22A0ED0 + 4, "map.thunk");             // +0x100
    flip_source(0xA88D5A + 2, "map.bucket_count");       // +0x4
    flip_source(0xA88D5D + 3, "map.buckets");            // +0x8
    flip_source(0xA88D80 + 2, "map.node_object");
    flip_source(0xA88D86 + 3, "map.sprite_name");        // +0x10
    flip_source(0xA88DB4 + 3, "map.node_next");          // +0x8
    flip_source(0xBDF353 + 3, "scope.country_read");     // +0x3C8
    flip_source(0x58191A + 4, "scope.country_write");    // +0x8
    flip_source(0xC081BA + 10, "tag.text_array");        // +0x358
    flip_source(0x146804, "files.allocator");
    flip_source(0x146834, "files.calls_get_files");
    flip_source(0x22A0ED8, "map.thunk_jumps");
    flip_source(0xBDF365, "scope.calls_set_country");
    check(pins::first_failed_vp_pin(text) == nullptr && pins::first_failed_sprite_pin(text) == nullptr &&
          pins::first_failed_source_pin(text) == nullptr,
          "restored image passes again");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

}  // namespace

int main(int argc, char** argv) {
    try {
        table_tests();
        if (argc > 1) {
            exe_tests(argv[1]);
        } else {
            static_cast<void>(std::printf("catalog pins: no EXE given, table checks only\n"));
        }
    } catch (...) {
        ++failures;
    }
    static_cast<void>(std::printf("catalog pins exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}