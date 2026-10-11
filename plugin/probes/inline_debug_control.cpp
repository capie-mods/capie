// Development-only controller: a fixed scalar request to an already-loaded DLL.
// No remote LoadLibrary, injection, arbitrary export, retry, kill or remote buffer.
#include "../src/image_guard.hpp"
#include "inline_debug_manifest.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr wchar_t fixed_name[]=L"rename_anything_inline_editor_debug_bridge.dll";
constexpr char fixed_export[]="RNDebugRequest";
constexpr DWORD request_wait_ms=30000;
constexpr bool same_literal(const wchar_t* left,const wchar_t* right) noexcept {
    for(std::size_t i=0;;++i) { if(left[i]!=right[i]) return false; if(!left[i]) return true; }
}
static_assert(same_literal(rn_debug_dll_name,fixed_name));
static_assert(sizeof(void*)==8 && sizeof(rn_debug_dll_sha256)/sizeof(wchar_t)==65);
static_assert(rn_debug_dll_sha256[64]==L'\0');

struct Failure : std::runtime_error {
    int code;
    Failure(int value,const char* message):std::runtime_error(message),code(value) {}
};
class Handle {
    HANDLE value_{};
public:
    Handle() noexcept=default;
    explicit Handle(HANDLE value) noexcept:value_(value) {}
    ~Handle() { if(*this) CloseHandle(value_); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value_(std::exchange(other.value_,nullptr)) {}
    Handle& operator=(Handle&&)=delete;
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_!=INVALID_HANDLE_VALUE; }
};
struct Mapping {
    HMODULE module{};
    ~Mapping() { if(module) FreeLibrary(module); } // Local mapping only.
    Mapping()=default;
    Mapping(const Mapping&)=delete;
    Mapping& operator=(const Mapping&)=delete;
};
struct Identity {
    DWORD volume{},high{},low{};
    bool operator==(const Identity&) const=default;
};
Identity identity(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
        throw Failure(20,"Cannot identify pinned file.");
    return {info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow};
}
std::wstring final_path(HANDLE file) {
    const DWORD required=GetFinalPathNameByHandleW(file,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!required || required>=32767) throw Failure(20,"Cannot resolve pinned file path.");
    std::vector<wchar_t> value(static_cast<std::size_t>(required)+1);
    const DWORD written=GetFinalPathNameByHandleW(file,value.data(),static_cast<DWORD>(value.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!written || written>=value.size()) throw Failure(20,"Pinned file path changed.");
    return {value.data(),written};
}
std::wstring same_handle_sha256(HANDLE file) {
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)) throw Failure(20,"Cannot seek pinned file.");
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    const auto cleanup=[&]() noexcept {
        if(hash) BCryptDestroyHash(hash);
        if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    };
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
       BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) {
        cleanup(); throw Failure(20,"SHA256 is unavailable.");
    }
    std::array<unsigned char,65536> buffer{}; std::array<unsigned char,32> digest{};
    bool good=true;
    for(;;) {
        DWORD bytes{};
        if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr)) { good=false; break; }
        if(!bytes) break;
        if(BCryptHashData(hash,buffer.data(),bytes,0)<0) { good=false; break; }
    }
    good=good && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    cleanup();
    if(!good) throw Failure(20,"Cannot hash pinned file.");
    constexpr wchar_t hex[]=L"0123456789abcdef";
    std::wstring result; result.reserve(64);
    for(const auto value:digest) { result.push_back(hex[value>>4]); result.push_back(hex[value&15]); }
    return result;
}
struct Pin {
    Handle handle;
    std::wstring path;
    Identity id;
    std::wstring digest;
    explicit Pin(const std::wstring& input):handle(CreateFileW(input.c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)) {
        if(!handle) throw Failure(20,"Missing, locked or unreadable fixed file.");
        path=final_path(handle.get()); id=identity(handle.get()); digest=same_handle_sha256(handle.get());
    }
    bool matches(const std::wstring& input) const noexcept {
        try {
            Handle other(CreateFileW(input.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
            return other && identity(other.get())==id && final_path(other.get())==path;
        } catch(...) { return false; }
    }
};
bool read_exact(HANDLE process,std::uintptr_t address,void* destination,std::size_t size) noexcept {
    if(!address || size>(std::numeric_limits<std::uintptr_t>::max)()-address) return false;
    SIZE_T actual{};
    return ReadProcessMemory(process,reinterpret_cast<const void*>(address),destination,size,&actual)!=0 && actual==size;
}
struct Image {
    HANDLE process{};
    std::uintptr_t base{};
    DWORD size{},timestamp{},export_rva{},export_size{};
    bool read(DWORD rva,void* destination,std::size_t bytes) const noexcept {
        return rva<=size && bytes<=size-rva && base<=(std::numeric_limits<std::uintptr_t>::max)()-rva &&
            read_exact(process,base+rva,destination,bytes);
    }
};
Image inspect_image(HANDLE process,std::uintptr_t base,DWORD expected_size) {
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if(!read_exact(process,base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
       dos.e_lfanew<=0 || dos.e_lfanew>65536 ||
       base>(std::numeric_limits<std::uintptr_t>::max)()-static_cast<std::uintptr_t>(dos.e_lfanew) ||
       !read_exact(process,base+static_cast<std::uintptr_t>(dos.e_lfanew),&nt,sizeof(nt)) ||
       nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
       !(nt.FileHeader.Characteristics&IMAGE_FILE_DLL) ||
       nt.FileHeader.NumberOfSections==0 || nt.FileHeader.NumberOfSections>96 ||
       nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
       nt.OptionalHeader.SizeOfImage==0 || nt.OptionalHeader.SizeOfImage>536870912 ||
       (expected_size && nt.OptionalHeader.SizeOfImage!=expected_size) ||
       nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXPORT)
        throw Failure(22,"Invalid local or remote fixed DLL PE headers.");
    const auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!directory.VirtualAddress || directory.Size<sizeof(IMAGE_EXPORT_DIRECTORY) ||
       directory.VirtualAddress>nt.OptionalHeader.SizeOfImage ||
       directory.Size>nt.OptionalHeader.SizeOfImage-directory.VirtualAddress)
        throw Failure(22,"Invalid fixed DLL export directory.");
    return {process,base,nt.OptionalHeader.SizeOfImage,nt.FileHeader.TimeDateStamp,directory.VirtualAddress,directory.Size};
}
DWORD fixed_export_rva(const Image& image) {
    IMAGE_EXPORT_DIRECTORY directory{};
    if(!image.read(image.export_rva,&directory,sizeof(directory)) ||
       directory.NumberOfNames==0 || directory.NumberOfNames>4096 ||
       directory.NumberOfFunctions==0 || directory.NumberOfFunctions>4096)
        throw Failure(22,"Invalid fixed export table bounds.");
    DWORD result{}; unsigned matches{};
    for(DWORD i=0;i<directory.NumberOfNames;++i) {
        DWORD name_rva{};
        const std::uint64_t name_entry=static_cast<std::uint64_t>(directory.AddressOfNames)+sizeof(DWORD)*i;
        if(name_entry>MAXDWORD || !image.read(static_cast<DWORD>(name_entry),&name_rva,sizeof(name_rva)))
            throw Failure(22,"Unreadable export name table.");
        if(name_rva>=image.size) throw Failure(22,"Invalid export name RVA.");
        std::array<char,128> name{};
        const auto available=(std::min)(name.size(),static_cast<std::size_t>(image.size-name_rva));
        if(!image.read(name_rva,name.data(),available) || std::memchr(name.data(),0,available)==nullptr)
            throw Failure(22,"Unreadable bounded export name.");
        if(std::strcmp(name.data(),fixed_export)!=0) continue;
        WORD ordinal{}; DWORD rva{};
        const std::uint64_t ordinal_entry=static_cast<std::uint64_t>(directory.AddressOfNameOrdinals)+sizeof(WORD)*i;
        if(ordinal_entry>MAXDWORD || !image.read(static_cast<DWORD>(ordinal_entry),&ordinal,sizeof(ordinal)) ||
           ordinal>=directory.NumberOfFunctions) throw Failure(22,"Invalid fixed export ordinal.");
        const std::uint64_t function_entry=static_cast<std::uint64_t>(directory.AddressOfFunctions)+sizeof(DWORD)*ordinal;
        if(function_entry>MAXDWORD || !image.read(static_cast<DWORD>(function_entry),&rva,sizeof(rva)) ||
           !rva || rva>=image.size || image.size-rva<32 ||
           (rva>=image.export_rva && rva-image.export_rva<image.export_size))
            throw Failure(22,"Fixed export is forwarded or out of bounds.");
        result=rva; ++matches;
    }
    if(matches!=1) throw Failure(22,"Exactly one direct RNDebugRequest export is required.");
    return result;
}
bool executable_image_page(HANDLE process,std::uintptr_t address,std::uintptr_t base) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQueryEx(process,reinterpret_cast<const void*>(address),&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || info.Type!=MEM_IMAGE || reinterpret_cast<std::uintptr_t>(info.AllocationBase)!=base ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD access=info.Protect&0xff;
    return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
std::wstring module_path(HMODULE module) {
    std::vector<wchar_t> path(32768);
    const DWORD size=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!size || size>=path.size()) throw Failure(20,"Cannot identify controller or local DLL path.");
    return {path.data(),size};
}
struct Loaded { std::uintptr_t base{}; DWORD size{}; };
Loaded loaded_fixed_module(DWORD pid,const Pin& dll) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(!snapshot) throw Failure(21,"Cannot enumerate target modules.");
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(!Module32FirstW(snapshot.get(),&entry)) throw Failure(21,"Cannot read target module list.");
    Loaded result; unsigned matches{};
    do {
        if(_wcsicmp(entry.szModule,fixed_name)!=0) continue;
        if(!dll.matches(entry.szExePath)) throw Failure(21,"Loaded debug DLL is not the pinned absolute sibling file.");
        result={reinterpret_cast<std::uintptr_t>(entry.modBaseAddr),entry.modBaseSize}; ++matches;
    } while(Module32NextW(snapshot.get(),&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES || matches!=1)
        throw Failure(21,"Exactly one already-loaded fixed sibling debug DLL is required; nothing is injected.");
    return result;
}
struct Target {
    Handle process;
    DWORD pid;
    FILETIME created{};
    std::wstring image_path;
    // Documented CreateRemoteThread access set; no VM-write/allocation API is used.
    explicit Target(DWORD requested):process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|
        PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_CREATE_THREAD|SYNCHRONIZE,FALSE,requested)),pid(requested) {
        if(!process) throw Failure(3,"Cannot open target process without elevation.");
        FILETIME end{},kernel{},user{};
        if(!GetProcessTimes(process.get(),&created,&end,&kernel,&user)) throw Failure(10,"Cannot identify target creation time.");
        std::vector<wchar_t> path(32768); DWORD size=static_cast<DWORD>(path.size());
        if(!QueryFullProcessImageNameW(process.get(),0,path.data(),&size)) throw Failure(10,"Cannot identify target image.");
        image_path.assign(path.data(),size);
        if(_wcsicmp(std::filesystem::path(image_path).filename().c_str(),L"hoi4.exe")!=0)
            throw Failure(10,"Target must be the exact pinned hoi4.exe.");
    }
    bool current(const Pin& game) const noexcept {
        try {
            FILETIME start{},end{},kernel{},user{};
            std::vector<wchar_t> path(32768); DWORD size=static_cast<DWORD>(path.size());
            return WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT && GetProcessId(process.get())==pid &&
                GetProcessTimes(process.get(),&start,&end,&kernel,&user) && CompareFileTime(&start,&created)==0 &&
                QueryFullProcessImageNameW(process.get(),0,path.data(),&size) && game.matches(std::wstring(path.data(),size)) &&
                game.matches(game.path);
        } catch(...) { return false; }
    }
};
std::uintptr_t validate_remote(Target& target,const Pin& dll,const Image& local,DWORD local_rva) {
    if(!dll.matches(dll.path)) throw Failure(21,"Pinned DLL disk identity changed.");
    const auto loaded=loaded_fixed_module(target.pid,dll);
    const auto remote=inspect_image(target.process.get(),loaded.base,loaded.size);
    if(remote.timestamp!=local.timestamp || remote.size!=local.size ||
       remote.export_rva!=local.export_rva || remote.export_size!=local.export_size ||
       fixed_export_rva(remote)!=local_rva) throw Failure(22,"Remote PE/export identity differs from pinned DLL.");
    std::array<unsigned char,32> expected{},actual{};
    if(!local.read(local_rva,expected.data(),expected.size()) || !remote.read(local_rva,actual.data(),actual.size()) ||
       expected!=actual || !executable_image_page(target.process.get(),remote.base+local_rva,remote.base))
        throw Failure(22,"Remote RNDebugRequest first32 bytes or executable image page mismatch.");
    return remote.base+local_rva;
}
void quiet_guardian(HANDLE thread,HANDLE process) noexcept {
    const HANDLE handles[]={thread,process};
    for(;;) {
        const DWORD result=WaitForMultipleObjects(2,handles,FALSE,INFINITE);
        if(result==WAIT_OBJECT_0 || result==WAIT_OBJECT_0+1) return;
        Sleep(1000); // Never release pins on an uncertain wait failure.
    }
}
int request(DWORD pid,DWORD opcode) {
    for(std::size_t i=0;i<64;++i) {
        const wchar_t ch=rn_debug_dll_sha256[i];
        if(!((ch>=L'0' && ch<=L'9') || (ch>=L'a' && ch<=L'f') || (ch>=L'A' && ch<=L'F')))
            throw Failure(20,"Invalid compiled fixed DLL hash manifest.");
    }
    const auto sibling=std::filesystem::path(module_path(nullptr)).parent_path()/fixed_name;
    Pin dll(sibling.wstring());
    if(_wcsicmp(dll.digest.c_str(),rn_debug_dll_sha256)!=0) throw Failure(20,"Fixed sibling debug DLL SHA256 mismatch.");
    Target target(pid);
    Pin game(target.image_path);
    std::wstring expected_game;
    for(const char ch:rn::exe_sha256) if(ch) expected_game.push_back(static_cast<unsigned char>(ch));
    if(game.digest!=expected_game || !target.current(game)) throw Failure(10,"Target image/hash/creation identity mismatch.");
    // Data/code mapping only: no DLL initialization, imports or remote load.
    Mapping mapping;
    mapping.module=LoadLibraryExW(dll.path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!mapping.module || !dll.matches(module_path(mapping.module))) throw Failure(22,"Cannot map exact pinned local DLL.");
    const auto local=inspect_image(GetCurrentProcess(),reinterpret_cast<std::uintptr_t>(mapping.module),0);
    const DWORD local_rva=fixed_export_rva(local);
    const auto entry=GetProcAddress(mapping.module,fixed_export);
    if(!entry || reinterpret_cast<std::uintptr_t>(entry)!=local.base+local_rva ||
       !executable_image_page(GetCurrentProcess(),local.base+local_rva,local.base))
        throw Failure(22,"Local direct export RVA/type mismatch.");
    const auto remote_entry=validate_remote(target,dll,local,local_rva);
    if(!target.current(game)) throw Failure(10,"Target changed before request; nothing sent.");
    // Exactly one fixed DWORD WINAPI(void*) call. The argument is scalar, not a
    // borrowed pointer or remote allocation. A timeout cannot trigger a retry.
    Handle thread(CreateRemoteThread(target.process.get(),nullptr,0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_entry),reinterpret_cast<void*>(static_cast<std::uintptr_t>(opcode)),0,nullptr));
    if(!thread) throw Failure(23,"Fixed request thread could not be created; nothing retried.");
    const DWORD wait=WaitForSingleObject(thread.get(),request_wait_ms);
    DWORD code{};
    if(wait==WAIT_OBJECT_0 && GetExitCodeThread(thread.get(),&code)) {
        if(code==0) std::puts("QUEUED: scalar request accepted; snapshot/dump completion is not confirmed. Read the native diagnostic output separately.");
        else if(code==41) std::fputs("Refused: debug bridge inactive (41).\n",stderr);
        else if(code==42) std::fputs("Refused: invalid opcode (42).\n",stderr);
        else if(code==43) std::fputs("Refused: request serial exhausted (43); no automatic retry.\n",stderr);
        else { std::fprintf(stderr,"Unexpected fixed export result %lu; no retry.\n",code); return 24; }
        return static_cast<int>(code);
    }
    std::fputs("UNCERTAIN: request did not complete/read back within30s. No retry/kill/unload. Holding file/process pins quietly until this thread completes or the captured target exits.\n",stderr);
    std::fflush(stderr);
    quiet_guardian(thread.get(),target.process.get());
    return 24;
}
DWORD parse_pid(const wchar_t* input) {
    if(!input || !*input) throw Failure(2,"PID must be a positive decimal DWORD.");
    std::uint64_t value{};
    for(const wchar_t* ch=input;*ch;++ch) {
        if(*ch<L'0' || *ch>L'9') throw Failure(2,"PID must contain decimal digits only.");
        value=value*10+static_cast<unsigned>(*ch-L'0');
        if(value>MAXDWORD) throw Failure(2,"PID exceeds DWORD range.");
    }
    if(!value) throw Failure(2,"PID must be nonzero.");
    return static_cast<DWORD>(value);
}
DWORD parse_opcode(const wchar_t* input) {
    if(std::wcscmp(input,L"snapshot")==0) return 1;
    if(std::wcscmp(input,L"dump")==0) return 2;
    if(std::wcscmp(input,L"pause")==0) return 3;
    if(std::wcscmp(input,L"resume")==0) return 4;
    throw Failure(2,"Operation must be snapshot, dump, pause or resume.");
}
} // namespace
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wcscmp(argv[1],L"--help")==0) {
            std::puts("DEVELOPMENT ONLY: inline_debug_control.exe PID snapshot|dump|pause|resume\n"
                "Calls only RNDebugRequest in the already-loaded, exact pinned sibling debug DLL.\n"
                "No injection/LoadLibrary remote, arbitrary DLL/export, game start, retry or kill.\n"
                "0 means QUEUED, not snapshot/dump complete. 41 inactive;42 invalid;43 serial exhausted.\n"
                "Uncertain completion retains pins quietly until thread or captured target exit.\n"
                "Use one debug bridge variant in a fresh process; --help performs no target/file access.");
            return 0;
        }
        if(argc!=3) throw Failure(2,"Usage: inline_debug_control.exe PID snapshot|dump|pause|resume (or --help).");
        const DWORD pid=parse_pid(argv[1]);
        const DWORD opcode=parse_opcode(argv[2]);
        return request(pid,opcode);
    } catch(const Failure& failure) { std::fprintf(stderr,"%s\n",failure.what()); return failure.code; }
    catch(...) { std::fputs("Unexpected controller failure; no retry.\n",stderr); return 99; }
}
