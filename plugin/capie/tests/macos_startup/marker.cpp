/** @file marker.cpp
 * @brief Observe thread-count snapshots from an owned injected library, without patching code.
 */
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <cstdio>
#include <unistd.h>
#ifdef WITH_WORKER
extern "C" int fixture_worker_ready();
#endif

/** @brief Enumerate current task threads, release all send rights and the returned array.
 * @param phase Observation label. @return Observed thread count, or -1 on enumeration failure.
 */
extern "C" int fixture_snapshot(const char* phase) {
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    const auto result = task_threads(mach_task_self(), &threads, &count);
    int translated = 0;
    std::size_t size = sizeof(translated);
    const int sysctl_result = sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0);
    const int observed = result == KERN_SUCCESS ? static_cast<int>(count) : -1;
    if (result == KERN_SUCCESS) {
        for (mach_msg_type_number_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
    }
    char message[192];
    const int length = std::snprintf(message, sizeof(message),
        "phase=%s threads=%d kr=%d translated=%d sysctl=%d\n", phase, observed, result, translated, sysctl_result);
    if (length > 0 && static_cast<std::size_t>(length) < sizeof(message)) write(STDOUT_FILENO, message, length);
    return observed;
}

/** @brief Observe the injected library's initializer before the owned executable enters main. */
__attribute__((constructor)) static void initialise() {
#ifdef WITH_WORKER
    if (!fixture_worker_ready()) _exit(71);
#endif
    fixture_snapshot("initializer");
}
