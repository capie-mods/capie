/**
 * @file macos_memory_test.cpp
 * @brief Tests real macOS memory reads against owned mappings; never accesses the game.
 */
#include "../src/core/memory.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
/// @brief Number of assertions executed.
unsigned checks = 0;
/// @brief Number of failed assertions.
unsigned failures = 0;

/**
 * @brief Records a test result.
 * @param ok Whether the assertion passed.
 * @param label Description printed on failure.
 */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
}  // namespace

/**
 * @brief Exercises the process-memory policy with real readable and inaccessible mappings.
 * @return Zero if every check passed.
 */
int main() {
    using capie::core::ProcessMemory;
    const long page_size = sysconf(_SC_PAGESIZE);
    check(page_size > 0, "obtain page size");
    if (page_size <= 0) {
        return 1;
    }
    const auto page = static_cast<std::size_t>(page_size);
    auto* pages = static_cast<unsigned char*>(
        mmap(nullptr, 3 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    check(pages != MAP_FAILED, "map three pages");
    if (pages == MAP_FAILED) {
        return 1;
    }
    std::memset(pages, 0x5a, 3 * page);
    const auto base = reinterpret_cast<std::uintptr_t>(pages);
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    std::array<unsigned char, 8> result{};
    const ProcessMemory memory{};
    std::uint32_t member = 0;
    check(capie::core::member(pages, 4, member) && member == 0x5a5a5a5a,
          "shared member helper uses macOS backend");
    check(!capie::core::member(nullptr, 4, member), "member rejects null object");
    check(capie::core::rd(memory, base, 8, member) && member == 0x5a5a5a5a,
          "shared policy member read");
    check(!capie::core::rd(memory, maximum - 1, 4, member), "member offset overflow rejected");
    check(memory.read(base, result.data(), result.size()) && result[0] == 0x5a && result[7] == 0x5a,
          "copy readable bytes");
    check(memory.read(base + page - 4, result.data(), result.size()), "read across two readable pages");
    check(capie::core::readable(nullptr, 0), "zero readability succeeds");
    check(!capie::core::readable(nullptr, 1), "null readability fails");
    check(!capie::core::readable(reinterpret_cast<void*>(maximum - 3), 8), "readability rejects overflow");
    check(capie::core::readable(pages, 3 * page), "readability spans multiple chunks");
    check(mprotect(pages, page, PROT_READ) == 0, "protect first page read-only");
    check(memory.read(base, result.data(), result.size()), "copy read-only source");
    check(!memory.read(base + page, pages, 1), "refuse read-only destination without fault");
    check(mprotect(pages + 2 * page, page, PROT_NONE) == 0, "protect third page no-access");
    check(!memory.read(base + 2 * page, result.data(), 1), "refuse no-access source");
    check(!capie::core::readable(pages + 2 * page - 4, 8), "readability rejects crossing no-access page");
    check(capie::core::readable(pages, 2 * page), "readability ends exactly at no-access page");
    check(!memory.read(base + 2 * page - 4, result.data(), result.size()), "refuse range into no-access page");
    check(!memory.read(base, pages + 2 * page, 1), "refuse no-access destination");
    check(!memory.read(0, result.data(), 1), "refuse null source");
    check(!memory.read(base, nullptr, 1), "refuse null destination");
    check(memory.read(0, nullptr, 0), "zero bytes succeeds with null pointers");
    check(memory.read(maximum, nullptr, 0), "zero bytes never dereferences source");
    check(!memory.read(maximum - 3, result.data(), 8), "refuse source address overflow");
    check(!memory.read(base, reinterpret_cast<void*>(maximum - 3), 8), "refuse destination address overflow");
    check(!memory.read(base, result.data(), maximum), "refuse oversized range");
    check(munmap(pages + 2 * page, page) == 0, "unmap third page");
    check(!memory.read(base + 2 * page, result.data(), 1), "refuse unmapped source");
    check(!memory.read(base + 2 * page - 4, result.data(), result.size()), "refuse range into unmapped page");
    check(memory.read(base + 2 * page - result.size(), result.data(), result.size()),
          "range ending exactly at unmapped page succeeds");
    check(!memory.read(base, reinterpret_cast<void*>(base + 2 * page), 1), "refuse unmapped destination");
    check(munmap(pages, 2 * page) == 0, "release remaining pages");
    check(!memory.read(base, result.data(), 1), "refuse released source");
    std::printf("macOS memory: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
