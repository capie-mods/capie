/** @file macos_owned_string_test.cpp
 * @brief Construction/ownership tests for the macOS faction-name return adapter.
 */
#include "../src/engine/macos_owned_string.hpp"
#include "../src/engine/macos_string.hpp"
#include "../src/core/macos_memory.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
int checks = 0;
int failures = 0;
std::size_t requested = 0;
void check(bool value) { ++checks; if (!value) ++failures; }
void* allocate(std::size_t bytes) noexcept { requested = bytes; return std::malloc(bytes); }
void* deny(std::size_t) noexcept { return nullptr; }
}
int main() {
    using namespace capie::engine::macos;
    capie::core::macos::ProcessMemory mem;
    for (std::size_t length : {0U, 1U, 22U, 23U, 31U, 32U, 127U, 128U}) {
        std::string text(length, 'A');
        NativeString out;
        requested = 0;
        check(construct_name(text, out, allocate));
        check(name_equals<char>(mem, reinterpret_cast<std::uintptr_t>(&out), text));
        check((out.bytes[0] & 1) == (length > 22));
        if (length > 22) {
            std::uint64_t tagged = 0;
            void* pointer = nullptr;
            std::memcpy(&tagged, out.bytes.data(), 8);
            std::memcpy(&pointer, out.bytes.data() + 16, 8);
            check(requested == (length | 15U) + 1);
            check(tagged == requested + 1);
            std::free(pointer); // Ownership transferred to the simulated game caller.
        } else {
            check(requested == 0);
        }
    }
    NativeString sentinel;
    sentinel.bytes.fill(0xab);
    const auto original = sentinel.bytes;
    check(!construct_name(std::string(23, 'A'), sentinel, deny));
    check(sentinel.bytes == original);
    check(!construct_name(std::string(129, 'A'), sentinel, allocate));
    check(sentinel.bytes == original);
    check(!construct_name(std::string_view("a\0b", 3), sentinel, allocate));
    check(sentinel.bytes == original);
    check(!construct_name(std::string(23, 'A'), sentinel, nullptr));
    check(sentinel.bytes == original);
    check(construct_name("short", sentinel, nullptr));
    std::printf("macOS owned strings: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
