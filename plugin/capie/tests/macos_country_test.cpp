/** @file macos_country_test.cpp @brief Composition wiring against owned callbacks; no patches/game. */
#include "../src/dll/macos_country.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
unsigned checks = 0, failures = 0, calls = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
struct Memory {
    bool read(std::uintptr_t, void*, std::size_t) const noexcept { return false; }
    bool write(std::uintptr_t, const void*, std::size_t) const noexcept { return false; }
};
using Runtime = capie::country::macos::Runtime<Memory>;
constexpr char fallback[] = "original";
const char* n(const char* key, int len) {
    ++calls;
    check(key == nullptr && len == -1, "lookup n args");
    return fallback;
}
const char* z(const char*) {
    ++calls;
    return fallback;
}
bool exists(const char*) {
    ++calls;
    return false;
}
std::uint64_t update(std::uintptr_t c) {
    ++calls;
    check(c == 0, "country forwarded");
    return 91;
}
void* build(void* o, const void* t, void* a, void* b, void* c, void* d, void* e, char f) {
    ++calls;
    check(t == reinterpret_cast<void*>(2) && a == reinterpret_cast<void*>(3) && b == reinterpret_cast<void*>(4) &&
              c == reinterpret_cast<void*>(5) && d == reinterpret_cast<void*>(6) && e == reinterpret_cast<void*>(7) &&
              f == 8,
          "build args");
    return o;
}
void set(std::uintptr_t e, std::uintptr_t c) {
    ++calls;
    check(e == 0 && c == 0, "set args");
}
void* faction(void* o, std::uintptr_t f) {
    ++calls;
    check(f == 123, "SysV faction receiver after out");
    return o;
}
void atlas(std::uintptr_t a, std::uintptr_t g) {
    ++calls;
    check(a == 1 && g == 2, "void atlas args");
}
int flag(std::uintptr_t a, std::uintptr_t c, void* d, int x, int y, int z_, int w, void* p) {
    ++calls;
    check(a == 1 && c == 0 && d == reinterpret_cast<void*>(3) && x == 4 && y == 5 && z_ == 6 && w == 7 &&
              p == reinterpret_cast<void*>(8),
          "flag args");
    return 31;
}
void* candidates(void* o, const void*, const void*, void*) {
    ++calls;
    return o;
}
void* portrait(void* o, void* map, std::int32_t t, std::int32_t s, std::uintptr_t c, const void*) {
    ++calls;
    check(map == nullptr && t == 9 && s == 8 && c == 0, "SysV portrait output precedes map");
    return o;
}
bool valid(const void*) {
    return false;
}
void* allocate(std::size_t n_) noexcept {
    return std::malloc(n_);
}
void release(void* p) noexcept {
    std::free(p);
}
}  // namespace
int main() {
    Memory memory;
    Runtime runtime(memory);
    check(!runtime.entrypoints().has_value() && runtime.capabilities() == 0, "default off/unbound");
    capie::core::macos::image::Image image;
    image.count = 1;
    image.segments[0] = {0x100000000, 0x1000, 0, 0x1000, VM_PROT_READ | VM_PROT_EXECUTE};
    Runtime::Configuration config;
    config.image = &image;
    config.lookup_n = n;
    config.lookup_z = z;
    config.exists = exists;
    config.names = {update, build, set, faction};
    config.flags = {atlas, flag, candidates};
    config.portraits.resolve = portrait;
    config.portraits.valid = valid;
    config.allocate = allocate;
    config.release = release;
    auto bad = config;
    bad.lookup_z = nullptr;
    check(!runtime.configure(bad) && !runtime.entrypoints(), "missing original refuses binding");
    bad = config;
    bad.allocate = nullptr;
    check(!runtime.configure(bad), "missing allocator refused");
    bad = config;
    bad.image = nullptr;
    check(!runtime.configure(bad), "missing verified image refused");
    bad = config;
    bad.names.faction_name = nullptr;
    check(!runtime.configure(bad), "missing faction original refused");
    bad = config;
    bad.flags.candidates = nullptr;
    check(!runtime.configure(bad), "missing flag original refused");
    bad = config;
    bad.portraits.valid = nullptr;
    check(!runtime.configure(bad), "missing portrait validity refused");
    check(runtime.configure(config), "explicit configure succeeds");
    check(!runtime.configure(config), "no rebind after callbacks published");
    {
        Runtime second(memory);
        check(!second.configure(config), "exclusive shared localisation owner");
    }

    const auto hooks = *runtime.entrypoints();
    check(hooks.lookup_z("RN1|Polska|Polski|") == fallback, "inactive RN1 forwarded");
    check(hooks.lookup_n(nullptr, -1) == fallback, "inactive n forwarded");
    check(!hooks.exists("RN1|Polska|Polski|"), "inactive exists forwarded");
    check(hooks.update_names(0) == 91, "inactive update result");
    void* out = reinterpret_cast<void*>(1);
    check(hooks.build_name_key(out, reinterpret_cast<void*>(2), reinterpret_cast<void*>(3), reinterpret_cast<void*>(4),
                               reinterpret_cast<void*>(5), reinterpret_cast<void*>(6), reinterpret_cast<void*>(7),
                               8) == out,
          "inactive builder result");
    hooks.set_variable(0, 0);
    check(hooks.faction_name(out, 123) == out, "inactive sret result");
    hooks.flag_atlas(1, 2);
    check(hooks.load_flag(1, 0, reinterpret_cast<void*>(3), 4, 5, 6, 7, reinterpret_cast<void*>(8)) == 31,
          "flag result");
    check(hooks.flag_candidates(out, nullptr, nullptr, nullptr) == out, "candidate result");
    check(hooks.portrait_resolve(out, nullptr, 9, 8, 0, nullptr) == out, "portrait result");
    check(calls == 11, "each inactive original exactly once");
    check(!runtime.enable({false, true, false}, true), "dependent flag cannot enable without names");
    check(!runtime.enable({true, true, true}), "full contract gate defaults false");
    check(hooks.lookup_z("CAPIE_API") == fallback && hooks.lookup_z("RN_PLUGIN_STATUS") == fallback,
          "no false public API/status");
    check(runtime.enable({true, true, true}, true), "all configured layers enable under fixture attestation");
    check(runtime.capabilities() == capie::country::capabilities_of(true, true, true), "internal capability bits");
    check(std::strcmp(hooks.lookup_z("RN1|Polska|Polski|"), "Polska") == 0, "shared RN1 active");
    check(hooks.exists("RN1|Polska|Polski|"), "shared RN1 existence");
    check(std::strcmp(hooks.lookup_z("CAPIE_API"), capie::core::capie_api_text) == 0,
          "shared status only after fixture full-ready");
    const char* held = hooks.lookup_z("RN1|Polska|Polski|");
    runtime.disable();
    check(runtime.capabilities() == 0 && hooks.lookup_z("RN1|Polska|Polski|") == fallback,
          "disabled forwards retained original");
    check(std::strcmp(held, "Polska") == 0, "interned answer survives disable");
    check(hooks.update_names(0) == 91, "disabled name hook forwards");
    check(
        runtime.enable({true, false, false}, true) && runtime.capabilities() == capie::country::country_capabilities_v2,
        "selected layers map capability bits");
    runtime.disable();
    check(runtime.capabilities() == 0, "cleanup restores no capabilities");

    std::printf("macOS country composition: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
