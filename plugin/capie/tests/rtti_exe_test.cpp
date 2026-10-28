/**
 * @file rtti_exe_test.cpp
 * @brief Checks every vtable class pin (`core/rtti.hpp`) against the pinned `hoi4.exe` FILE.
 *
 * Maps the file's sections into a private buffer at their RVAs and runs the production check
 * `core::first_wrong_class` with reads at the preferred base (the vtable's locator pointer in the
 * file is a preferred-base address). Then checks that a wrong class name, a wrong subobject offset
 * and a vtable moved by one slot are refused. Without an argument it only checks the tables'
 * shape. No process is touched; the EXE is read as a file.
 */
#include "../src/core/game_build.hpp"
#include "../src/core/rtti.hpp"

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

using capie::core::uptr;
using capie::core::VtableClass;

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

/**
 * @brief Checks the tables' shape without the EXE.
 */
void table_tests() {
    for (const auto& entry : capie::core::editor_vtable_classes) {
        check(std::strncmp(entry.mangled, ".?AV", 4) == 0 && entry.vtable_rva != 0, std::string("shape ") + entry.id);
    }
    for (const auto& entry : capie::core::core_vtable_classes) {
        check(std::strncmp(entry.mangled, ".?AV", 4) == 0 && entry.vtable_rva != 0, std::string("shape ") + entry.id);
    }
}

/**
 * @brief Runs the production check on the mapped file and on wrong expectations.
 * @param path EXE path.
 */
void exe_tests(const char* path) {
    std::vector<unsigned char> image;
    check(map_file(path, image), "map the EXE");
    if (image.empty()) {
        return;
    }
    const MappedMemory mem{&image};
    const uptr base = capie::core::game_preferred_base;
    for (const auto& entry : capie::core::editor_vtable_classes) {
        check(capie::core::vtable_is_class(mem, base, entry), std::string("editor class ") + entry.id);
    }
    for (const auto& entry : capie::core::core_vtable_classes) {
        check(capie::core::vtable_is_class(mem, base, entry), std::string("core class ") + entry.id);
    }
    check(capie::core::first_wrong_class(mem, base, capie::core::editor_vtable_classes) == nullptr, "editor table");
    const VtableClass edit = capie::core::editor_vtable_classes[0];
    check(!capie::core::vtable_is_class(mem, base, VtableClass{"x", edit.vtable_rva, ".?AVCInstantTextBox@@", 0}),
          "wrong class name refused");
    check(!capie::core::vtable_is_class(mem, base, VtableClass{"x", edit.vtable_rva, edit.mangled, 0x80}),
          "wrong subobject offset refused");
    check(!capie::core::vtable_is_class(mem, base, VtableClass{"x", edit.vtable_rva + 8, edit.mangled, 0}),
          "vtable moved by one slot refused");
    check(!capie::core::vtable_is_class(mem, base, VtableClass{"x", edit.vtable_rva, ".?AVCEditBox", 0}),
          "name prefix refused");
}

}  // namespace

/**
 * @brief Entry point.
 * @param argc Argument count.
 * @param argv Optional path of the pinned hoi4.exe.
 * @return 0 when every check passed.
 */
int main(int argc, char** argv) {
    table_tests();
    if (argc > 1) {
        exe_tests(argv[1]);
    }
    static_cast<void>(std::printf("rtti exe tests: %d checks, %d failures\n", checks, failures));
    return failures == 0 ? 0 : 1;
}
