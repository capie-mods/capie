/** @file macos_country.hpp
 * @brief Explicit macOS country composition; no constructors, exports or patch activation.
 *
 * Caller must verify exact image/pins, every SysV callback ABI and allocator ownership.
 * Configure/enable/disable/destruction require caller-established quiescence. The object,
 * memory policy and trampolines must outlive every installed/residual callback. Disable
 * retains originals and caches; it does not uninstall patches or make unloading safe.
 * Only a future complete editor+country composition may attest full_contract_ready.
 */
#pragma once
#include "../core/macos_image.hpp"
#include "../features/country/country_names.hpp"
#include "../features/country/flag_layer_hooks.hpp"
#include "../features/country/name_layer_hooks.hpp"
#include "../features/country/portrait_layer_hooks.hpp"

#include <optional>

namespace capie::country::macos {
/// @brief Shared localisation globals permit only one explicitly configured country owner.
inline const void* owner = nullptr;

/** @brief Process-lifetime country wiring over a bounded read/write memory policy. */
template <class M>
class Runtime {
public:
    using Names = layer::NameLayer<M>;             ///< Shared name/faction implementation.
    using Flags = flag::FlagLayer<M>;              ///< Shared flag implementation.
    using Portraits = portrait::PortraitLayer<M>;  ///< Shared portrait implementation.

    /// @brief Caller-verified callbacks; non-null validation alone is not ABI/pin proof.
    struct Configuration {
        const core::macos::image::Image* image{};   ///< Exact verified image/profile, consumed during configure.
        LookupN lookup_n{};                         ///< Verified localisation trampoline.
        LookupZ lookup_z{};                         ///< Verified localisation trampoline.
        Exists exists{};                            ///< Verified localisation trampoline.
        typename Names::Originals names{};          ///< Four original SysV functions.
        typename Flags::Originals flags{};          ///< Three original SysV functions.
        typename Portraits::Originals portraits{};  ///< Resolve trampoline and validity function.
        engine::macos::StringAllocate allocate{};   ///< Verified native allocator.
        void (*release)(void*) noexcept {};         ///< Matching verified free.
        layer::LogFn log{};                         ///< Optional scalar-only logging sink.
    };
    /// @brief Requested layers; flags and portraits depend on names.
    struct Layers {
        bool names = true;
        bool flags = true;
        bool portraits = true;
    };
    /// @brief Function addresses usable only after successful configure and while this owner lives.
    struct Entrypoints {
        LookupN lookup_n;
        LookupZ lookup_z;
        Exists exists;
        layer::UpdateNamesFn update_names;
        layer::BuildNameKeyFn build_name_key;
        layer::SetVariableFn set_variable;
        layer::FactionNameFn faction_name;  // SysV hidden output first.
        flag::AtlasUpdateFn flag_atlas;     // Native void return contract.
        flag::LoadFlagFn load_flag;
        flag::CandidatesFn flag_candidates;
        portrait::ResolveFn portrait_resolve;
    };

    /// @brief Construct inactive state explicitly; memory must outlive this object.
    explicit Runtime(const M& memory) : memory_(memory), names_(memory), flags_(memory), portraits_(memory) {}
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    /// @brief Only legal after all patch callbacks are removed and callers quiescent.
    ~Runtime() {
        if (owner == this) {
            disable();
            owner = nullptr;
            current_ = nullptr;
        }
    }

    /** @brief Bind all forwarding targets without enabling any overrides.
     * @return False without changing state if inputs are incomplete or an owner already exists.
     */
    bool configure(const Configuration& config) noexcept {
        if (configured_ || owner || !config.image || config.image->count == 0 ||
            config.image->count > config.image->segments.size() || !config.lookup_n || !config.lookup_z ||
            !config.exists || !config.names.update || !config.names.build || !config.names.set_variable ||
            !config.names.faction_name || !config.flags.atlas_update || !config.flags.load_flag ||
            !config.flags.candidates || !config.portraits.resolve || !config.portraits.valid || !config.allocate ||
            !config.release)
            return false;
        std::uintptr_t base = 0;
        if (!config.image->runtime_address(core::macos::image::preferred_base, 1, base))
            return false;
        config_ = config;
        config_.image = nullptr;  // No retained borrowed Image lifetime is required.
        image_base_ = base;
        names_.originals() = config.names;
        flags_.originals() = config.flags;
        flags_.originals().allocate = config.allocate;
        flags_.originals().deallocate = config.release;
        portraits_.originals() = config.portraits;
        portraits_.originals().allocate = config.allocate;
        portraits_.originals().deallocate = config.release;
        country::active.store(false);
        country::original_n = config.lookup_n;
        country::original_z = config.lookup_z;
        country::original_exists = config.exists;
        owner = this;
        current_ = this;
        configured_ = true;
        return true;
    }

