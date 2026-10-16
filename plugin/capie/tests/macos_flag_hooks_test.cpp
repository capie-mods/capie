/** @file macos_flag_hooks_test.cpp @brief Native flag hook arguments, strings and bounded atlas writes. */
#include "../src/features/country/flag_layer_hooks.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
namespace flag = capie::country::flag;
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
    static constexpr std::uintptr_t base = 0x1000, country = base, entries = 0x9000, image = 0x100000000;
    mutable std::array<unsigned char, 0xc000> data{};
    mutable unsigned writes = 0;
    bool write(std::uintptr_t address, const void* input, std::size_t size) const noexcept {
        if (address < base || address - base > data.size() || size > data.size() - (address - base))
            return false;
        std::memcpy(data.data() + address - base, input, size);
        ++writes;
        return true;
    }
    std::uintptr_t denied = 0;
    unsigned count = 0;
    bool read(std::uintptr_t address, void* out, std::size_t size) const noexcept {
        if (address == image + 0x3519740 && size == 8) {
            std::uintptr_t state = 0x3000;
            std::memcpy(out, &state, 8);
            return true;
        }
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
        put(country + 0x1d8, std::uintptr_t{0x8000});
        put(std::uintptr_t{0x8010}, entries);
        put<std::uint32_t>(0x801c, 0);
        put<std::uint8_t>(0x8020, 255);
        put(std::uintptr_t{0x32d8}, std::uintptr_t{0x4000});
        put<std::int32_t>(0x32e4, 2);
        put(std::uintptr_t{0x4008}, country);
        put(std::uintptr_t{0x50c0}, std::uintptr_t{0x6000});
        put<std::int32_t>(0x50cc, 2);
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
flag::FlagLayer<Memory>* layer = nullptr;
unsigned loads = 0, allocs = 0, frees = 0, atlas_calls = 0;
int status = 0;
bool fail_alloc = false;
bool override_seen = false;
bool wrong_cosmetic = false;
void* allocate(std::size_t n) noexcept {
    if (fail_alloc)
        return nullptr;
    ++allocs;
    return std::malloc(n);
}
void release(void* p) noexcept {
    ++frees;
    std::free(p);
}
void atlas(std::uintptr_t a, std::uintptr_t gfx) {
    check(a == 0x5000 && gfx == 12, "atlas ABI args");
    ++atlas_calls;
}
std::string native_string(const void* p) {
    auto* b = static_cast<const unsigned char*>(p);
    if (!(b[0] & 1))
        return {reinterpret_cast<const char*>(b + 1), std::size_t(b[0] >> 1)};
    const char* ptr;
    std::size_t len;
    std::memcpy(&len, b + 8, 8);
    std::memcpy(&ptr, b + 16, 8);
    return {ptr, len};
}
void* candidates(void* out, const void* tag, const void* cosmetic, void* group) {
    if (tag != reinterpret_cast<void*>(0x111)) {
        override_seen = true;
        check(native_string(tag) == std::string(24, 'a') && native_string(cosmetic).empty() && group == nullptr,
              "native candidate input strings");
    }
    return out;
}
int load(std::uintptr_t a, std::uintptr_t country, void* dst, int a4, int a5, int a6, int a7, void* a8) {
    ++loads;
    check(a == 0x5000 && country == Memory::country && dst == reinterpret_cast<void*>(3) && a4 == 4 && a5 == 5 &&
              a6 == 6 && a7 == 7 && a8 == reinterpret_cast<void*>(8),
          "all eight SysV load arguments");
    layer->candidates(nullptr, reinterpret_cast<void*>(0x111),
                      reinterpret_cast<void*>(country + (wrong_cosmetic ? 0x1208 : 0x1200)),
                      reinterpret_cast<void*>(0x222));
    return loads % 2 == 1 ? status : 0;
}
void seed(Memory& mem) {
    mem.add("capie_cf_ver", 100000);
    mem.add("capie_cf_len", 2400000);
    mem.add("capie_cf_s^num", 800000);
    for (unsigned i = 0; i < 8; ++i)
        mem.add(std::string("capie_cf_s^") + std::to_string(i), 4161LL * 100000);
}
}  // namespace
int main() {
    Memory mem;
    seed(mem);
    flag::FlagLayer<Memory> flags(mem);
    layer = &flags;
    flags.originals() = {atlas, load, candidates, allocate, release};
    flags.activate(Memory::image, nullptr);
    check(flags.update(Memory::country), "native flag cache");
    auto invoke = [&] {
        return flags.load_flag(0x5000, Memory::country, reinterpret_cast<void*>(3), 4, 5, 6, 7,
                               reinterpret_cast<void*>(8));
    };
    check(invoke() == 0 && override_seen && allocs == 1 && frees == 1, "long temporary freed after builder");
    flags.atlas_update(0x5000, 12);
    check(mem.writes == 1 && atlas_calls == 1, "slot invalidation native layout");
    std::uint64_t key = 0;
    mem.read(0x6008, &key, 8);
    check(key == ~std::uint64_t{0}, "forced native key");
    flags.atlas_update(0x5000, 12);
    check(mem.writes == 1, "unchanged generation no write");
    loads = 0;
    status = 2;
    override_seen = false;
    check(invoke() == 0 && loads == 2 && override_seen, "failed layer reloads vanilla");
    loads = 0;
    status = 5;
    check(invoke() == 5 && loads == 1, "alternate native success");
    loads = 0;
    fail_alloc = true;
    override_seen = false;
    check(invoke() == 5 && !override_seen, "allocation failure delegates candidate");
    fail_alloc = false;
    loads = 0;
    wrong_cosmetic = true;
    override_seen = false;
    invoke();
    check(!override_seen, "foreign cosmetic pointer preserves inputs");
    wrong_cosmetic = false;
    override_seen = false;
    flags.candidates(nullptr, reinterpret_cast<void*>(0x111), reinterpret_cast<void*>(Memory::country + 0x1200),
                     nullptr);
    check(!override_seen, "candidate outside load has no frame");
    flags.deactivate();
    loads = 0;
    override_seen = false;
    invoke();
    check(!override_seen, "disabled vanilla");
    check(!flag::guarded_write(0, nullptr, 8), "null write refused");
    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    void* page = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    check(page != MAP_FAILED, "private writable page");
    if (page != MAP_FAILED) {
        std::uint64_t value = 42;
        auto addr = reinterpret_cast<std::uintptr_t>(page);
        check(flag::guarded_write(addr, &value, 8) && *static_cast<std::uint64_t*>(page) == 42, "kernel qword write");
        check(!flag::guarded_write(addr, &value, 4), "non-qword write refused");
        check(mprotect(page, page_size, PROT_READ) == 0, "make page readonly");
        check(!flag::guarded_write(addr, &value, 8), "readonly write refused");
        munmap(page, page_size);
        check(!flag::guarded_write(addr, &value, 8), "unmapped write refused");
    }
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
