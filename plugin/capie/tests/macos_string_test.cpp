/**
 * @file macos_string_test.cpp
 * @brief Synthetic-memory tests of the proven macOS libc++ string layout.
 */
#include "../src/engine/macos_string.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace {
/// @brief Number of checks executed.
unsigned checks = 0;
/// @brief Number of failed checks.
unsigned failures = 0;
/** @brief Records one result. @param ok Result. @param label Failure description. */
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

/// @brief Bounded fake address space; no dereferences of synthetic addresses.
struct Memory {
    /// @brief Base address assigned to the synthetic mapping.
    static constexpr std::uintptr_t base = 0x1000;
    /// @brief Owned bytes containing a header and a separate long buffer.
    std::array<unsigned char, 768> bytes{};
    /// @brief Readable length, shortened to simulate inaccessible data.
    std::size_t mapped = bytes.size();
    /**
     * @brief Implements the process-memory policy against the owned byte array.
     * @param address Synthetic address. @param out Writable output. @param size Byte count.
     * @return True only for a completely mapped range.
     */
    bool read(std::uintptr_t address, void* out, std::size_t size) const {
        if (address < base || address - base > mapped || size > mapped - (address - base)) {
            return false;
        }
        std::memcpy(out, bytes.data() + (address - base), size);
        return true;
    }
    /** @brief Writes a header word. @param offset Byte offset. @param value Word value. */
    void word(std::size_t offset, std::uint64_t value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
};

/**
 * @brief Exercises both element widths with identical validity boundaries.
 * @tparam Char char or char32_t.
 */
template <class Char>
void exercise() {
    namespace strings = capie::engine::macos;
    constexpr std::size_t short_capacity = sizeof(Char) == 1 ? 22 : 4;
    constexpr std::size_t short_offset = sizeof(Char);
    Memory memory;
    std::array<Char, 129> out{};
    std::size_t size = 99;
    const std::basic_string_view<Char> empty;
    check(strings::string_bytes(memory, Memory::base, std::span<Char>(out), size) && size == 0,
          "empty short string");
    check(strings::string_equals(memory, Memory::base, empty), "empty comparison");
    std::array<Char, short_capacity + 1> text{};
    for (std::size_t i = 0; i < short_capacity; ++i) {
        text[i] = static_cast<Char>('A' + i);
    }
    memory.bytes[0] = static_cast<unsigned char>(short_capacity * 2);
    std::memcpy(memory.bytes.data() + short_offset, text.data(), sizeof(text));
    const std::basic_string_view<Char> expected(text.data(), short_capacity);
    check(strings::string_bytes(memory, Memory::base, std::span<Char>(out), size) && size == short_capacity &&
              std::memcmp(out.data(), text.data(), sizeof(text)) == 0, "maximum short string");
    check(strings::name_equals(memory, Memory::base, expected), "strict short comparison");
    check(strings::string_equals(memory, Memory::base, expected), "nonterminator short comparison");
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out.data(), short_capacity), size),
          "output requires terminator space");
    memory.bytes[short_offset + short_capacity * sizeof(Char)] = 1;
    check(!strings::name_equals(memory, Memory::base, expected), "strict rejects missing short terminator");
    check(strings::string_equals(memory, Memory::base, expected), "nonterminator accepts missing short terminator");
    memory.bytes[0] = static_cast<unsigned char>((short_capacity + 1) * 2);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject oversized short length");

    memory = Memory{};
    const std::array<Char, 4> long_text{static_cast<Char>('a'), static_cast<Char>('b'), static_cast<Char>('c'), 0};
    memory.word(0, 33); // allocated count 32, usable capacity 31
    memory.word(8, 3);
    memory.word(16, Memory::base + 64);
    std::memcpy(memory.bytes.data() + 64, long_text.data(), sizeof(long_text));
    const std::basic_string_view<Char> long_expected(long_text.data(), 3);
    check(strings::string_bytes(memory, Memory::base, std::span<Char>(out), size) && size == 3,
          "read long string");
    check(strings::name_equals(memory, Memory::base, long_expected), "strict long comparison");
    check(strings::string_equals(memory, Memory::base, long_expected), "nonterminator long comparison");
    memory.mapped = 64 + 3 * sizeof(Char);
    check(!strings::name_equals(memory, Memory::base, long_expected), "strict requires readable long terminator");
    check(strings::string_equals(memory, Memory::base, long_expected), "nonterminator does not read past content");
    memory.mapped = memory.bytes.size();
    memory.bytes[64 + 3 * sizeof(Char)] = 1;
    check(!strings::name_equals(memory, Memory::base, long_expected), "strict rejects wrong long terminator");
    memory.bytes[64 + 3 * sizeof(Char)] = 0;
    std::memset(memory.bytes.data() + 64 + sizeof(Char), 0, sizeof(Char));
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "strict rejects embedded NUL");
    const std::array<Char, 3> with_nul{static_cast<Char>('a'), 0, static_cast<Char>('c')};
    check(strings::string_equals(memory, Memory::base, std::basic_string_view<Char>(with_nul.data(), 3)),
          "nonterminator compares embedded NUL literally");
    std::memcpy(memory.bytes.data() + 64, long_text.data(), sizeof(long_text));
    memory.word(0, 5); // allocation 4 means usable capacity 3
    check(strings::name_equals(memory, Memory::base, long_expected), "long size at capacity boundary");
    memory.word(8, 4);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "allocation includes terminator");
    memory.word(8, 3);
    memory.word(0, 1);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject zero allocated count");
    memory.word(0, strings::max_string_capacity + 3);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject excessive capacity");
    memory.word(0, strings::max_string_capacity | 1);
    check(strings::name_equals(memory, Memory::base, long_expected), "largest even allocation within bound");
    memory.word(0, 33);
    memory.word(8, std::numeric_limits<std::uint64_t>::max());
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject overflowing element count");
    memory.word(8, 3);
    memory.word(16, std::numeric_limits<std::uintptr_t>::max() - 1);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject data range overflow");
    memory.word(16, 0);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject null long pointer");
    check(!strings::string_bytes(memory, 0, std::span<Char>(out), size), "reject null header");
    check(!strings::string_bytes(memory, std::numeric_limits<std::uintptr_t>::max() - 3,
                                std::span<Char>(out), size), "reject header range overflow");
    memory.mapped = 23;
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size), "reject truncated header");
    check(!strings::string_equals(memory, Memory::base, std::basic_string_view<Char>(out.data(), 129)),
          "reject oversized expected name");
    memory = Memory{};
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(), size), "reject empty destination");
    memory.word(0, 131); // allocation 130, capacity 129
    memory.word(8, 128);
    memory.word(16, Memory::base + 64);
    std::array<Char, 130> maximum_name{};
    for (std::size_t i = 0; i < 128; ++i) {
        maximum_name[i] = static_cast<Char>('x');
    }
    std::memcpy(memory.bytes.data() + 64, maximum_name.data(), sizeof(maximum_name));
    const std::basic_string_view<Char> maximum_expected(maximum_name.data(), 128);
    check(strings::name_equals(memory, Memory::base, maximum_expected), "strict maximum name length");
    check(strings::string_equals(memory, Memory::base, maximum_expected), "nonterminator maximum name length");
    check(strings::string_bytes(memory, Memory::base, std::span<Char>(out), size) && size == 128,
          "long content larger than short storage");
    memory.word(8, 129);
    check(!strings::string_bytes(memory, Memory::base, std::span<Char>(out), size) && size == 128,
          "long output bound fails without modifying reported size");
    memory.word(8, 128);
    memory.mapped = 64 + sizeof(Char) * 127;
    check(!strings::string_equals(memory, Memory::base, maximum_expected), "reject truncated long content");
}
}  // namespace

/** @brief Executes both layout suites. @return Zero on success. */
int main() {
    exercise<char>();
    exercise<char32_t>();
    std::printf("macOS strings: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
