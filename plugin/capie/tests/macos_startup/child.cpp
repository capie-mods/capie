/** @file child.cpp
 * @brief Owned executable observes injected marker state at main; no game is loaded.
 */
#include <dlfcn.h>
/** @brief Call the injected observer in main. @return Failure if insertion or enumeration failed. */
int main() {
    using Snapshot = int (*)(const char*);
    const auto snapshot = reinterpret_cast<Snapshot>(dlsym(RTLD_DEFAULT, "fixture_snapshot"));
    if (!snapshot) return 73;
    return snapshot("main") < 1 ? 74 : 0;
}
