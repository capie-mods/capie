/** @file macos_variables_test.cpp @brief Synthetic-memory variable lookup checks; no game access. */
#include "../src/engine/macos_variables.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
namespace vars = capie::engine::macos::variables;
namespace {
unsigned checks = 0;
unsigned failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
struct Memory {
    static constexpr std::uintptr_t base = 0x1000;
    std::array<unsigned char, 16384> bytes{};
    std::size_t mapped = bytes.size();
    mutable unsigned reads = 0;
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        ++reads;
        if (address < base || address - base > mapped || size > mapped - (address - base))
            return false;
        std::memcpy(out, bytes.data() + address - base, size);
        return true;
    }
    template <class T>
    void put(std::size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    void table(std::uint32_t mask = 3, unsigned char extra = 3) {
        put<std::uintptr_t>(0x10, base + 0x100);
        put(0x1c, mask);
        put(0x20, extra);
    }
    void entry(std::size_t slot, std::string_view key, std::int64_t value, unsigned char probe = 1) {
        const auto at = 0x100 + slot * 0x28;
        bytes[at + 4] = probe;
        if (key.size() <= 22) {
            bytes[at + 8] = static_cast<unsigned char>(key.size() * 2);
            std::memcpy(bytes.data() + at + 9, key.data(), key.size());
        } else {
            put<std::uint64_t>(at + 8, 65);
            put<std::uint64_t>(at + 16, key.size());
            put<std::uintptr_t>(at + 24, base + 0x3000);
            std::memcpy(bytes.data() + 0x3000, key.data(), key.size());
        }
        put(at + 32, value);
    }
};
}  // namespace
int main() {
    Memory mem;
    mem.table();
    vars::Table table;
    check(vars::read_table(mem, Memory::base, table), "table header");
    std::int64_t value = 777;
    const auto bucket = vars::fnv1a("capie_cn_ver") & 3;
    check(vars::find_variable(mem, table, "capie_cn_ver", value) == vars::Lookup::absent && value == 777,
          "empty unchanged");
    mem.entry(bucket, "capie_cn_ver", -100000);
    check(vars::find_variable(mem, table, "capie_cn_ver", value) == vars::Lookup::found && value == -100000,
          "negative SSO");
    mem.entry(bucket, "different", 1);
    mem.entry(bucket + 1, "capie_cn_ver", 42, 2);
    check(vars::find_variable(mem, table, "capie_cn_ver", value) == vars::Lookup::found && value == 42,
          "collision exact key");
    mem.bytes[0x100 + (bucket + 1) * 0x28 + 4] = 1;
    value = 777;
    check(vars::find_variable(mem, table, "capie_cn_ver", value) == vars::Lookup::absent && value == 777,
          "robin hood stop");
    const std::string long_key(32, 'x');
    mem = Memory{};
    mem.table();
    vars::read_table(mem, Memory::base, table);
    mem.entry(vars::fnv1a(long_key) & 3, long_key, -9223372036854775807LL);
    check(vars::find_variable(mem, table, long_key, value) == vars::Lookup::found && value == -9223372036854775807LL,
          "heap key max length");
    mem.mapped = 0x3000 + long_key.size();
    check(vars::find_variable(mem, table, long_key, value) == vars::Lookup::found,
          "heap comparison needs no readable terminator");
    value = 777;
    mem.mapped = 0x3000;
    check(vars::find_variable(mem, table, long_key, value) == vars::Lookup::unreadable && value == 777,
          "unreadable heap unchanged");
    mem.mapped = 0x100;
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::unreadable, "unreadable entry");
    mem.mapped = 0x20;
    check(!vars::read_table(mem, Memory::base, table), "unreadable header");
    check(!vars::read_table(mem, std::numeric_limits<std::uintptr_t>::max() - 8, table), "header overflow");
    mem = Memory{};
    mem.table(2);
    check(!vars::read_table(mem, Memory::base, table), "non mask rejected");
    mem.table(0xffffffff);
    check(!vars::read_table(mem, Memory::base, table), "negative mask rejected");
    mem.table(0x1ffffff);
    check(!vars::read_table(mem, Memory::base, table), "huge mask rejected");
    table = {std::numeric_limits<std::uintptr_t>::max() - 10, 3, 7};
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::unreadable, "entry address overflow");
    table = {0, 3, 7};
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::absent, "null table empty");
    mem = Memory{};
    mem.table(0, 0);
    vars::read_table(mem, Memory::base, table);
    mem.entry(0, "other", 1);
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::unreadable, "extra slots exhausted no wrap");
    mem.table(0, 255);
    vars::read_table(mem, Memory::base, table);
    for (unsigned i = 0; i < 256; ++i)
        mem.entry(i, "other", 1, 255);
    const auto reads = mem.reads;
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::unreadable && mem.reads - reads <= 255,
          "probe bound");
    check(vars::find_variable(mem, table, std::string(33, 'x'), value) == vars::Lookup::absent, "oversize name absent");
    mem = Memory{};
    mem.table(0, 0);
    vars::read_table(mem, Memory::base, table);
    mem.entry(0, "a", 9);
    mem.bytes[0x108] = 46;
    check(vars::find_variable(mem, table, "a", value) == vars::Lookup::unreadable, "invalid SSO length");
    mem.entry(0, long_key, 9);
    mem.put<std::uint64_t>(0x108, 1);
    check(vars::find_variable(mem, table, long_key, value) == vars::Lookup::unreadable, "invalid heap allocation");
    mem.entry(0, long_key, 9);
    mem.put<std::uintptr_t>(0x118, std::numeric_limits<std::uintptr_t>::max() - 8);
    check(vars::find_variable(mem, table, long_key, value) == vars::Lookup::unreadable, "heap range overflow");
    mem = Memory{};
    mem.table(0, 0);
    vars::read_table(mem, Memory::base, table);
    const std::string nul_key("a\0b", 3);
    mem.entry(0, nul_key, 123);
    check(vars::find_variable(mem, table, nul_key, value) == vars::Lookup::found && value == 123,
          "exact byte key permits NUL like memcmp");
    mem.entry(0, "", 321);
    check(vars::find_variable(mem, table, "", value) == vars::Lookup::found && value == 321,
          "empty key distinct from empty entry");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
