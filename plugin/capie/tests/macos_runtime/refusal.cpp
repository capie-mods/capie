/** @brief Load only an explicitly supplied runtime into this owned foreign process. */
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 3) return 64;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "%s\n", dlerror()); return 1; }
    const auto state = reinterpret_cast<unsigned (*)()>(dlsym(library, "capie_macos_runtime_state"));
    const unsigned expected = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    if (!state || state() != expected) return 2;
    std::printf("runtime state %u; foreign process alive\n", state());
    return 0;
}
