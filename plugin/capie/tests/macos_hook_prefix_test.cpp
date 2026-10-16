/** @brief Verify all release hook prefixes fit the restricted relocation-free backend. */
#include "../src/core/macos_hook_prefixes.h"
#include "../src/core/macos_patch.hpp"
#include <cstdio>
int main() {
    unsigned failures = 0;
    for (const auto& entry : capie_macos_hook_prefixes) {
        if (entry.length < capie::core::macos::patch::jump_size ||
            !capie::core::macos::patch::relocation_free({entry.bytes, entry.length})) {
            ++failures;
            std::printf("FAIL unsupported prefix: %s\n", entry.id);
        }
    }
    std::printf("macOS release hook prefixes: %zu checks, %u failures\n", capie_macos_hook_prefix_count, failures);
    return failures ? 1 : 0;
}
