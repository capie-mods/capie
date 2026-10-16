/** @file macos_activation_checks_test.cpp @brief Owned sparse-memory activation checks. */
#include "../src/core/macos_activation_checks.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
namespace checks = capie::core::macos::activation;
struct Memory {
    struct Region {
        std::uintptr_t address;
        std::vector<unsigned char> bytes;
    };
    std::vector<Region> regions;
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        for (const auto& region : regions) {
            if (address == region.address && size == region.bytes.size()) {
                std::memcpy(out, region.bytes.data(), size);
                return true;
            }
        }
        return false;
    }
};
int main() {
    unsigned total = 0, failures = 0;
    auto check = [&](bool value) {
        ++total;
        if (!value)
            ++failures;
    };
    constexpr std::uintptr_t image = 0x200000000;
    Memory memory{{{image + 0x2a7fbd0, {0x55, 0x48, 0x89, 0xe5, 0x5d, 0xe9, 0xc6, 0x67, 0x0a, 0}},
                   {image + 0x2a7fc10, {0x55, 0x48, 0x89, 0xe5, 0x5d, 0xe9, 0x5e, 0x65, 0x0a, 0}},
                   {image + 0x2b263a0, {0xff, 0x25, 0x42, 0xc3, 0x77, 0}},
                   {image + 0x2b26178, {0xff, 0x25, 0x8a, 0xc2, 0x77, 0}},
                   {image + 0x35af598, {0xb2, 0x3a, 0, 0}},
                   {image + 0x35ae4b8, {0x70, 0x3a, 0, 0}}}};
    check(checks::allocator_matches(memory, image));
    check(checks::tokens_ready(memory, image));
    for (std::size_t r = 0; r < 4; ++r) {
        for (auto& byte : memory.regions[r].bytes) {
            byte ^= 1;
            check(!checks::allocator_matches(memory, image));
            byte ^= 1;
        }
    }
    for (std::size_t r = 4; r < 6; ++r) {
        auto saved = memory.regions[r].bytes;
        memory.regions[r].bytes.assign(4, 0);
        check(!checks::tokens_ready(memory, image));
        memory.regions[r].bytes = saved;
        memory.regions[r].bytes[0] ^= 1;
        check(!checks::tokens_ready(memory, image));
        memory.regions[r].bytes = saved;
    }
    check(!checks::allocator_matches(Memory{}, image));
    check(!checks::tokens_ready(Memory{}, image));
    check(!checks::allocator_matches(memory, 0));
    check(!checks::tokens_ready(memory, 0));
    check(!checks::allocator_matches(memory, std::numeric_limits<std::uintptr_t>::max()));
    check(!checks::tokens_ready(memory, std::numeric_limits<std::uintptr_t>::max()));
    std::printf("activation checks: %u checks, %u failures\n", total, failures);
    return failures != 0;
}
