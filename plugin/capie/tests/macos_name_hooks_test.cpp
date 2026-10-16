/** @file macos_name_layer_test.cpp
 * @brief Shared country/faction protocol against synthetic macOS variable storage; no game calls.
 */
#include "../src/features/country/name_layer_hooks.hpp"
#include <cstdlib>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {
namespace layer = capie::country::layer;
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
/// @brief One-bucket robin-hood table: all keys have sequential distances from bucket zero.
struct Memory {
    static constexpr std::uintptr_t country = 0x1000, container = 0x2000, entries = 0x3000;
    std::array<unsigned char, 0x8000> data{};
    std::uintptr_t denied = 0;
    unsigned count = 0;
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        if (address == 0x103519740 && size == 8) {
            const std::uintptr_t state = 0x5000;
            std::memcpy(out, &state, size);
            return true;
        }
        if (address < country || address - country > data.size() ||
            size > data.size() - (address - country) ||
            (denied && address <= denied && denied - address < size)) return false;
        std::memcpy(out, data.data() + address - country, size);
        return true;
    }
    template<class T> void put(std::uintptr_t address, T value) {
        std::memcpy(data.data() + address - country, &value, sizeof(value));
    }
    Memory() {
        put(country + 0x1d8, container);
        put(container + 0x10, entries);
        put<std::int32_t>(container + 0x1c, 0);
        put<std::uint8_t>(container + 0x20, 255);
    }
    std::uintptr_t add(std::string_view key, std::int64_t raw) {
        const auto address = entries + count * 0x28;
        put<std::uint8_t>(address + 4, static_cast<std::uint8_t>(++count));
        put<std::uint8_t>(address + 8, static_cast<std::uint8_t>(key.size() * 2));
        std::memcpy(data.data() + address + 9 - country, key.data(), key.size());
        put(address + 0x20, raw);
        return address;
    }
};
}

