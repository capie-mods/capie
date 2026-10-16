/** @file probe.cpp
 * @brief Owned x86_64 executable/anonymous page protection experiment; never attaches to another process.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" int fixture_target();
// Isolate the only modified function from all code running during the write window.
asm(".text\n.p2align 14\n.globl _fixture_target\n_fixture_target:\n.byte 0xb8,7,0,0,0,0xc3\n.p2align 14\n");

/** @brief Print current and maximum protections of a queried mapping. */
static bool region(void* pointer, const char* label) {
    mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(pointer);
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    auto result = mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                                reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
    std::printf("region %s kr=%d protection=%d max=%d size=%llu\n", label, result,
                info.protection, info.max_protection, size);
    return result == KERN_SUCCESS && info.protection == (VM_PROT_READ | VM_PROT_EXECUTE);
}

/** @brief Run one isolated protection/write/restore experiment. */
int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    if (argc != 3) return 2;
    const bool anonymous = std::strcmp(argv[1], "anonymous") == 0;
    const bool copy = std::strcmp(argv[2], "mach-copy") == 0;
    int translated = 0;
    std::size_t translated_size = sizeof(translated);
    const int sysctl_result = sysctlbyname("sysctl.proc_translated", &translated, &translated_size, nullptr, 0);
    const auto page_size = static_cast<std::size_t>(getpagesize());
    std::printf("translated=%d sysctl=%d page_size=%zu target=%s method=%s\n", translated,
                sysctl_result, page_size, argv[1], argv[2]);
    unsigned char* code = reinterpret_cast<unsigned char*>(&fixture_target);
    const unsigned char original[] = {0xb8, 7, 0, 0, 0, 0xc3};
    if (anonymous) {
        code = static_cast<unsigned char*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                                               MAP_ANON | MAP_PRIVATE, -1, 0));
        if (code == MAP_FAILED) { std::perror("mmap"); return 3; }
        std::memcpy(code, original, sizeof(original));
        if (mprotect(code, page_size, PROT_READ | PROT_EXEC) != 0) { std::perror("initial RX"); return 4; }
    }
    using Function = int (*)();
    Function volatile function = reinterpret_cast<Function>(code);
    if (!region(code, "before") || function() != 7 || std::memcmp(code, original, 6) != 0) return 5;
    const auto start = reinterpret_cast<std::uintptr_t>(code) & ~(page_size - 1);
    auto writable = [&]() {
        if (copy) {
            const auto result = mach_vm_protect(mach_task_self(), start, page_size, false,
                                               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
            std::printf("writable mach kr=%d error=%s\n", result, mach_error_string(result));
            return result == KERN_SUCCESS;
        }
        const int result = mprotect(reinterpret_cast<void*>(start), page_size, PROT_READ | PROT_WRITE);
        std::printf("writable mprotect result=%d errno=%d\n", result, result == 0 ? 0 : errno);
        if (result != 0) std::perror("writable mprotect");
        return result == 0;
    };
    auto executable = [&]() {
        const auto result = mach_vm_protect(mach_task_self(), start, page_size, false,
                                           VM_PROT_READ | VM_PROT_EXECUTE);
        std::printf("restore RX kr=%d error=%s\n", result, mach_error_string(result));
        sys_icache_invalidate(code, 6);
        return result == KERN_SUCCESS && region(code, "RX");
    };
    if (!writable()) return 10;
    region(code, "writable");
    code[1] = 42;
    if (!executable()) return 11;
    const int patched = function();
    std::printf("patched_call=%d\n", patched);
    if (patched != 42) return 12;
    if (!writable()) return 13;
    std::memcpy(code, original, 6);
    if (!executable()) return 14;
    const int restored = function();
    const bool bytes_restored = std::memcmp(code, original, 6) == 0;
    std::printf("restored_call=%d original_bytes=%d\n", restored, bytes_restored);
    if (!anonymous) {
        const auto max_result = mach_vm_protect(mach_task_self(), start, page_size, true,
                                               VM_PROT_READ | VM_PROT_EXECUTE);
        std::printf("restore maximum RX kr=%d error=%s\n", max_result, mach_error_string(max_result));
        if (max_result != KERN_SUCCESS || !region(code, "maximum-restored")) return 16;
    }
    if (anonymous) munmap(code, page_size);
    return restored == 7 && bytes_restored ? 0 : 15;
}
