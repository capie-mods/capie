/** @file macos_profile_test.cpp
 * @brief Generated profile descriptor checks using synthetic memory, never game execution.
 */
#include "../src/core/macos_profile.generated.hpp"
#include <cstdio>
#include <cstring>
namespace {
namespace profile = capie::core::macos::profile;
namespace pins = capie::core::macos::pins;
namespace image = capie::core::macos::image;
/// @brief Fixed nonzero test relocation, independent from host ASLR.
constexpr std::uintptr_t slide = 0x100000;
/// @brief Count assertions and failures.
unsigned checks = 0, failures = 0;
/** @brief Require a condition. @param value Assertion result. */
void check(bool value) { ++checks; if (!value) ++failures; }
/// @brief Synthetic pointer reader distinguishes a loaded pointer from raw on-disk bytes.
struct PointerMemory {
    bool readable = true; ///< Inject an unreadable slot.
    bool relocated = true; ///< False supplies the unslid disk pointer.
    /** @brief Read generated pointer fields only.
     * @param address Loaded field VA. @param out Result storage. @param size Required eight bytes.
     * @return A matching readable slot was found.
     */
    bool read(std::uintptr_t address, void* out, std::size_t size) const {
        if (!readable || size != 8) return false;
        for (const auto& pin : profile::pointers) {
            if (address == pin.address + slide) {
                const auto value = pin.target + (relocated ? slide : 0);
                std::memcpy(out, &value, sizeof(value));
                return true;
            }
        }
        return false;
    }
};
/// @brief Reads one expected range from synthetic read-only storage.
struct ByteMemory {
    pins::BytesPin pin; ///< The selected immutable data range.
    /** @brief Read a complete synthetic pin.
     * @param address Slid VA. @param out Destination. @param size Requested bytes.
     * @return Exact range is available.
     */
    bool read(std::uintptr_t address, void* out, std::size_t size) const {
        if (address != pin.address + slide || size != pin.expected.size()) return false;
        std::memcpy(out, pin.expected.data(), size);
        return true;
    }
};
}
/** @brief Validate generated descriptors under nonzero slide and differing protection requirements.
 * @return Nonzero on any assertion failure.
 */
int main() {
    static_assert(profile::bytes.size() == 203 && profile::pointers.size() == 15 && profile::relatives.size() == 22);
    image::Image img;
    img.count = 1;
    img.slide = slide;
    img.segments[0] = {0x100000000, 0x4000000, 0, 0x4000000, VM_PROT_READ | VM_PROT_EXECUTE};
    for (const auto& pin : profile::pointers) {
        check(pins::pointer_matches(PointerMemory{}, img, pin));
        check(!pins::pointer_matches(PointerMemory{true, false}, img, pin));
        check(!pins::pointer_matches(PointerMemory{false, true}, img, pin));
    }
    unsigned code = 0, data = 0;
    img.segments[0].initprot = VM_PROT_READ;
    for (const auto& pin : profile::bytes) {
        if (pin.code) { ++code; continue; }
        ++data;
        check(pins::bytes_match(ByteMemory{pin}, img, pin));
        auto wrong = pin;
        wrong.code = true;
        check(!pins::bytes_match(ByteMemory{pin}, img, wrong));
    }
    check(code == 199 && data == 4);
    check(profile::bytes_ids.size() == profile::bytes.size());
    check(profile::pointers_ids.size() == profile::pointers.size());
    check(profile::relatives_ids.size() == profile::relatives.size());
    std::printf("macOS generated profile: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
