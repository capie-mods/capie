/** @file macos_bootstrap_api.h
 * @brief Process-lifetime C interface; hook indices follow macos_hook_prefixes.h.
 * FORWARDING proves the complete static profile was checked before patching.
 * It does not prove initialized game tokens or feature ABI correctness.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Disabled includes identity, ordering, policy and recoverable installation refusal. */
enum capie_macos_bootstrap_status {
    CAPIE_BOOTSTRAP_DISABLED = 0,
    CAPIE_BOOTSTRAP_FORWARDING = 1,
    CAPIE_BOOTSTRAP_POISONED = 2
};
/** Acquire snapshot. POISONED must never be interpreted as safely disabled. */
__attribute__((visibility("default"))) uint32_t capie_macos_bootstrap_state(void);
/** Return a process-lifetime original entry, or NULL unless fully installed. */
__attribute__((visibility("default"))) void* capie_macos_bootstrap_trampoline(uint32_t index);
/** Release-publish a process-lifetime ABI-compatible destination; NULL restores forwarding.
 * Caller must finish dynamic token/feature checks before publishing any replacement.
 */
__attribute__((visibility("default"))) bool capie_macos_bootstrap_set_dispatch(uint32_t index, void* destination);
/** Verified loaded main base, or zero unless fully installed. */
__attribute__((visibility("default"))) uintptr_t capie_macos_bootstrap_image_base(void);
/** Actual signed dyld slide; consult state before interpreting zero. */
__attribute__((visibility("default"))) int64_t capie_macos_bootstrap_slide(void);
#ifdef __cplusplus
}
#endif
