/* Owned refusal fixture: claims forwarding but supplies no valid game identity. */
#include "../../src/bootstrap/macos_bootstrap_api.h"
uint32_t capie_macos_bootstrap_state(void) { return CAPIE_BOOTSTRAP_FORWARDING; }
void* capie_macos_bootstrap_trampoline(uint32_t index) { (void)index; return 0; }
bool capie_macos_bootstrap_set_dispatch(uint32_t index, void* destination) {
    (void)index; (void)destination; return false;
}
uintptr_t capie_macos_bootstrap_image_base(void) { return 0; }
int64_t capie_macos_bootstrap_slide(void) { return 0; }
