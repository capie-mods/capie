/**
 * @file probe-macos-live.cpp
 * @brief Explicit read-only menu probe. Never attaches a debugger, suspends, or writes.
 *
 * SDK libsystem_kernel.tbd exports task_read_for_pid; mach_vm_read_overwrite accepts
 * vm_map_read_t. The read-port syscall is not declared by the public SDK headers.
 * Only this read port is requested; failure is final, with no task_for_pid fallback.
 * This probe intentionally supports only the observed unslid image at 0x100000000.
 * File pins come from build_pin_evidence.py; TSV fields: id, VA hex, expected hex.
 */
#include "../plugin/capie/src/core/macos_image.hpp"
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/proc_info.h>
#include <array>
#include <climits>
#include <cerrno>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

extern "C" kern_return_t task_read_for_pid(mach_port_name_t, int, mach_port_name_t*);
namespace {
namespace image = capie::core::macos::image;
constexpr std::uintptr_t base = image::preferred_base;
struct Identity {
    proc_bsdinfo info{};
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};
};
bool identity(int pid, Identity& out) {
    return proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &out.info, sizeof(out.info)) == sizeof(out.info) &&
           proc_pidpath(pid, out.path.data(), out.path.size()) > 0;
}
bool same(const Identity& first, const Identity& second) {
    return first.info.pbi_start_tvsec == second.info.pbi_start_tvsec &&
           first.info.pbi_start_tvusec == second.info.pbi_start_tvusec &&
           std::strcmp(first.path.data(), second.path.data()) == 0;
}
struct ReadPort {
    mach_port_t value = MACH_PORT_NULL;
    ~ReadPort() { if (value != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), value); }
    bool read(std::uintptr_t address, void* out, std::size_t size) const {
        if (size == 0 || size > 4096 || address > UINTPTR_MAX - size) return false;
        mach_vm_size_t copied = 0;
        const auto status = mach_vm_read_overwrite(value, address, size,
            reinterpret_cast<mach_vm_address_t>(out), &copied);
        return status == KERN_SUCCESS && copied == size;
    }
};
struct Pin { std::string id; std::uintptr_t va{}; std::vector<unsigned char> bytes; };
std::vector<Pin> load_pins(const char* path, const image::Image& img,
                           const std::vector<unsigned char>& file) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("profile open");
    std::vector<Pin> pins;
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream row(line);
        Pin pin; std::string address, encoded, extra;
        if (!(row >> pin.id >> address >> encoded) || row >> extra ||
            encoded.empty() || encoded.size() % 2 || encoded.size() > 8192 || pins.size() >= 300)
            throw std::runtime_error("profile format");
        pin.va = std::stoull(address, nullptr, 16);
        for (std::size_t i=0; i<encoded.size(); i+=2) {
            const auto pair = encoded.substr(i,2);
            std::size_t consumed = 0;
            const auto value = std::stoul(pair, &consumed,16);
            if (consumed != 2 || value > 255) throw std::runtime_error("profile hex");
            pin.bytes.push_back(static_cast<unsigned char>(value));
        }
        std::size_t offset = 0;
        if (!img.file_range(pin.va,pin.bytes.size(),offset) ||
            std::memcmp(file.data()+offset,pin.bytes.data(),pin.bytes.size()) != 0)
            throw std::runtime_error("profile file mismatch");
        pins.push_back(std::move(pin));
    }
    if (pins.empty()) throw std::runtime_error("empty profile");
    return pins;
}
int run(int pid, const char* executable, const char* profile) {
    Identity before, after;
    std::array<char, PATH_MAX> canonical{};
    if (!identity(pid,before) || !realpath(executable,canonical.data()) ||
        std::strcmp(before.path.data(),canonical.data()) != 0) {
        std::puts("FAIL identity_path"); return 2;
    }
    std::ifstream stream(executable,std::ios::binary|std::ios::ate);
    const auto length=stream.tellg();
    if (!stream || length<=0 || length>1024*1024*1024) return 2;
    std::vector<unsigned char> file(static_cast<std::size_t>(length));
    stream.seekg(0);stream.read(reinterpret_cast<char*>(file.data()),file.size());
    image::Image img;
    if (!stream || !image::verify_game(file,0,img) || !identity(pid,after) || !same(before,after)) {
        std::puts("FAIL exact_file_or_pid_identity");return 2;
    }
    const auto pins=load_pins(profile,img,file);
    std::printf("PASS pid_path_starttime_sha256 pid=%d sha256=%s\n",pid,image::game_sha256);
    ReadPort task;
    errno = 0;
    const auto access=task_read_for_pid(mach_task_self(),pid,&task.value);
    const int access_errno = errno;
    if (access!=KERN_SUCCESS || task.value==MACH_PORT_NULL) {
        std::printf("DENIED task_read_for_pid status=%d errno=%d; no fallback\n",access,access_errno);
        return 3;
    }
    std::array<unsigned char,4096> buffer{};
    if (!task.read(base,buffer.data(),sizeof(mach_header_64)) ||
        std::memcmp(buffer.data(),file.data(),sizeof(mach_header_64)) != 0) {
        std::puts("FAIL loaded_header_at_observed_base");return 4;
    }
    mach_header_64 header{};std::memcpy(&header,file.data(),sizeof(header));
    const auto extent=sizeof(header)+header.sizeofcmds;
    for (std::size_t offset=0;offset<extent;) {
        const auto size=std::min(buffer.size(),extent-offset);
        if (!task.read(base+offset,buffer.data(),size) ||
            std::memcmp(buffer.data(),file.data()+offset,size)!=0) {
            std::puts("FAIL loaded_header_commands");return 4;
        }
        offset+=size;
    }
    std::puts("PASS loaded_x86_64_header_and_commands base=0x100000000 slide=0");
    unsigned failures=0;
    for (const auto& pin:pins) {
        const bool ok=task.read(pin.va,buffer.data(),pin.bytes.size()) &&
            std::memcmp(buffer.data(),pin.bytes.data(),pin.bytes.size())==0;
        if (!ok) {++failures;std::printf("FAIL static_pin %s\n",pin.id.c_str());}
    }
    const std::array<std::pair<std::uintptr_t,std::uint32_t>,2> tokens{{{0x1035af598,0x3ab2},{0x1035ae4b8,0x3a70}}};
    for (const auto& [address,expected]:tokens) {
        std::uintptr_t checked=0;std::uint32_t value=0;
        const bool ok=img.runtime_virtual_address(address,sizeof(value),checked) &&
            task.read(checked,&value,sizeof(value));
        if (!ok || value!=expected) ++failures;
        if(ok) std::printf("%s runtime_token va=0x%llx value=0x%x expected=0x%x\n",
            value==expected?"PASS":"FAIL",static_cast<unsigned long long>(address),value,expected);
        else std::puts("FAIL runtime_token_unreadable");
    }
    if (!identity(pid,after) || !same(before,after)) {std::puts("FAIL final_pid_identity");return 4;}
    std::printf("SUMMARY static_ranges=%zu runtime_tokens=2 failures=%u; read_only_no_hooks_no_ABI_claim\n",pins.size(),failures);
    return failures?4:0;
}
}
int main(int argc,char** argv) {
    if(argc!=4){std::puts("usage: probe-macos-live PID EXACT_GAME_PATH VERIFIED_PIN_TSV");return 2;}
    try {
        std::size_t used=0;const auto pid=std::stoul(argv[1],&used);
        if (used!=std::strlen(argv[1]) || pid==0 || pid>INT_MAX) return 2;
        return run(static_cast<int>(pid),argv[2],argv[3]);
    } catch(...) {std::puts("FAIL input_or_file_validation; no activation");return 2;}
}
