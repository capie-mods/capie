/** @file macos_runtime.cpp
 * @brief Late C++ composition behind the minimal first-inserted bootstrap.
 * No code pages are modified here. The bootstrap owns process-lifetime trampolines.
 */
#include "../bootstrap/macos_bootstrap_api.h"
#include "../core/macos_hook_prefixes.h"
#include "../core/macos_gui_probe.hpp"
#include "../core/macos_activation_checks.hpp"
#include "macos_country.hpp"
#include "macos_editor.hpp"
#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <new>
#include <cstdlib>

// A runtime loaded alone must remain inert, not fail dyld's symbol binding.
#pragma weak capie_macos_bootstrap_state
#pragma weak capie_macos_bootstrap_trampoline
#pragma weak capie_macos_bootstrap_set_dispatch
#pragma weak capie_macos_bootstrap_image_base
#pragma weak capie_macos_bootstrap_slide

namespace capie::runtime::macos {
namespace {
using Memory = country::flag::FlagMemory;
using Country = country::macos::Runtime<Memory>;
namespace native = core::macos;
const Memory memory{};
native::image::Image image{};
Country* country_runtime = nullptr; // Process lifetime: residual dispatch must never outlive this object.
editor::Tick tick_original = nullptr;
std::atomic<unsigned> status{0}; // 0 inert, 1 awaiting GUI, 2 active, 3 refused, 4 observe-only.
native::gui_probe::Recorder<32768> gui_recorder{};
core::platform::Lock probe_lock{};
core::platform::Lock report_lock{};
int report_file = -1;
std::size_t exported_events = 0;
std::atomic_flag initializing = ATOMIC_FLAG_INIT;

unsigned index_of(const char* id) noexcept {
    for (unsigned i = 0; i < capie_macos_hook_prefix_count; ++i)
        if (std::strcmp(capie_macos_hook_prefixes[i].id, id) == 0) return i;
    return static_cast<unsigned>(capie_macos_hook_prefix_count);
}
template<class T> T original(const char* id) noexcept {
    return reinterpret_cast<T>(capie_macos_bootstrap_trampoline(index_of(id)));
}
template<class T> T function(core::uptr rva) noexcept {
    core::uptr address = 0;
    if (!image.runtime_virtual_address(native::image::preferred_base + rva, 1, address, true)) return nullptr;
    return reinterpret_cast<T>(address);
}
template<class T> bool publish(const char* id, T callback) noexcept {
    return capie_macos_bootstrap_set_dispatch(index_of(id), reinterpret_cast<void*>(callback));
}
void restore_forwarding() noexcept {
    for (unsigned i = 0; i < capie_macos_hook_prefix_count; ++i)
        static_cast<void>(capie_macos_bootstrap_set_dispatch(i, nullptr));
}
std::uint32_t capabilities() noexcept { return country_runtime ? country_runtime->capabilities() : 0; }

void open_report() noexcept {
    const char* path = std::getenv("CAPIE_RUNTIME_REPORT_DIR");
    if (!path || path[0] != '/') return;
    const int folder = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (folder < 0) return;
    struct stat info{};
    if (fstat(folder, &info) == 0 && info.st_uid == geteuid() && (info.st_mode & 0077) == 0) {
        char name[64]{};
        std::snprintf(name, sizeof(name), "capie-runtime-%ld.jsonl", static_cast<long>(getpid()));
        report_file = openat(folder, name,
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    }
    close(folder);
}
void json_line(const char* data, std::size_t size) noexcept {
    core::platform::lock(&report_lock);
    if (report_file >= 0) {
        while (size != 0) {
            const auto written = ::write(report_file, data, size);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            data += written;
            size -= static_cast<std::size_t>(written);
        }
    }
    core::platform::unlock(&report_lock);
}
void report_state(const char* stage) noexcept {
    char line[256]{};
    const auto bootstrap = capie_macos_bootstrap_state ? capie_macos_bootstrap_state() : 0;
    const int size = std::snprintf(line, sizeof(line),
        "{\"stage\":\"%s\",\"runtime_state\":%u,\"bootstrap_state\":%u,\"thread\":%lu,\"main\":%s}\n",
        stage, status.load(), bootstrap, core::platform::thread_id(), pthread_main_np() ? "true" : "false");
    if (size > 0) json_line(line, core::logging::stored_bytes(size, sizeof(line)));
}
void export_events() noexcept {
    core::platform::lock(&probe_lock);
    native::gui_probe::Event event{};
    while (gui_recorder.read(exported_events, event)) {
        const auto& s = event.snapshot;
        char line[512]{};
        const int size = std::snprintf(line, sizeof(line),
            "{\"call\":%zu,\"kind\":%u,\"phase\":%u,\"thread\":%lu,\"main\":%s,"
            "\"window\":%llu,\"model\":%llu,\"root\":%llu,\"context\":%llu,"
            "\"model_vtable\":%llu,\"root_vtable\":%llu,\"complete\":%s,\"context_matches\":%s}\n",
            event.call, static_cast<unsigned>(event.kind), static_cast<unsigned>(event.phase), event.thread.id,
            event.thread.main ? "true" : "false", static_cast<unsigned long long>(s.window),
            static_cast<unsigned long long>(s.model), static_cast<unsigned long long>(s.root),
            static_cast<unsigned long long>(s.context), static_cast<unsigned long long>(s.model_vtable),
            static_cast<unsigned long long>(s.root_vtable), s.complete ? "true" : "false",
            s.context_matches ? "true" : "false");
        if (size > 0) json_line(line, core::logging::stored_bytes(size, sizeof(line)));
        ++exported_events;
    }
    core::platform::unlock(&probe_lock);
}
void observe_tick(void* window, char force) noexcept {
    if (!editor::window_may_be_own(window)) { tick_original(window, force); return; }
    gui_recorder.tick(memory, reinterpret_cast<core::uptr>(window), force,
                     {core::platform::thread_id(), pthread_main_np() != 0}, tick_original);
    export_events();
}
void observe_dispatch(void* context, void* selector) noexcept {
    const auto forward = original<editor::Dispatch>("window_dispatch");
    void* window = nullptr;
    if (!core::member(context, core::off::context_window, window) || !editor::window_may_be_own(window)) {
        forward(context, selector); return;
    }
    gui_recorder.dispatch(memory, reinterpret_cast<core::uptr>(context), selector,
                         {core::platform::thread_id(), pthread_main_np() != 0}, forward);
    export_events();
}

bool load_image() noexcept {
    std::uint32_t index = 0;
    if (!native::image::executable_index(_dyld_image_count(), _dyld_get_image_header, index)) return false;
    const char* path = _dyld_get_image_name(index);
    if (!path) return false;
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat info{};
    bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 32 && info.st_size <= 1073741824;
    if (valid) {
        const auto size = static_cast<std::size_t>(info.st_size);
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        valid = mapped != MAP_FAILED;
        if (valid) {
            valid = native::image::verify_game({static_cast<const unsigned char*>(mapped), size},
                                               capie_macos_bootstrap_slide(), image);
            munmap(mapped, size);
        }
    }
    close(fd);
    core::uptr base = 0;
    return valid && image.runtime_address(native::image::preferred_base, 1, base) &&
           base == capie_macos_bootstrap_image_base() &&
           base == reinterpret_cast<core::uptr>(_dyld_get_image_header(index));
}

bool bind_country() {
    Country::Configuration config{};
    config.image = &image;
    config.lookup_n = original<country::LookupN>("loc_lookup_n");
    config.lookup_z = original<country::LookupZ>("loc_lookup_z");
    config.exists = original<country::Exists>("loc_exists");
    config.names = {original<country::layer::UpdateNamesFn>("update_names"),
                    original<country::layer::BuildNameKeyFn>("build_name_key"),
                    original<country::layer::SetVariableFn>("set_variable_execute"),
                    original<country::layer::FactionNameFn>("faction_get_name")};
    config.flags.atlas_update = original<country::flag::AtlasUpdateFn>("flag_atlas_update");
    config.flags.load_flag = original<country::flag::LoadFlagFn>("load_country_flag");
    config.flags.candidates = original<country::flag::CandidatesFn>("build_flag_candidates");
    config.portraits.resolve = original<country::portrait::ResolveFn>("portrait_resolve");
    config.portraits.valid = function<country::portrait::ValidFn>(core::rva::portrait_valid);
    config.allocate = function<engine::macos::StringAllocate>(native::activation::allocate_va - native::image::preferred_base);
    config.release = function<void (*)(void*) noexcept>(native::activation::deallocate_va - native::image::preferred_base);
    country_runtime = new Country(memory);
    return country_runtime->configure(config);
}

bool bind_editor() noexcept {
    editor::macos::Configuration config{};
    config.image = capie_macos_bootstrap_image_base();
    config.ui_thread = core::platform::thread_id();
    config.tick = tick_original;
    config.bind = original<editor::Bind>("window_bind");
    config.dispatch = original<editor::Dispatch>("window_dispatch");
    config.constructor = original<editor::Constructor>("effect_ctor");
    config.find_edit = function<engine::FindWidget>(core::rva::find_edit);
    config.find_instant = function<engine::FindWidget>(core::rva::find_instant);
    config.set_text = function<engine::SetText>(core::rva::text_buffer_set);
    config.refresh_edit = function<engine::RefreshEdit>(core::rva::edit_refresh);
    config.caption_request = function<engine::CaptionRequest>(core::rva::request_view_refresh);
    config.focus_switch = function<engine::FocusSwitch>(core::rva::focus_switch);
    config.capabilities = capabilities;
    config.caption_verified = config.focus_verified = true; // Bootstrap's complete static profile plus initialized tokens.
    return editor::macos::configure(config);
}

bool publish_callbacks() noexcept {
    const auto entries = country_runtime->entrypoints();
    if (!entries) return false;
    const auto& e = *entries;
    return publish("loc_lookup_n", e.lookup_n) && publish("loc_lookup_z", e.lookup_z) &&
        publish("loc_exists", e.exists) && publish("update_names", e.update_names) &&
        publish("build_name_key", e.build_name_key) && publish("set_variable_execute", e.set_variable) &&
        publish("faction_get_name", e.faction_name) && publish("flag_atlas_update", e.flag_atlas) &&
        publish("load_country_flag", e.load_flag) && publish("build_flag_candidates", e.flag_candidates) &&
        publish("portrait_resolve", e.portrait_resolve) && publish("window_bind", editor::macos::bind) &&
        publish("window_dispatch", editor::macos::dispatch) && publish("effect_ctor", editor::macos::construct) &&
        publish("window_tick", editor::macos::tick);
}

// The pinned scripted-window Tick supplies the owner thread; no arbitrary initializer thread is admitted.
void awaiting_gui(void* window, char force) noexcept {
    if (status.load(std::memory_order_acquire) != 1 || initializing.test_and_set(std::memory_order_acquire)) {
        tick_original(window, force);
        return;
    }
    void* model = nullptr;
    core::uptr vtable = 0;
    const bool ready = native::activation::tokens_ready(memory, capie_macos_bootstrap_image_base()) &&
        core::member(window, core::off::window_model, model) && core::member(model, 0, vtable) &&
        vtable == capie_macos_bootstrap_image_base() + core::rva::vt_sg_model;
    if (!ready) {
        initializing.clear(std::memory_order_release);
        tick_original(window, force);
        return;
    }
    const bool bound = bind_editor() && publish_callbacks();
    // Experimental owner-thread assumption: first initialized scripted-window Tick owns this GUI.
    // Native calls remain lexically admitted; runtime validation is still required.
    const bool enabled = bound && editor::macos::activate(true) && country_runtime->enable({}, true);
    if (!enabled) {
        editor::macos::deactivate();
        country_runtime->disable();
        restore_forwarding();
        status.store(3, std::memory_order_release);
        tick_original(window, force);
    } else {
        status.store(2, std::memory_order_release);
        report_state("features-active");
        editor::macos::tick(window, force);
    }
    initializing.clear(std::memory_order_release);
}

void initialize() noexcept {
    // shortcut: first-Tick owner/lifetime admission remains experimental until in-game validation.
    const char* experimental = std::getenv("CAPIE_EXPERIMENTAL_RUNTIME");
    const char* mode = std::getenv("CAPIE_RUNTIME_MODE");
    const bool observing = mode && std::strcmp(mode, "observe") == 0;
    if (!observing && (!experimental || std::strcmp(experimental, "1") != 0)) return;
    open_report();
    report_state("runtime-loaded");
    if (!capie_macos_bootstrap_state || !capie_macos_bootstrap_trampoline ||
        !capie_macos_bootstrap_set_dispatch || !capie_macos_bootstrap_image_base || !capie_macos_bootstrap_slide ||
        capie_macos_bootstrap_state() != CAPIE_BOOTSTRAP_FORWARDING) {
        report_state("bootstrap-not-forwarding"); return;
    }
    try {
        if (!load_image() || !native::activation::allocator_matches(memory, capie_macos_bootstrap_image_base())) {
            status.store(3); report_state("image-or-allocator-refused"); return;
        }
        tick_original = original<editor::Tick>("window_tick");
        if (!tick_original) { status.store(3); return; }
        engine::image_base = capie_macos_bootstrap_image_base();
        if (observing) {
            if (publish("window_tick", observe_tick) && publish("window_dispatch", observe_dispatch)) status.store(4);
            else { restore_forwarding(); status.store(3); }
            report_state("observe-only");
            return;
        }
        if (!bind_country()) { status.store(3); report_state("country-config-refused"); return; }
        status.store(1, std::memory_order_release);
        report_state("awaiting-gui");
        if (!publish("window_tick", awaiting_gui)) status.store(3);
    } catch (...) { status.store(3); }
}
// Ordered after the shared C++ state defined above; an attribute constructor could run before it.
struct Startup { Startup() noexcept { initialize(); } } startup;
} // namespace
} // namespace capie::runtime::macos
extern "C" __attribute__((visibility("default"))) unsigned capie_macos_runtime_state() noexcept {
    return capie::runtime::macos::status.load(std::memory_order_acquire);
}
