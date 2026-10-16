/** @file macos_name_layer_test.cpp
 * @brief Shared country/faction protocol against synthetic macOS variable storage; no game calls.
 */
#include "../src/features/country/name_layer.hpp"
#include "../src/features/country/flag_layer.hpp"
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
int main() {
    Memory memory;
    check(layer::read_layer(memory, Memory::country).status == layer::Status::absent, "missing version");
    const auto version = memory.add("capie_cn_ver", 100000);
    const auto count = memory.add("capie_cn_n^num", 100000);
    const auto name = memory.add("capie_cn_n^0", 2700000);
    memory.add("capie_cn_a^num", 100000);
    memory.add("capie_cn_a^0", 100000);
    auto result = layer::read_layer(memory, Memory::country);
    check(result.status == layer::Status::valid && result.view() == "RN1|A|a|", "shared RN1 encoding");
    memory.put<std::int64_t>(name + 0x20, 6700000);
    result = layer::read_layer(memory, Memory::country);
    check(result.status == layer::Status::valid && result.view() == "RN1|ą|a|", "shared Polish alphabet");
    memory.put<std::int64_t>(version + 0x20, 200000);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::invalid, "unknown version");
    memory.put<std::int64_t>(version + 0x20, 100000);
    memory.put<std::int64_t>(count + 0x20, 3300000);
    result = layer::read_layer(memory, Memory::country);
    check(result.status == layer::Status::invalid && result.view().empty(), "count too large hides partial key");
    memory.put<std::int64_t>(count + 0x20, 100001);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::invalid, "fractional count");
    memory.put<std::int64_t>(count + 0x20, 100000);
    memory.put<std::int64_t>(name + 0x20, -100000);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::invalid, "negative code");
    memory.put<std::int64_t>(name + 0x20, 26500000);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::invalid, "code outside shared alphabet");
    memory.put<std::int64_t>(name + 0x20, 2700000);
    memory.denied = name + 0x20;
    result = layer::read_layer(memory, Memory::country);
    check(result.status == layer::Status::unreadable && result.view().empty(), "failed read hides partial key");
    memory.denied = 0;
    memory.add("capie_fn_ver", 100000);
    memory.add("capie_fn_n^num", 100000);
    memory.add("capie_fn_n^0", 2800000);
    result = layer::read_faction_layer(memory, Memory::country);
    check(result.status == layer::Status::valid && result.view() == "B", "shared faction encoding");
    memory.put<std::int64_t>(count + 0x20, 200000);
    result = layer::read_layer(memory, Memory::country);
    check(result.status == layer::Status::invalid && result.view().empty(), "missing array element");
    memory.put<std::int64_t>(count + 0x20, 0);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::invalid, "empty name");
    Memory maximum_name;
    maximum_name.add("capie_cn_ver", 100000);
    maximum_name.add("capie_cn_n^num", 3200000);
    maximum_name.add("capie_cn_a^num", 100000);
    maximum_name.add("capie_cn_a^0", 100000);
    for (unsigned i = 0; i < 32; ++i) {
        maximum_name.add("capie_cn_n^" + std::to_string(i), 2700000);
    }
    result = layer::read_layer(maximum_name, Memory::country);
    check(result.status == layer::Status::valid && result.name_points == 32 &&
          result.view() == "RN1|" + std::string(32, 'A') + "|a|", "full 32-character contract");
    memory.put<std::uintptr_t>(Memory::country + 0x1d8, 0);
    check(layer::read_layer(memory, Memory::country).status == layer::Status::absent, "null country container");
    check(layer::read_layer(memory, 0).status == layer::Status::unreadable, "null country refused");
    check(layer::read_layer(memory, std::numeric_limits<std::uintptr_t>::max() - 1).status ==
          layer::Status::unreadable, "country offset overflow");
    namespace flag = capie::country::flag;
    Memory flags;
    check(flag::read_flag_layer(flags, Memory::country).status == layer::Status::absent, "flag absent");
    flags.add("capie_cf_ver", 100000);
    const auto flag_length = flags.add("capie_cf_len", 300000);
    flags.add("capie_cf_s^num", 100000);
    const auto packed = flags.add("capie_cf_s^0", (1 * 4096LL + 27 * 64 + 63) * 100000);
    auto flag_value = flag::read_flag_layer(flags, Memory::country);
    check(flag_value.status == layer::Status::valid && flag_value.stem.view() == "aA_", "flag packed alphabet");
    check(flag::flag_shown(flag_value, "OTHER", false), "flag without snapshot");
    flags.add("capie_cf_cos_len", 300000);
    flags.add("capie_cf_cos^num", 100000);
    flags.add("capie_cf_cos^0", (27 * 4096LL + 28 * 64 + 29) * 100000);
    flag_value = flag::read_flag_layer(flags, Memory::country);
    check(flag_value.status == layer::Status::valid && flag_value.snapshot.view() == "ABC" &&
          flag::flag_shown(flag_value, "ABC", false) && !flag::flag_shown(flag_value, "ABD", false) &&
          !flag::flag_shown(flag_value, "ABC", true), "flag cosmetic gate");
    flags.put<std::int64_t>(flag_length + 0x20, 200000);
    flag_value = flag::read_flag_layer(flags, Memory::country);
    check(flag_value.status == layer::Status::invalid && flag_value.stem.view().empty(), "flag nonzero padding rejected");
    flags.put<std::int64_t>(flag_length + 0x20, 6400000);
    check(flag::read_flag_layer(flags, Memory::country).status == layer::Status::invalid, "flag overlong stem");
    flags.put<std::int64_t>(flag_length + 0x20, 300000);
    flags.denied = packed + 0x20;
    check(flag::read_flag_layer(flags, Memory::country).status == layer::Status::unreadable, "flag unreadable value");
    flags.denied = 0;
    constexpr auto cosmetic = Memory::country + 0x1200;
    flags.put<std::uint8_t>(cosmetic, 6);
    flags.put<std::uint32_t>(cosmetic + 1, 0x00434241);
    flag::Text text;
    bool too_long = true;
    check(flag::read_cosmetic(flags, Memory::country, text, too_long) && text.view() == "ABC" && !too_long,
          "cosmetic libc++ short string");
    flags.put<std::uint8_t>(cosmetic, 0);
    check(flag::read_cosmetic(flags, Memory::country, text, too_long) && text.view().empty() && !too_long,
          "cosmetic empty string");
    flags.put<std::uint64_t>(cosmetic, 65); // 64 allocated bytes, capacity 63.
    flags.put<std::uint64_t>(cosmetic + 8, 63);
    flags.put<std::uintptr_t>(cosmetic + 16, 0x7000);
    for (unsigned i = 0; i < 63; ++i) flags.put<char>(0x7000 + i, 'A');
    flags.denied = 0x703f; // Windows contract compares content without requiring a terminator.
    check(flag::read_cosmetic(flags, Memory::country, text, too_long) && text.view() == std::string(63, 'A') && !too_long,
          "cosmetic libc++ heap boundary without terminator");
    flags.denied = 0x7000;
    check(!flag::read_cosmetic(flags, Memory::country, text, too_long), "cosmetic unreadable heap");
    flags.put<std::uint64_t>(cosmetic, 67);
    flags.put<std::uint64_t>(cosmetic + 8, 64);
    check(flag::read_cosmetic(flags, Memory::country, text, too_long) && too_long && text.view().empty(),
          "cosmetic too long without reading payload");
    flags.put<std::uint64_t>(cosmetic + 8, 66);
    check(!flag::read_cosmetic(flags, Memory::country, text, too_long) && !too_long, "cosmetic invalid capacity");
    flags.denied = cosmetic;
    check(!flag::read_cosmetic(flags, Memory::country, text, too_long), "cosmetic unreadable header");
    check(!flag::read_cosmetic(flags, 0, text, too_long), "cosmetic null country");
    check(!flag::read_cosmetic(flags, std::numeric_limits<std::uintptr_t>::max() - 1, text, too_long),
          "cosmetic address overflow");
    std::printf("macOS shared name layer: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