namespace {
using Native = capie::engine::macos::NativeString;
using Adapter = layer::NameLayer<Memory>;
Adapter* active_layer = nullptr;
unsigned updates = 0, builds = 0, sets = 0, factions = 0, allocated = 0, released = 0;
bool allocation_failure = false;
std::string observed;
void* allocate(std::size_t size) noexcept {
    if (allocation_failure) return nullptr;
    ++allocated;
    return std::malloc(size);
}
void release(void* pointer) noexcept { ++released; std::free(pointer); }
void free_native(Native& value) {
    if (value.bytes[0] & 1) {
        void* pointer = nullptr;
        std::memcpy(&pointer, value.bytes.data() + 16, 8);
        release(pointer);
    }
}
void* build(void* out, const void* tag, void* a3, void* a4, void* a5, void* a6, void* a7, char a8) {
    ++builds;
    check(a3 == reinterpret_cast<void*>(3) && a4 == reinterpret_cast<void*>(4) &&
          a5 == reinterpret_cast<void*>(5) && a6 == reinterpret_cast<void*>(6) &&
          a7 == reinterpret_cast<void*>(7) && a8 == 8, "all six other builder arguments preserved");
    std::array<char, 512> bytes{};
    std::size_t size = 0;
    check(capie::engine::macos::string_bytes(capie::core::ProcessMemory{}, reinterpret_cast<std::uintptr_t>(tag),
          std::span<char>(bytes), size), "native builder reads replacement string");
    observed.assign(bytes.data(), size);
    return out;
}
std::uint64_t update(std::uintptr_t) {
    ++updates;
    Native tag, out;
    capie::engine::macos::construct_name("VANILLA", tag, allocate);
    check(active_layer->build_name_key(&out, &tag, reinterpret_cast<void*>(3), reinterpret_cast<void*>(4),
          reinterpret_cast<void*>(5), reinterpret_cast<void*>(6), reinterpret_cast<void*>(7), 8) == &out,
          "builder result preserved");
    return 0x12345678;
}
void set_variable(std::uintptr_t, std::uintptr_t) { ++sets; }
void* faction_original(void* out, std::uintptr_t faction) {
    ++factions;
    check(faction == 0x7000, "SysV faction receiver order");
    capie::engine::macos::construct_name("VANILLA", *static_cast<Native*>(out), allocate);
    return out;
}
std::string read_native(const Native& value) {
    std::array<char, 512> bytes{};
    std::size_t size = 0;
    check(capie::engine::macos::string_bytes(capie::core::ProcessMemory{}, reinterpret_cast<std::uintptr_t>(&value),
          std::span<char>(bytes), size), "returned native string is valid");
    return {bytes.data(), size};
}
}
int main() {
    Memory memory;
    memory.add("capie_cn_ver", 100000);
    const auto name_count = memory.add("capie_cn_n^num", 100000);
    const auto first = memory.add("capie_cn_n^0", 2700000);
    memory.add("capie_cn_a^num", 100000);
    memory.add("capie_cn_a^0", 100000);
    memory.add("capie_fn_ver", 100000);
    memory.add("capie_fn_n^num", 3200000);
    for (unsigned i = 0; i < 32; ++i) memory.add("capie_fn_n^" + std::to_string(i), 2700000);
    memory.put<std::uintptr_t>(0x5000 + 0x2d8, 0x6000);
    memory.put<std::int32_t>(0x5000 + 0x2e4, 1);
    memory.put<std::uintptr_t>(0x6000, Memory::country);
    memory.put<std::uintptr_t>(0x7000 + 0x48, 0x7100);
    memory.put<std::int32_t>(0x7000 + 0x54, 1);
    memory.put<std::uintptr_t>(0x7100, Memory::country);
    Adapter adapter(memory);
    active_layer = &adapter;
    adapter.originals() = {update, build, set_variable, faction_original};
    check(adapter.update_names(Memory::country) == 0x12345678 && observed == "VANILLA", "inactive forwarding");
    adapter.activate(0x100000000, nullptr, allocate, release);
    check(adapter.enabled(), "native hooks require complete originals and allocators");
    check(adapter.update_names(Memory::country) == 0x12345678 && observed == "RN1|A|a|", "native name substitution");
    check(adapter.shown_key(Memory::country) == "RN1|A|a|", "shared shown cache");
    check(adapter.refresh_changed().refreshed == 0, "unchanged name is not refreshed");
    Native out;
    check(adapter.faction_name_sysv(&out, 0x7000) == &out && read_native(out) == std::string(32, 'A') && factions == 0,
          "native faction string transfers heap ownership");
    free_native(out);
    allocation_failure = true;
    check(adapter.faction_name_sysv(&out, 0x7000) == &out && read_native(out) == "VANILLA" && factions == 1,
          "allocation failure forwards faction exactly once");
    allocation_failure = false;
    memory.put<std::int64_t>(first + 0x20, 2800000);
    constexpr std::string_view commit = "capie_cn_rev";
    memory.put<std::uint8_t>(0x8000 + 0xc0, commit.size() * 2);
    std::memcpy(memory.data.data() + 0x8000 + 0xc1 - Memory::country, commit.data(), commit.size());
    adapter.set_variable(0x8000, 0);
    check(sets == 1 && observed == "RN1|B|a|" && updates == 3, "commit invokes original then refreshes changed name");
    memory.put<std::int64_t>(name_count + 0x20, 3200000);
    for (unsigned i = 1; i < 32; ++i) memory.add("capie_cn_n^" + std::to_string(i), 2700000);
    adapter.update_names(Memory::country);
    check(observed == "RN1|B" + std::string(31, 'A') + "|a|", "long native key reaches builder");
    check(allocated == released, "temporary builder heap freed after synchronous call");
    allocation_failure = true;
    memory.put<std::int64_t>(first + 0x20, 2900000);
    adapter.update_names(Memory::country);
    check(observed == "VANILLA", "failed temporary allocation preserves original tag");
    allocation_failure = false;
    check(adapter.refresh_changed().refreshed == 1 && observed == "RN1|C" + std::string(31, 'A') + "|a|",
          "failed replacement is not remembered as displayed");
    adapter.deactivate();
    check(adapter.faction_name_sysv(&out, 0x7000) == &out && read_native(out) == "VANILLA" && factions == 2,
          "inactive faction forwards original");
    Native long_key;
    check(capie::engine::macos::construct_name<layer::max_key_size>(std::string(200, 'A'), long_key, allocate),
          "full RN1 buffer bound can exceed faction bound");
    free_native(long_key);
    check(allocated == released, "all fixture-owned native allocations released");
    std::printf("macOS name hooks: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
