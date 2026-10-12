/**
 * @file signature_exe_test.cpp
 * @brief Checks every entry of the CAPIE signature table against the pinned `hoi4.exe` FILE (read only).
 *
 * Uses the production matcher (`core::unique_at`) on the file's `.text` bytes and the
 * production pin checks (`verify_slot_pin`, `verify_token_pin`, `verify_rip_pin`) through a
 * memory policy that maps preferred-base addresses to file offsets. Without an argument
 * only the table itself is checked (decodable, counts). No process is touched.
 *
 * Expected: 3 country + 11 editor + 25 caption + 8 focus = 47 signatures unique at their
 * RVA; 4 caption + 2 focus slot pins; 2 tokens; 1 RIP pin.
 */
#include "../src/core/game_build.hpp"
#include "../src/core/signature_table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

/**
 * @brief Records one check.
 * @param ok Result.
 * @param label Description printed on failure.
 */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        static_cast<void>(std::printf("FAIL %s\n", label));
    }
}

/// @brief One PE section of the file.
struct Section {
    char name[9]{};            ///< NUL-terminated name.
    std::uint32_t va{};        ///< RVA.
    std::uint32_t vsize{};     ///< Virtual size.
    std::uint32_t raw{};       ///< File offset.
    std::uint32_t raw_size{};  ///< Raw size.
};

/// @brief Memory policy over the file image at the preferred base.
struct FileMemory {
    const std::vector<unsigned char>* data{};  ///< File bytes.
    const std::vector<Section>* sections{};    ///< Section table.
    /**
     * @brief Reads `size` bytes at a preferred-base address.
     * @param address Address (preferred base + RVA).
     * @param out Destination.
     * @param size Byte count.
     * @return `false` outside raw section data.
     */
    bool read(capie::core::uptr address, void* out, std::size_t size) const {
        if (address < capie::core::game_preferred_base) {
            return false;
        }
        const auto rva = address - capie::core::game_preferred_base;
        const auto found = std::ranges::find_if(*sections, [rva, size](const Section& s) {
            return rva >= s.va && rva + size <= static_cast<capie::core::uptr>(s.va) + s.raw_size;
        });
        if (found == sections->end()) {
            return false;
        }
        std::memcpy(out, data->data() + found->raw + (rva - found->va), size);
        return true;
    }
};

/**
 * @brief Checks one table against `.text`.
 * @tparam N Table size.
 * @param text `.text` view of the file.
 * @param table Signatures.
 * @param label Group label.
 * @return Number of verified entries.
 */
template <std::size_t N>
int check_table(const capie::core::TextSection* text, const capie::core::Signature (&table)[N], const char* label) {
    int ok_count = 0;
    for (const auto& signature : table) {
        capie::core::Pattern pattern{};
        const bool decoded = capie::core::decode_pattern(signature.hex, pattern);
        check(decoded, signature.id);
        if (text == nullptr || !decoded) {
            continue;
        }
        const bool ok = capie::core::unique_at(*text, signature.rva, pattern);
        check(ok, signature.id);
        ok_count += ok ? 1 : 0;
        static_cast<void>(std::printf("  %-8s %-22s %07llX %3zuB %s\n", label, signature.id,
                                      static_cast<unsigned long long>(signature.rva), pattern.size,
                                      ok ? "unique-at-rva" : "BAD"));
    }
    return ok_count;
}

}  // namespace

/**
 * @brief Entry point.
 * @param argc Argument count.
 * @param argv Optional path to hoi4.exe.
 * @return 0 when every check passed.
 */
int main(int argc, char** argv) {
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    using namespace capie::core;  // NOLINT(google-build-using-namespace): test brevity
    check(country_signature_count == 3 && editor_signature_count == 11 && caption_signature_count == 25 &&
              focus_signature_count == 8,
          "table sizes 3/11/25/8");
    std::vector<unsigned char> data;
    std::vector<Section> sections;
    TextSection text{};
    const TextSection* text_view = nullptr;
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        if (file) {
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            auto u32 = [&](std::size_t o) {
                std::uint32_t v = 0;
                std::memcpy(&v, data.data() + o, 4);
                return v;
            };
            auto u16 = [&](std::size_t o) {
                std::uint16_t v = 0;
                std::memcpy(&v, data.data() + o, 2);
                return v;
            };
            const auto e = u32(0x3C);
            const auto count = u16(e + 6);
            const auto optional = u16(e + 20);
            for (unsigned i = 0; i < count; ++i) {
                const auto o = e + 24 + optional + (i * 40);
                Section s{};
                std::memcpy(s.name, data.data() + o, 8);
                s.vsize = u32(o + 8);
                s.va = u32(o + 12);
                s.raw_size = u32(o + 16);
                s.raw = u32(o + 20);
                sections.push_back(s);
            }
            for (const auto& s : sections) {
                if (std::strcmp(s.name, ".text") == 0) {
                    text = TextSection{data.data() + s.raw - s.va, s.va, s.vsize < s.raw_size ? s.vsize : s.raw_size};
                    text_view = &text;
                }
            }
            check(text_view != nullptr, ".text present");
        } else {
            static_cast<void>(std::printf("EXE not found: table-only checks\n"));
        }
    }
    int verified = 0;
    verified += check_table(text_view, country_signatures, "country");
    verified += check_table(text_view, editor_signatures, "editor");
    verified += check_table(text_view, caption_signatures, "caption");
    verified += check_table(text_view, focus_signatures, "focus");
    int pins = 0;
    if (text_view != nullptr) {
        const FileMemory memory{&data, &sections};
        for (const auto& pin : caption_slot_pins) {
            const bool ok = verify_slot_pin(memory, game_preferred_base, pin);
            check(ok, "caption slot pin");
            pins += ok ? 1 : 0;
        }
        for (const auto& pin : focus_slot_pins) {
            const bool ok = verify_slot_pin(memory, game_preferred_base, pin);
            check(ok, "focus slot pin");
            pins += ok ? 1 : 0;
        }
        for (const auto& token : caption_token_pins) {
            const bool ok = verify_token_pin(memory, game_preferred_base, token);
            check(ok, token.id);
            pins += ok ? 1 : 0;
        }
        const bool rip = verify_rip_pin(memory, game_preferred_base, focus_manager_rip_pin);
        check(rip, focus_manager_rip_pin.id);
        pins += rip ? 1 : 0;
        static_cast<void>(std::printf(
            "signature EXE check: %d/47 signatures unique at RVA (3 country, 11 editor, 25 caption, 8 focus); "
            "%d/9 pins (6 slot, 2 token, 1 RIP)\n",
            verified, pins));
    }
    static_cast<void>(std::printf("signature tests: %d checks, %d failures\n", checks, failures));
    return failures != 0 ? 1 : 0;
}
