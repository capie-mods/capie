/**
 * @file memory_test.cpp
 * @brief Tests of the bounded readers in core/memory.hpp (closes review NOTE-S2-1).
 *
 * `readable()` must refuse guard pages, no-access pages, reserved (uncommitted) memory,
 * null, address overflow and ranges that run from a readable page into an unreadable
 * one; `guarded_copy()` and `member()` must then fail without touching the memory. For a
 * guard page the test also checks that the guard is still armed afterwards, i.e. nothing
 * was read (a read would consume the guard or raise STATUS_GUARD_PAGE_VIOLATION, which
 * the SEH filter deliberately does not handle). Owned pages only; no game process.
 */
#include "../src/core/memory.hpp"

#include <cstdint>
#include <cstdio>

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

/// @brief Byte written to the readable page and read back.
constexpr unsigned char marker = 0x5A;

/// @brief One page.
constexpr std::size_t page = 4096;

/**
 * @brief Whether a page still carries PAGE_GUARD.
 * @param address Page address.
 * @return `true` when the guard is armed.
 */
bool guard_armed(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(address, &info, sizeof(info)) != 0 && (info.Protect & PAGE_GUARD) != 0;
}

}  // namespace

/**
 * @brief Entry point.
 * @return 0 when every check passed.
 */
int main() {
    using namespace capie::core;  // NOLINT(google-build-using-namespace): test brevity
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4 * page, MEM_RESERVE, PAGE_NOACCESS));
    check(pages != nullptr, "reserve four pages");
    if (pages == nullptr) {
        static_cast<void>(std::printf("memory tests: %d checks, %d failures\n", checks, failures));
        return 1;
    }
    // page 0: read/write; page 1: read/write + guard; page 2: no access; page 3: reserved only.
    check(VirtualAlloc(pages, 3 * page, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit three pages");
    pages[0] = marker;
    DWORD old = 0;
    check(VirtualProtect(pages + page, page, PAGE_READWRITE | PAGE_GUARD, &old) != 0, "guard page 1");
    check(VirtualProtect(pages + (2 * page), page, PAGE_NOACCESS, &old) != 0, "no-access page 2");

    unsigned char byte = 0;
    std::uint64_t word = 0;
    check(readable(pages, 1) && guarded_copy(&byte, pages, 1) && byte == marker, "read/write page is readable");
    check(readable(pages, 0) && readable(nullptr, 0), "zero-length range is readable");
    check(!readable(pages + page, 1), "guard page is refused");
    check(!guarded_copy(&byte, pages + page, 1), "guarded_copy refuses the guard page");
    check(!member(pages + page, 0, word), "member refuses the guard page");
    check(!readable(pages + page - 4, 8), "range into the guard page is refused");
    check(guard_armed(pages + page), "guard still armed: nothing was read");
    check(!readable(pages + (2 * page), 1) && !guarded_copy(&byte, pages + (2 * page), 1), "no-access page is refused");
    check(!readable(pages + (3 * page), 1), "reserved, uncommitted page is refused");
    check(!readable(nullptr, 1) && !member(nullptr, 0, word), "null is refused");
    check(!readable(reinterpret_cast<const void*>(uptr_max - 3), 8), "address overflow is refused");
    check(!member(pages, uptr_max, word), "offset overflow is refused");
    check(VirtualFree(pages, 0, MEM_RELEASE) != 0, "release");
    static_cast<void>(std::printf("memory tests: %d checks, %d failures\n", checks, failures));
    return failures != 0 ? 1 : 0;
}
