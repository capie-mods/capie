/** @file macos_portrait_layer_test.cpp @brief Native portrait read protocol and leader chain, synthetic memory only. */
#include "../src/features/country/portrait_layer_hooks.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
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
namespace {
unsigned calls = 0, allocations = 0, frees = 0;
bool valid_sprite = true, fail_allocate = false;
void* expected_out;
void* expected_map;
const void* expected_fallback;
void* original(void* out, void* map, std::int32_t, std::int32_t, std::uintptr_t, const void* fallback) {
    ++calls;
    check(out == expected_out && map == expected_map && fallback == expected_fallback,
          "SysV argument order and fallback identity");
    return out;
}
bool valid(const void* entry) {
    auto* p = static_cast<const portrait::Entry*>(entry);
    check(p->set == 1 && p->kind == 0 && p->handle == 0, "native validation fields");
    return valid_sprite;
}
void* allocate(std::size_t size) noexcept {
    if (fail_allocate)
        return nullptr;
    ++allocations;
    return std::malloc(size);
}
void release(void* ptr) noexcept {
    ++frees;
    std::free(ptr);
}
void seed(Memory& mem, unsigned length) {
    mem.add("capie_pt_ver", 100000);
    mem.add("capie_pt_len", length * 100000);
    mem.add("capie_pt_s^num", ((length + 2) / 3) * 100000);
    for (unsigned i = 0; i < (length + 2) / 3; ++i) {
        auto key = std::string("capie_pt_s^") + std::to_string(i);
        unsigned value = 0;
        for (unsigned j = 0; j < 3; ++j)
            if (i * 3 + j < length)
                value |= 1u << (12 - 6 * j);
        mem.add(key, value * std::int64_t{100000});
    }
}
}  // namespace
int main() {
    static_assert(sizeof(portrait::Entry) == 0x28);
    Memory mem;
    seed(mem, 24);
    portrait::PortraitLayer<Memory> layer(mem);
    layer.originals() = {original, valid, allocate, release};
    layer.activate(Memory::image, nullptr);
    check(layer.update(Memory::country), "cache native embedded layer");
    portrait::Entry out{};
    int map = 0, fallback = 0;
    expected_out = &out;
    expected_map = &map;
    expected_fallback = &fallback;
    check(layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback) == &out, "SysV result pointer");
    check(calls == 0 && allocations == 1 && frees == 0 && out.name.bytes[0] & 1, "long result ownership transferred");
    void* ptr = nullptr;
    std::memcpy(&ptr, out.name.bytes.data() + 16, 8);
    check(std::string_view(static_cast<char*>(ptr)) == std::string(24, 'a'), "owned result content");
    release(ptr);
    valid_sprite = false;
    layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback);
    check(calls == 1 && allocations == 2 && frees == 2, "rejected temporary freed before fallback");
    valid_sprite = true;
    fail_allocate = true;
    layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback);
    check(calls == 2 && allocations == 2, "allocation failure falls through");
    fail_allocate = false;
    layer.resolve_sysv(&out, &map, 1, 1, Memory::character, &fallback);
    check(calls == 3, "noncivilian fallback");
    layer.resolve_sysv(&out, &map, 0, 0, Memory::character, &fallback);
    check(calls == 4, "absent small fallback");
    mem.put<std::int32_t>(Memory::character + 0xc, 123);
    layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback);
    check(calls == 5, "changed character identity fallback");
    layer.deactivate();
    layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback);
    check(calls == 6, "disabled fallback");
    Memory short_mem;
    seed(short_mem, 1);
    portrait::PortraitLayer<Memory> short_layer(short_mem);
    short_layer.originals() = {original, valid, allocate, release};
    short_layer.activate(Memory::image, nullptr);
    check(short_layer.update(Memory::country), "short cache");
    short_layer.resolve_sysv(&out, &map, 0, 1, Memory::character, &fallback);
    check(out.name.bytes[0] == 2 && out.name.bytes[1] == 'a' && allocations == 2, "SSO result no allocation");
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
