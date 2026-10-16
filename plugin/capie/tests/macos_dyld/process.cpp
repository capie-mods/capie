/** @file process.cpp @brief Own-process fixture for the hardened-parent dyld environment boundary. */
#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

/**
 * @brief Reports marker presence and optionally executes a supplied fixture child.
 * @param argc Argument count; one for child, four for parent.
 * @param argv Parent arguments: inherited|explicit, child path, marker dylib path.
 * @return Zero for child; two for invalid input/environment error; three for exec failure.
 */
int main(int argc, char** argv) {
    if (argc != 1 && argc != 4) {
        return 2;
    }
    const bool parent = argc == 4;
    std::printf("%s loaded=%d env=%d\n", parent ? "parent" : "child",
                dlsym(RTLD_DEFAULT, "capie_fixture_loaded") != nullptr,
                std::getenv("DYLD_INSERT_LIBRARIES") != nullptr);
    std::fflush(stdout);
    if (!parent) {
        return 0;
    }
    if (std::strcmp(argv[1], "explicit") == 0) {
        if (setenv("DYLD_INSERT_LIBRARIES", argv[3], 1) != 0) {
            return 2;
        }
    } else if (std::strcmp(argv[1], "inherited") != 0) {
        return 2;
    }
    char* args[] = {argv[2], nullptr};
    execv(argv[2], args);
    std::perror("execv");
    return 3;
}
