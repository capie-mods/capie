/** Owned fixture only: substitutes all 15 target addresses, never accepts the game. */
#include "../../src/bootstrap/macos_bootstrap.cpp"
#include "fixture_identity.h"
__attribute__((constructor)) static void fixture_init() {
    if (const char* unknown = std::getenv("CAPIE_FIXTURE_UNKNOWN_DEPENDENCY"))
        if (!dlopen(unknown, RTLD_NOW | RTLD_LOCAL)) return;
    if (!policy_matches()) return;
    uint32_t index = 0;
    if (!image::executable_index(_dyld_image_count(), _dyld_get_image_header, index)) return;
    File file(_dyld_get_image_name(index));
    if (!file.bytes || !image::sha256_matches({file.bytes, file.size}, fixture_hash)) return;
    image::Image parsed;
    verified_slide = _dyld_get_image_vmaddr_slide(index);
    if (!image::parse({file.bytes, file.size}, verified_slide, parsed)) return;
    if (!parsed.runtime_address(image::preferred_base, 32, verified_base) ||
        verified_base != reinterpret_cast<uintptr_t>(_dyld_get_image_header(index))) return;
    install_all(parsed);
}
