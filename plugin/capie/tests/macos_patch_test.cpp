/** @file macos_patch_test.cpp
 * @brief Quiescent owned-code detour tests; no game process is loaded.
 */
#include "../src/core/macos_patch.hpp"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
using namespace capie::core::macos::patch;
extern "C" int detour_fixture_text();
asm(".text\n.p2align 12\n.globl _detour_fixture_text\n_detour_fixture_text:\n.byte 0xb8,7,0,0,0\n.rept 9\nnop\n.endr\nret\n.p2align 12\n");
namespace {
int checks = 0, failures = 0;
void check(bool value) { ++checks; if (!value) ++failures; }
int replacement() { return 42; }
struct FaultMemory : NativeMemory {
    int writes = 0;
    int restores = 0;
    bool partial_source_write = false;
    bool first_restore_failure = false;
    bool persistent_restore_failure = false;
    bool allocation_failure = false;
    bool writable_failure = false;
    std::uintptr_t allocate(std::size_t size) {
        return allocation_failure ? 0 : NativeMemory::allocate(size);
    }
    bool writable(const Page& page) { return !writable_failure && NativeMemory::writable(page); }
    bool write(std::uintptr_t address, const void* bytes, std::size_t size) {
        ++writes;
        if (partial_source_write && writes == 2) { NativeMemory::write(address, bytes, 1); return false; }
        return NativeMemory::write(address, bytes, size);
    }
    bool restore(const Page& page) {
        ++restores;
        const bool actual = NativeMemory::restore(page);
        return actual && !persistent_restore_failure && !(first_restore_failure && restores == 1);
    }
};
}
int main() {
    NativeMemory memory;
    unsigned char original[15] = {0xb8, 7, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};
    auto* code = static_cast<unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
    check(code != MAP_FAILED);
    if (code == MAP_FAILED) return 1;
    std::memcpy(code, original, sizeof(original));
    check(mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0);
    const auto source = reinterpret_cast<std::uintptr_t>(code);
    const auto destination = reinterpret_cast<std::uintptr_t>(&replacement);
    using Function = int (*)();
    Function volatile function = reinterpret_cast<Function>(code);
    Hook hook;
    check(install(memory, hook, source, destination, {original, 14}, false) == Result::invalid);
    check(hook.state == State::empty);
    check(function() == 7);
    unsigned char wrong[14]; std::memcpy(wrong, original, 14); wrong[1] = 8;
    check(install(memory, hook, source, destination, wrong, true) == Result::mismatch);
    check(function() == 7);
    check(install(memory, hook, source, destination, {original, 13}, true) == Result::invalid);
    check(install(memory, hook, source, destination, {original, 14}, true) == Result::ok);
    check(hook.state == State::installed);
    check(function() == 42);
    check(reinterpret_cast<Function>(hook.trampoline)() == 7);
    check(remove(memory, hook, false) == Result::invalid);
    check(function() == 42);
    Page tamper_page{};
    check(memory.inspect(source,14,tamper_page));
    check(memory.writable(tamper_page));
    unsigned char modified = hook.patched[13] ^ 1;
    check(memory.write(source+13,&modified,1));
    check(memory.restore(tamper_page));
    check(remove(memory,hook,true)==Result::mismatch);
    unsigned char observed = 0;
    check(memory.read(source+13,&observed,1) && observed==modified);
    check(memory.writable(tamper_page));
    check(memory.write(source+13,hook.patched.data()+13,1));
    check(memory.restore(tamper_page));
    check(remove(memory, hook, true) == Result::ok);
    check(function() == 7);
    check(std::memcmp(code, original, 15) == 0);
    check(hook.state == State::empty);
    check(remove(memory, hook, true) == Result::invalid);
    const unsigned char rip[] = {0x48,0x8b,0x05,0,0,0,0,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    check(!relocation_free(rip));
    const unsigned char call[] = {0xe8,0,0,0,0,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    check(!relocation_free(call));
    check(relocation_free({original,14}));
    for (const unsigned char rex : {0x48, 0x49}) {
        for (unsigned char reg = 0; reg < 8; ++reg) {
            const unsigned char movabs[] = {rex, static_cast<unsigned char>(0xb8 + reg),
                                           0, 1, 2, 3, 4, 5, 6, 7};
            check(relocation_free(movabs));
            check(!relocation_free({movabs, 9}));
        }
    }
    const unsigned char lookup_n[] = {0x55,0x48,0x89,0xe5,0x49,0xb8,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb};
    const unsigned char lookup_z[] = {0x55,0x48,0x89,0xe5,0x48,0xbe,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb};
    const unsigned char exists_prefix[] = {0x55,0x48,0x89,0xe5,0x48,0xbe,0x25,0x23,0x22,0x84,0xe4,0x9c,0xf2,0xcb};
    check(relocation_free(lookup_n));
    check(relocation_free(lookup_z));
    check(relocation_free(exists_prefix));
    check(install(memory, hook, source, destination, rip, true) == Result::unsupported);
    check(function() == 7);
    check(install(memory, hook, 0, destination, {original,14}, true) == Result::invalid);
    check(install(memory, hook, source, source, {original,14}, true) == Result::invalid);
    Page page{};
    check(!memory.inspect(source+4090,14,page));
    FaultMemory allocation; allocation.allocation_failure=true;
    check(install(allocation,hook,source,destination,{original,14},true)==Result::allocation_failed);
    check(hook.state==State::empty && function()==7);
    FaultMemory protection; protection.writable_failure=true;
    check(install(protection,hook,source,destination,{original,14},true)==Result::protection_failed);
    check(hook.state==State::empty && function()==7);
    FaultMemory partial; partial.partial_source_write=true;
    check(install(partial,hook,source,destination,{original,14},true)==Result::rolled_back);
    check(hook.state==State::empty && function()==7 && std::memcmp(code,original,15)==0);
    FaultMemory transient; transient.first_restore_failure=true;
    check(install(transient,hook,source,destination,{original,14},true)==Result::rolled_back);
    check(hook.state==State::empty && function()==7);
    FaultMemory persistent; persistent.persistent_restore_failure=true;
    check(install(persistent,hook,source,destination,{original,14},true)==Result::rollback_failed);
    check(hook.state==State::poisoned && hook.trampoline!=0);
    check(install(memory,hook,source,destination,{original,14},true)==Result::invalid);
    persistent.persistent_restore_failure=false;
    check(rollback(persistent,hook));
    check(hook.state==State::empty && function()==7);
    munmap(code,4096);
    const auto text = reinterpret_cast<std::uintptr_t>(&detour_fixture_text);
    Page before{}, after{};
    check(memory.inspect(text,14,before));
    check(install(memory,hook,text,destination,{original,14},true)==Result::ok);
    check(detour_fixture_text()==42);
    check(reinterpret_cast<Function>(hook.trampoline)()==7);
    check(remove(memory,hook,true)==Result::ok);
    check(detour_fixture_text()==7);
    check(memory.inspect(text,14,after));
    check(before.current==after.current && before.maximum==after.maximum);
    std::printf("macOS quiescent patch: %d checks, %d failures\n",checks,failures);
    return failures != 0;
}