    /** @brief Enable only after caller attests the complete editor+country script contract.
     * @param selected Requested, separately verified layers.
     * @param full_contract_ready False by default; never inferred from callbacks or pin bytes.
     * @return False and inactive for absent ownership, incomplete contract or invalid dependencies.
     */
    bool enable(Layers selected = {}, bool full_contract_ready = false) noexcept {
        if (!configured_ || owner != this)
            return false;
        disable();
        if (!full_contract_ready || ((!selected.names) && (selected.flags || selected.portraits)))
            return false;
        if (selected.names)
            names_.activate(image_base_, config_.log, config_.allocate, config_.release);
        if (selected.flags)
            flags_.activate(image_base_, config_.log);
        if (selected.portraits)
            portraits_.activate(image_base_, config_.log);
        country::active.store(true);
        return true;
    }
    /// @brief Stop overrides; all bound originals remain available for residual forwarding.
    void disable() noexcept {
        if (owner != this)
            return;
        country::active.store(false);
        names_.deactivate();
        flags_.deactivate();
        portraits_.deactivate();
    }
    /// @brief Internal capability query only; no exported or advertised API without full admission.
    [[nodiscard]] std::uint32_t capabilities() const noexcept {
        return owner == this && country::active.load()
                   ? capabilities_of(names_.enabled(), flags_.enabled(), portraits_.enabled())
                   : 0;
    }
    /// @brief Only publish callback addresses after every original was bound successfully.
    [[nodiscard]] std::optional<Entrypoints> entrypoints() const noexcept {
        if (!configured_ || owner != this)
            return std::nullopt;
        return Entrypoints{country::lookup_n, country::lookup_z, country::exists, update_hook,
                           build_hook,        set_hook,          faction_hook,    atlas_hook,
                           flag_hook,         candidates_hook,   portrait_hook};
    }

private:
    /// @brief Run shared update logic in Windows composition order, preserving original return.
    static std::uint64_t update_hook(core::uptr country) noexcept {
        const auto result = current_->names_.update_names(country);
        static_cast<void>(current_->flags_.update(country));
        static_cast<void>(current_->portraits_.update(country));
        return result;
    }
    /// @brief Forward all eight native builder arguments to the shared feature.
    static void* build_hook(void* out, const void* tag, void* a3, void* a4, void* a5, void* a6, void* a7,
                            char a8) noexcept {
        return current_->names_.build_name_key(out, tag, a3, a4, a5, a6, a7, a8);
    }
    /// @brief Refresh flag/portrait caches after the shared set-variable original/commit logic.
    static void set_hook(core::uptr effect, core::uptr context) noexcept {
        current_->names_.set_variable(effect, context);
        core::uptr name = 0;
        if (effect == 0 || !core::add_offset(effect, core::macos::off::set_variable_name, name))
            return;
        if (current_->flags_.enabled() &&
            engine::macos::string_equals(current_->memory_, name, std::string_view(flag::commit_variable)))
            static_cast<void>(current_->flags_.refresh_all());
        if (current_->portraits_.enabled() &&
            engine::macos::string_equals(current_->memory_, name, std::string_view(portrait::commit_variable)))
            static_cast<void>(current_->portraits_.refresh_all());
    }
    /// @brief SysV sret output precedes the faction receiver.
    static void* faction_hook(void* out, core::uptr faction) noexcept {
        return current_->names_.faction_name_sysv(out, faction);
    }
    /// @brief macOS atlas update returns void, unlike the Windows ABI.
    static void atlas_hook(core::uptr atlas, core::uptr gfx) noexcept { current_->flags_.atlas_update(atlas, gfx); }
    /// @brief Preserve the eight native flag loader arguments and status.
    static int flag_hook(core::uptr atlas, core::uptr country, void* out, int a4, int a5, int a6, int a7,
                         void* a8) noexcept {
        return current_->flags_.load_flag(atlas, country, out, a4, a5, a6, a7, a8);
    }
    /// @brief Dispatch native flag-candidate builder through the shared layer.
    static void* candidates_hook(void* out, const void* tag, const void* cosmetic, void* group) noexcept {
        return current_->flags_.candidates(out, tag, cosmetic, group);
    }
    /// @brief Dispatch native portrait resolution, preserving hidden output argument order.
    static void* portrait_hook(void* out, void* map, std::int32_t type, std::int32_t size, core::uptr character,
                               const void* fallback) noexcept {
        return current_->portraits_.resolve_sysv(out, map, type, size, character, fallback);
    }
    inline static Runtime* current_ = nullptr;  ///< Installed callbacks require this owner's lifetime.
    const M& memory_;                           ///< Caller-owned bounded policy.
    Names names_;
    Flags flags_;
    Portraits portraits_;      ///< Shared features; algorithms are not copied.
    Configuration config_{};   ///< Retained originals and allocator callbacks.
    core::uptr image_base_{};  ///< Validated relocated base, not a Windows RVA assumption.
    bool configured_ = false;  ///< Binding is single-shot and initially absent.
};
}  // namespace capie::country::macos
