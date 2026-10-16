/** @file macos_portrait_layer_test.cpp @brief Native portrait read protocol and leader chain, synthetic memory only. */
#include "../src/features/country/portrait_layer.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
namespace portrait = capie::country::portrait;
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
struct Memory {
    static constexpr std::uintptr_t base = 0x1000, country = base, politics = 0x3000, party = 0x4000, roles = 0x5000,
                                    role = 0x6000, character = 0x7000, entries = 0x9000, image = 0x100000000;
    std::array<unsigned char, 0xc000> data{};
    std::uintptr_t denied = 0;
    unsigned count = 0;
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        if (address < base || address - base > data.size() || size > data.size() - (address - base) ||
            (denied && address <= denied && denied - address < size))
            return false;
        std::memcpy(out, data.data() + address - base, size);
        return true;
    }
    template <class T>
    void put(std::uintptr_t address, T value) {
        std::memcpy(data.data() + address - base, &value, sizeof(value));
    }
    Memory() {
        put(country + 0xd38, politics);
        put(politics + 0xd0, party);
        put(party + 0x60, roles);
        put<std::int32_t>(party + 0x6c, 1);
        put(roles, role);
        put(role + 8, character);
        put(character, image + 0x332efe0);
        put(character + 0xd0 + 0x10, entries);
        put<std::uint32_t>(character + 0xd0 + 0x1c, 0);
        put<std::uint8_t>(character + 0xd0 + 0x20, 255);
    }
    std::uintptr_t add(std::string_view name, std::int64_t value) {
        auto at = entries + count * 0x28;
        put<std::uint8_t>(at + 4, static_cast<std::uint8_t>(++count));
        put<std::uint8_t>(at + 8, static_cast<std::uint8_t>(name.size() * 2));
        std::memcpy(data.data() + at + 9 - base, name.data(), name.size());
        put(at + 0x20, value);
        return at;
    }
};
}  // namespace
int main() {
    using Status = capie::country::layer::Status;
    Memory mem;
    std::uintptr_t found = 123;
    check(portrait::ruling_leader(mem, Memory::image, Memory::country, found) && found == Memory::character,
          "native ruling chain");
    check(portrait::is_character(mem, Memory::image, Memory::character), "native RTTI");
    check(!portrait::is_character(mem, std::numeric_limits<std::uintptr_t>::max() - 8, Memory::character),
          "image overflow rejected");
    check(portrait::read_portrait_layer(mem, Memory::character).status == Status::absent, "embedded empty variables");
    auto version = mem.add("capie_pt_ver", 100000);
    auto length = mem.add("capie_pt_len", 100000);
    mem.add("capie_pt_s^num", 100000);
    auto packed = mem.add("capie_pt_s^0", 409600000);
    auto result = portrait::read_portrait_layer(mem, Memory::character);
    check(result.status == Status::valid && result.large.view() == "a" && result.small.view().empty(),
          "large only embedded variables");
    auto small_length = mem.add("capie_pt_slen", 100000);
    mem.add("capie_pt_ss^num", 100000);
    mem.add("capie_pt_ss^0", 819200000);
    result = portrait::read_portrait_layer(mem, Memory::character);
    check(result.status == Status::valid && result.large.view() == "a" && result.small.view() == "b",
          "both portrait sizes");
    mem.put<std::int64_t>(small_length + 0x20, 0);
    check(portrait::read_portrait_layer(mem, Memory::character).small.view().empty(), "zero small uses game portrait");
    mem.put<std::int64_t>(version + 0x20, 200000);
    check(portrait::read_portrait_layer(mem, Memory::character).status == Status::invalid, "unknown version");
    mem.put<std::int64_t>(version + 0x20, 100000);
    mem.put<std::int64_t>(length + 0x20, 6400000);
    result = portrait::read_portrait_layer(mem, Memory::character);
    check(result.status == Status::invalid && result.large.view().empty() && result.small.view().empty(),
          "invalid length clears output");
    mem.put<std::int64_t>(length + 0x20, 100000);
    mem.denied = packed + 0x20;
    result = portrait::read_portrait_layer(mem, Memory::character);
    check(result.status == Status::unreadable && result.large.view().empty(), "unreadable entry clears output");
    mem.denied = 0;
    mem.put<std::uintptr_t>(Memory::politics + 0xd0, 0);
    check(portrait::ruling_leader(mem, Memory::image, Memory::country, found) && found == 0,
          "no party means no leader");
    mem.put(Memory::politics + 0xd0, Memory::party);
    mem.put<std::int32_t>(Memory::party + 0x6c, 0);
    check(portrait::ruling_leader(mem, Memory::image, Memory::country, found) && found == 0,
          "no roles means no leader");
    mem.put<std::int32_t>(Memory::party + 0x6c, -1);
    check(!portrait::ruling_leader(mem, Memory::image, Memory::country, found) && found == 0,
          "negative role count refused");
    mem.put<std::int32_t>(Memory::party + 0x6c, 1);
    mem.put<std::uintptr_t>(Memory::character, Memory::image + 0x1234);
    check(!portrait::ruling_leader(mem, Memory::image, Memory::country, found) && found == 0, "foreign vtable refused");
    check(portrait::read_portrait_layer(mem, 0).status == Status::unreadable, "null character refused");
    check(
        portrait::read_portrait_layer(mem, std::numeric_limits<std::uintptr_t>::max() - 1).status == Status::unreadable,
        "embedded offset overflow");
    Memory missing_large;
    missing_large.add("capie_pt_ver", 100000);
    check(portrait::read_portrait_layer(missing_large, Memory::character).status == Status::invalid,
          "set layer requires large sprite");
    mem.put<std::int64_t>(version + 0x20, 100001);
    check(portrait::read_portrait_layer(mem, Memory::character).status == Status::invalid,
          "fractional version invalid");
    mem.put<std::int64_t>(version + 0x20, 100000);
    mem.put<std::int64_t>(small_length + 0x20, -100000);
    result = portrait::read_portrait_layer(mem, Memory::character);
    check(result.status == Status::invalid && result.large.view().empty() && result.small.view().empty(),
          "bad small sprite clears both outputs");
    Memory broken;
    broken.denied = Memory::role + 8;
    check(!portrait::ruling_leader(broken, Memory::image, Memory::country, found) && found == 0,
          "unreadable role character");
    broken.denied = 0;
    broken.put<std::uintptr_t>(Memory::party + 0x60, 0);
    check(!portrait::ruling_leader(broken, Memory::image, Memory::country, found) && found == 0,
          "null roles with positive count");
    broken.put<std::uintptr_t>(Memory::country + 0xd38, 0);
    check(!portrait::ruling_leader(broken, Memory::image, Memory::country, found) && found == 0,
          "null politics unreadable");
    check(!portrait::ruling_leader(broken, Memory::image, 0, found) && found == 0, "null country unreadable");
    check(!portrait::is_character(broken, 0, Memory::character), "zero image rejected");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
