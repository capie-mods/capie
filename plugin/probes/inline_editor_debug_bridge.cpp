// Isolated own-field debug bridge; all predecessors remain frozen.
// Additive bounded pre-observation only; no Enter/focus/lifecycle policy changes.
// Enter remains copied diagnostic only; ordinary own buttons are the transport origin.
// Exact pinned image only; build into a separate output directory.
#include "../src/image_guard.hpp"
#include "inline_debug_trace.hpp"
#include "../src/editor_input.hpp"
#include "../src/country_capability.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <type_traits>
namespace {
constexpr std::uintptr_t tick_rva=0xB88950, key_rva=0x22F1030, bind_rva=0xB86320;
constexpr std::uintptr_t dispatch_rva=0xB867D0, ctor_rva=0x1144840;
constexpr std::size_t max_original_records=256, max_transport_records=70;
constexpr std::int64_t fixed_point=100000;
constexpr char command_epoch_key[]="rn_ip_cmd_epoch";
static_assert(sizeof(command_epoch_key)-1==15);
constexpr std::uintptr_t find_edit_rva=0x22BAD40, find_instant_rva=0x22BC600, set_text_rva=0x22F27C0;
constexpr std::uintptr_t refresh_edit_rva=0x22F4D80;
constexpr std::uintptr_t wrapper_rva=0x22F4580;
constexpr std::uintptr_t execute_rva=0x1156950;
constexpr std::size_t max_codepoints=128, max_utf8=max_codepoints*4;
struct MsvcString {
    union { unsigned char small[16]; const char* pointer; } data;
    std::uint64_t size, capacity;
};
static_assert(sizeof(MsvcString)==0x20 && offsetof(MsvcString,size)==0x10 && offsetof(MsvcString,capacity)==0x18);
using Tick=void(__fastcall*)(void*,char);
using Bind=void(__fastcall*)(void*);
using HandleKey=std::uint64_t(__fastcall*)(void*,void*);
using FindWidget=void*(__fastcall*)(void*,const MsvcString*,bool,bool);
using SetText=void(__fastcall*)(void*,const MsvcString*);
using RefreshEdit=void(__fastcall*)(void*);
using Dispatch=void(__fastcall*)(void*,void*);
using Execute=void(__fastcall*)(void*);
struct Record { MsvcString key; std::int64_t raw_value; };
struct TempVector { Record* data; std::int32_t capacity; std::int32_t count; void* allocator; };
static_assert(sizeof(Record)==0x28 && offsetof(Record,raw_value)==0x20);
static_assert(sizeof(TempVector)==0x18 && offsetof(TempVector,count)==0xC &&
              offsetof(TempVector,allocator)==0x10);
static_assert(std::is_trivially_copyable_v<Record> && std::is_trivially_copyable_v<TempVector>);
using Constructor=void*(__fastcall*)(void*,const MsvcString*,const MsvcString*,const void*,const TempVector*);
struct InputSnapshot { std::uint32_t points[max_codepoints]{}; std::size_t size{}; };
struct Utf8Snapshot { char bytes[max_utf8+1]{}; std::size_t size{}; InputSnapshot decoded{}; };
struct Field { const char* edit; const char* carrier; std::size_t model; };
struct Model { const char* name; const char* epoch; const char* apply; std::size_t first; std::size_t count; };
constexpr Field fields[]={
    {"rn_ip_leader_edit","rn_ip_leader_prefill",0},
    {"rn_ip_party_neutrality_edit","rn_ip_party_neutrality_prefill",1},
    {"rn_ip_party_neutrality_long_edit","rn_ip_party_neutrality_long_prefill",1},
    {"rn_ip_country_name_edit","rn_ip_country_name_prefill",2},
    {"rn_ip_country_adj_edit","rn_ip_country_adj_prefill",2}
};
constexpr Model models[]={
    {"rn_ip_leader_gui","rn_ip_leader_epoch","rn_ip_leader_apply_click",0,1},
    {"rn_ip_party_neutrality_gui","rn_ip_party_neutrality_epoch","rn_ip_party_neutrality_apply_click",1,2},
    {"rn_ip_country_gui","rn_ip_country_epoch","rn_ip_country_apply_click",3,2}
};
constexpr std::size_t field_count=sizeof(fields)/sizeof(fields[0]), model_count=sizeof(models)/sizeof(models[0]);
static_assert(field_count==5 && model_count==3 && models[0].count==1 && models[1].count==2 && models[2].count==2);
constexpr std::size_t country_model=2;
struct Pending {
    bool valid{}; std::uint64_t epoch{},generation{}; ULONGLONG created{}; DWORD thread{};
    InputSnapshot text{};
};
struct Lifecycle {
    bool visible{},attempted{},prefilled{};
    std::uint64_t epoch{},generation{};
    unsigned last_status{};
};
// Persistent structures contain only scalars and copied text, never engine pointers.
Lifecycle lifecycle[model_count]{};
Pending pending[field_count]{};
SRWLOCK state_lock=SRWLOCK_INIT;
// Stack-bound synchronous command origin; never stored in lifecycle/pending state.
struct Origin {
    void* context{};
    const MsvcString* model_name{};
    const MsvcString* selector{};
    const void* scope{};
    const TempVector* source{};
    Record records[max_original_records+max_transport_records]{};
    TempVector view{};
    unsigned ctor_calls{};
    unsigned long id{};
    std::uint64_t epoch{},generation{};
    std::size_t model{model_count};
};
thread_local Origin* origin=nullptr;
struct OriginFrame {
    Origin* previous;
    explicit OriginFrame(Origin* next) noexcept : previous(origin) { origin=next; }
    ~OriginFrame() { origin=previous; }
    OriginFrame(const OriginFrame&)=delete;
    OriginFrame& operator=(const OriginFrame&)=delete;
};
HMODULE own_module=nullptr;
std::uintptr_t image_base=0;
Tick original_tick=nullptr;
Bind original_bind=nullptr;
HandleKey original_key=nullptr, original_wrapper=nullptr;
FindWidget find_edit=nullptr, find_instant=nullptr;
SetText set_text=nullptr;
RefreshEdit refresh_edit=nullptr;
Dispatch original_dispatch=nullptr;
Execute original_execute=nullptr;
Constructor original_ctor=nullptr;
HANDLE log_file=INVALID_HANDLE_VALUE;
rn::debug::Ring debug_ring;
rn::debug::Control debug_control;
SRWLOCK debug_file_lock=SRWLOCK_INIT, debug_dump_lock=SRWLOCK_INIT, debug_request_lock=SRWLOCK_INIT;
rn::debug::Dump debug_dump_buffer;
std::atomic<bool> active{false};
std::atomic<unsigned> init_state{0};
std::atomic<unsigned long> observation_id{0};
bool readable(const void* source, std::size_t size) noexcept {
    if (size == 0) return true;
    auto cursor = reinterpret_cast<std::uintptr_t>(source);
    if (!cursor || size > (std::numeric_limits<std::uintptr_t>::max)() - cursor) return false;
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE &&
            access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - start) return false;
        const auto next = start + region.RegionSize;
        if (next <= cursor) return false;
        cursor = next < end ? next : end;
    }
    return true;
}

int memory_exception(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// SEH is restricted to POD helpers; no C++ destructors are bypassed.
bool guarded_copy(void* target, const void* source, std::size_t size) noexcept {
    if (!readable(source, size)) return false;
    __try { if (size) std::memcpy(target, source, size); return true; }
    __except (memory_exception(GetExceptionCode())) { return false; }
}

template<class T> bool member(const void* object, std::size_t offset, T& value) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    if (!address || offset > (std::numeric_limits<std::uintptr_t>::max)() - address) return false;
    return guarded_copy(&value, reinterpret_cast<const void*>(address + offset), sizeof(T));
}

void write_bytes(const char* text,std::size_t size) noexcept {
    if(log_file==INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(log_file,text,static_cast<DWORD>(size),&written,nullptr);
}
void remember_log(const char* text) noexcept {
    rn::debug::Stamp stamp{};
    debug_ring.append(text,std::strlen(text),GetTickCount64(),GetCurrentThreadId(),stamp);
}
void write_log(const char* text) noexcept {
    rn::debug::Stamp stamp{};
    const auto size=std::strlen(text);
    const bool remembered=debug_ring.append(text,size,GetTickCount64(),GetCurrentThreadId(),stamp);
    char suffix[160]{};
    const auto count=remembered?std::snprintf(suffix,sizeof(suffix),
        " trace_seq=%llu monotonic_ms=%llu trace_thread=%lu truncated=%u\r\n",
        static_cast<unsigned long long>(stamp.sequence),static_cast<unsigned long long>(stamp.ms),
        stamp.thread,stamp.truncated?1u:0u):std::snprintf(suffix,sizeof(suffix)," trace_paused=1\r\n");
    std::size_t end=size;
    while(end && (text[end-1]=='\r' || text[end-1]=='\n')) --end;
    AcquireSRWLockExclusive(&debug_file_lock);
    write_bytes(text,end); write_bytes(suffix,static_cast<std::size_t>(count));
    ReleaseSRWLockExclusive(&debug_file_lock);
}
void dump_debug_ring(std::uint64_t request) noexcept {
    if(!TryAcquireSRWLockExclusive(&debug_dump_lock)) {
        write_log("RN_DEBUG stage=dump reason=dump_already_running; no retry\r\n"); return;
    }
    debug_ring.copy(debug_dump_buffer);
    char header[256]{};
    auto count=std::snprintf(header,sizeof(header),
        "RN_DEBUG_DUMP begin request=%llu count=%zu overwritten=%llu sequence_saturated=%u; copied history, no engine calls\r\n",
        static_cast<unsigned long long>(request),debug_dump_buffer.count,
        static_cast<unsigned long long>(debug_dump_buffer.overwritten),debug_dump_buffer.sequence_saturated?1u:0u);
    AcquireSRWLockExclusive(&debug_file_lock);
    write_bytes(header,static_cast<std::size_t>(count));
    for(std::size_t i=0;i<debug_dump_buffer.count;++i) {
        const auto& entry=debug_dump_buffer.entries[i];
        count=std::snprintf(header,sizeof(header),"RN_DEBUG_HISTORY seq=%llu monotonic_ms=%llu thread=%lu truncated=%u | ",
            static_cast<unsigned long long>(entry.stamp.sequence),static_cast<unsigned long long>(entry.stamp.ms),
            entry.stamp.thread,entry.stamp.truncated?1u:0u);
        write_bytes(header,static_cast<std::size_t>(count)); write_bytes(entry.text,entry.size);
        if(!entry.size || entry.text[entry.size-1]!='\n') write_bytes("\r\n",2);
    }
    write_bytes("RN_DEBUG_DUMP end\r\n",19);
    ReleaseSRWLockExclusive(&debug_file_lock);
    ReleaseSRWLockExclusive(&debug_dump_lock);
}

bool encode_utf8(const std::uint32_t* points, std::size_t count, char* out, std::size_t& size) noexcept {
    size = 0;
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < count; ++i) {
        const auto cp = points[i];
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (cp == '"' || cp == '\\') { out[size++] = '\\'; out[size++] = static_cast<char>(cp); }
        else if (cp < 0x20 || cp == 0x7f) {
            out[size++] = '\\'; out[size++] = 'u'; out[size++] = '0'; out[size++] = '0';
            out[size++] = digits[(cp >> 4) & 15]; out[size++] = digits[cp & 15];
        } else if (cp < 0x80) { out[size++] = static_cast<char>(cp); }
        else if (cp < 0x800) {
            out[size++] = static_cast<char>(0xc0 | (cp >> 6));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[size++] = static_cast<char>(0xe0 | (cp >> 12));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        } else {
            out[size++] = static_cast<char>(0xf0 | (cp >> 18));
            out[size++] = static_cast<char>(0x80 | ((cp >> 12) & 63));
            out[size++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
            out[size++] = static_cast<char>(0x80 | (cp & 63));
        }
    }
    out[size] = '\0';
    return true;
}



MsvcString borrowed_string(const char* bytes,std::size_t size) noexcept {
    MsvcString result{}; result.size=size;
    if(size<=15) { if(size) std::memcpy(result.data.small,bytes,size); result.capacity=15; }
    else { result.data.pointer=bytes; result.capacity=size; }
    return result;
}
bool string_bytes(const void* native,char* out,std::size_t limit,std::size_t& size) noexcept {
    MsvcString value{};
    if(!guarded_copy(&value,native,sizeof(value)) || value.size>limit ||
       value.size>value.capacity || value.capacity>1048576) return false;
    const void* bytes=value.capacity<=15?static_cast<const void*>(value.data.small):static_cast<const void*>(value.data.pointer);
    size=static_cast<std::size_t>(value.size);
    return guarded_copy(out,bytes,size+1) && out[size]=='\0' && std::memchr(out,'\0',size)==nullptr;
}
bool name_equals(const void* native,const char* expected) noexcept {
    char value[129]{}; std::size_t size=0; const auto wanted=std::strlen(expected);
    return wanted<=128 && string_bytes(native,value,128,size) && size==wanted && std::memcmp(value,expected,size)==0;
}
bool bounded_list(const void* container,std::size_t offset) noexcept {
    void* data=nullptr; int capacity=0,count=0;
    return member(container,offset,data) && member(container,offset+8,capacity) && member(container,offset+12,count) &&
        count>=0 && count<=4096 && capacity>=count && capacity<=65536 && readable(data,static_cast<std::size_t>(count)*sizeof(void*));
}
void* guarded_find(FindWidget lookup,void* root,const char* name) noexcept {
    const auto text=borrowed_string(name,std::strlen(name));
    __try { return lookup(root,&text,false,false); }
    __except(memory_exception(GetExceptionCode())) { return nullptr; }
}
bool guarded_set(void* edit,const MsvcString* text) noexcept {
    __try { set_text(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit)+0x80),text); return true; }
    __except(memory_exception(GetExceptionCode())) { return false; }
}
bool guarded_refresh(void* edit) noexcept {
    if(!refresh_edit) return false;
    __try { refresh_edit(edit); return true; }
    __except(memory_exception(GetExceptionCode())) { return false; }
}
bool read_input_active(void* edit,bool& result) noexcept {
    void* manager=nullptr; void* receiver=nullptr; unsigned char flags=0;
    if(!member(reinterpret_cast<void*>(image_base),0x3453230,manager) || !member(manager,0x230,receiver) ||
       !member(edit,0x271,flags)) return false;
    result=(flags&1)!=0 && reinterpret_cast<std::uintptr_t>(receiver)==reinterpret_cast<std::uintptr_t>(edit)+0x1A0;
    return true;
}
bool input_active(void* edit) noexcept {
    bool result=false;
    return read_input_active(edit,result) && result;
}
bool visible_edit(void* edit) noexcept {
    unsigned char flags=0;
    return member(edit,0x75,flags) && (flags&8)!=0 && (flags&(4|16))==0;
}
bool edit_named(void* edit,const char* expected) noexcept {
    std::uintptr_t edit_vt=0,tb_vt=0,definition_vt=0; void* definition=nullptr;
    return member(edit,0,edit_vt) && edit_vt==image_base+0x2B4A940 &&
        member(edit,0x80,tb_vt) && tb_vt==image_base+0x2B4ABD0 &&
        member(edit,0x60,definition) && member(definition,0,definition_vt) && definition_vt==image_base+0x2B54050 &&
        name_equals(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(definition)+0x28),expected);
}
bool snapshot_edit(void* edit,const char* expected,InputSnapshot& result) noexcept {
    std::uint64_t size=0,capacity=0;
    if(!edit_named(edit,expected) || !member(edit,0xC0,size) || !member(edit,0xC8,capacity) ||
       size>max_codepoints || size>capacity || capacity>65536) return false;
    const void* text=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(edit)+0xB0);
    if(capacity>3 && !member(edit,0xB0,text)) return false;
    if(!guarded_copy(result.points,text,static_cast<std::size_t>(size)*sizeof(result.points[0]))) return false;
    result.size=static_cast<std::size_t>(size); return true;
}
bool same_text(const InputSnapshot& a,const InputSnapshot& b) noexcept {
    return a.size==b.size && std::memcmp(a.points,b.points,a.size*sizeof(a.points[0]))==0;
}
bool decode_utf8(Utf8Snapshot& value) noexcept {
    // No trimming, normalization, substitution or truncation of the game name.
    wchar_t utf16[max_utf8]{};
    if(value.size==0 || value.size>max_utf8) return false;
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.bytes,static_cast<int>(value.size),
                                      utf16,static_cast<int>(max_utf8));
    if(count<=0) return false;
    for(int i=0;i<count;++i) {
        std::uint32_t cp=static_cast<std::uint16_t>(utf16[i]);
        if(cp>=0xD800 && cp<=0xDBFF) {
            if(++i>=count) return false;
            const auto low=static_cast<std::uint32_t>(static_cast<std::uint16_t>(utf16[i]));
            if(low<0xDC00 || low>0xDFFF) return false;
            cp=0x10000+((cp-0xD800)<<10)+(low-0xDC00);
        } else if(cp>=0xDC00 && cp<=0xDFFF) return false;
        // Also reject unresolved bracket expressions and common render controls.
        if(cp<0x20 || (cp>=0x7F && cp<=0x9F) || cp=='[' || cp==']' || cp=='$' || cp==0xA7 || cp==0xA3) return false;
        if(value.decoded.size==max_codepoints) return false;
        value.decoded.points[value.decoded.size++]=cp;
    }
    return true;
}
bool carrier(void* root,const char* name,Utf8Snapshot& result,bool decode) noexcept {
    void* widget=guarded_find(find_instant,root,name); std::uintptr_t vt=0;
    if(!member(widget,0,vt) || vt!=image_base+0x2B468C8) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(widget);
    if(address>(std::numeric_limits<std::uintptr_t>::max)()-0x110 ||
       !string_bytes(reinterpret_cast<void*>(address+0x110),result.bytes,max_utf8,result.size)) return false;
    return !decode || decode_utf8(result);
}
bool read_epoch(void* root,std::size_t model,std::uint64_t& result) noexcept {
    Utf8Snapshot text{};
    if(!carrier(root,models[model].epoch,text,false) || text.size==0 || text.size>15) return false;
    std::uint64_t value=0;
    for(std::size_t i=0;i<text.size;++i) {
        const auto c=static_cast<unsigned char>(text.bytes[i]); if(c<'0' || c>'9') return false;
        value=value*10+c-'0';
    }
    if(value==0 || value>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/100000)) return false;
    result=value; return true;
}
enum class ViewResult { unrelated,invalid,hidden,visible };
struct View { void* root{}; std::size_t model{}; };
ViewResult inspect_window(void* window,View& out) noexcept {
    void* model=nullptr; std::uintptr_t model_vt=0;
    if(!member(window,0xE8,model) || !member(model,0,model_vt) || model_vt!=image_base+0x29DB558) return ViewResult::unrelated;
    const auto model_address=reinterpret_cast<std::uintptr_t>(model);
    if(model_address>(std::numeric_limits<std::uintptr_t>::max)()-8) return ViewResult::unrelated;
    bool recognized=false;
    for(std::size_t i=0;i<model_count;++i) if(name_equals(reinterpret_cast<void*>(model_address+8),models[i].name)) {
        out.model=i; recognized=true; break;
    }
    if(!recognized) return ViewResult::unrelated;
    const auto address=reinterpret_cast<std::uintptr_t>(window);
    if(address>(std::numeric_limits<std::uintptr_t>::max)()-0x110) return ViewResult::invalid;
    void* context=reinterpret_cast<void*>(address+0x110);
    std::uintptr_t ctx_vt=0,root_vt=0; void* ctx_window=nullptr; void* ctx_model=nullptr; void* ctx_root=nullptr;
    unsigned char flags=0,desired=0;
    if(!member(context,0,ctx_vt) || ctx_vt!=image_base+0x294E568 ||
       !member(context,0x10,ctx_window) || ctx_window!=window || !member(context,0x20,ctx_model) || ctx_model!=model ||
       !member(context,0x28,ctx_root) || !member(window,0xF8,out.root) || ctx_root!=out.root ||
       !member(out.root,0,root_vt) || root_vt!=image_base+0x2B45278 ||
       !member(out.root,0xA5,flags) || !member(window,0xB79,desired) || !readable(out.root,0x1360) ||
       !bounded_list(out.root,0x1240) || !bounded_list(out.root,0x11E0) || !bounded_list(out.root,0x1330)) return ViewResult::invalid;
    return desired && (flags&8) && !(flags&(4|16))?ViewResult::visible:ViewResult::hidden;
}
void reset_model(std::size_t model,bool hidden) noexcept {
    AcquireSRWLockExclusive(&state_lock);
    auto& state=lifecycle[model];
    if(hidden) {
        // A valid bound root was merely hidden. Retain this opening's draft/readiness.
        if(state.visible) ++state.generation;
        state.visible=false;
    } else {
        // An invalid root/epoch cannot authorize input. Same-epoch recovery must
        // not silently retry SetText over a draft: require a new Open or bind.
        state.visible=false; state.attempted=true; state.prefilled=false; ++state.generation;
    }
    for(std::size_t i=models[model].first;i<models[model].first+models[model].count;++i) pending[i]={};
    ReleaseSRWLockExclusive(&state_lock);
}
std::uint64_t reset_bound_model(std::size_t model) noexcept {
    AcquireSRWLockExclusive(&state_lock);
    auto& state=lifecycle[model];
    state.visible=false; state.attempted=false; state.prefilled=false;
    state.epoch=0; ++state.generation; state.last_status=0;
    for(std::size_t i=models[model].first;i<models[model].first+models[model].count;++i) pending[i]={};
    const auto generation=state.generation;
    ReleaseSRWLockExclusive(&state_lock);
    return generation;
}
void diagnostic(unsigned long id,const char* event,std::size_t field,std::uint64_t epoch,
                std::uint64_t generation,const InputSnapshot* text=nullptr,DWORD producer=0) noexcept {
    char line[3072]{};
    if(!text) std::snprintf(line,sizeof(line),"RN_INLINE id=%lu event=%s field=%s epoch=%llu generation=%llu thread=%lu; no Apply\r\n",
        id,event,fields[field].edit,static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(generation),GetCurrentThreadId());
    else {
        char encoded[max_codepoints*6+1]{}; std::size_t escaped=0;
        const bool valid=encode_utf8(text->points,text->size,encoded,escaped);
        char32_t points[max_codepoints]{};
        for(std::size_t i=0;i<text->size;++i) points[i]=static_cast<char32_t>(text->points[i]);
        const auto converted=rn::editor::encode_field(std::u32string_view(points,text->size));
        char codepoints[max_codepoints*9+1]{}; std::size_t position=0,utf8_bytes=0;
        for(std::size_t i=0;i<text->size;++i) {
            position+=static_cast<std::size_t>(std::snprintf(codepoints+position,
                sizeof(codepoints)-position,"%s%04X",i?" ":"",static_cast<unsigned>(text->points[i])));
            const auto cp=text->points[i];
            if(valid) utf8_bytes+=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
        }
        std::snprintf(line,sizeof(line),
            "RN_INLINE id=%lu event=%s field=%s epoch=%llu generation=%llu producer_thread=%lu consumer_thread=%lu "
            "utf32_len=%zu utf8_valid=%u utf8_bytes=%zu escaped_bytes=%zu text=\"%s\" codepoints=[%s] "
            "strict_input_ok=%u input_error=%u error_index=%zu; no Apply\r\n",
            id,event,fields[field].edit,static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(generation),
            producer,GetCurrentThreadId(),text->size,valid?1u:0u,utf8_bytes,valid?escaped:0,valid?encoded:"<invalid scalar>",
            codepoints,converted.value?1u:0u,static_cast<unsigned>(converted.error),converted.error_index);
    }
    write_log(line);
}
void prefill_open(void* window,const View& view,std::uint64_t epoch,std::uint64_t generation,unsigned long id) {
    // Prepare all fields before any SetText. A failed/partial attempt is not retried.
    void* edits[2]{}; Utf8Snapshot text[2]{}; const auto& model=models[view.model];
    for(std::size_t j=0;j<model.count;++j) {
        const auto field=model.first+j;
        edits[j]=guarded_find(find_edit,view.root,fields[field].edit);
        bool focused=false;
        if(!edit_named(edits[j],fields[field].edit)) {
            diagnostic(id,"prefill-edit-lookup-type-rejected",field,epoch,generation); return;
        }
        if(!visible_edit(edits[j])) {
            diagnostic(id,"prefill-edit-visibility-rejected",field,epoch,generation); return;
        }
        if(!read_input_active(edits[j],focused)) {
            diagnostic(id,"prefill-focus-state-unreadable",field,epoch,generation); return;
        }
        if(focused) {
            diagnostic(id,"prefill-focused-draft-preserved",field,epoch,generation); return;
        }
        if(!carrier(view.root,fields[field].carrier,text[j],false)) {
            diagnostic(id,"prefill-carrier-lookup-string-rejected",field,epoch,generation); return;
        }
        if(!decode_utf8(text[j])) {
            diagnostic(id,"prefill-carrier-unicode-render-rejected",field,epoch,generation); return;
        }
    }
    std::uint64_t fresh_epoch=0;
    if(!read_epoch(view.root,view.model,fresh_epoch) || fresh_epoch!=epoch) {
        diagnostic(id,"prefill-epoch-changed",model.first,epoch,generation); return;
    }
    for(std::size_t j=0;j<model.count;++j) {
        const auto field=model.first+j;
        const auto native=borrowed_string(text[j].bytes,text[j].size); InputSnapshot check{};
        bool focused=false;
        if(!edit_named(edits[j],fields[field].edit) || !visible_edit(edits[j]) ||
           !read_input_active(edits[j],focused) || focused ||
           !guarded_set(edits[j],&native) || !snapshot_edit(edits[j],fields[field].edit,check) ||
           !same_text(check,text[j].decoded)) {
            diagnostic(id,"prefill-write-readback-failed-reopen-required",field,epoch,generation); return;
        }
        // Native state-name/text-input paths refresh the CEditBox after TB SetText.
        // Resolve it again after the setter/observers; never write dirty flags.
        View fresh_view{}; std::uint64_t refresh_epoch=0;
        if(inspect_window(window,fresh_view)!=ViewResult::visible || fresh_view.model!=view.model ||
           fresh_view.root!=view.root || !read_epoch(fresh_view.root,view.model,refresh_epoch) || refresh_epoch!=epoch) {
            diagnostic(id,"prefill-render-view-changed-reopen-required",field,epoch,generation); return;
        }
        bool same_opening=false;
        AcquireSRWLockShared(&state_lock);
        const auto& current=lifecycle[view.model];
        same_opening=current.visible && current.attempted && current.epoch==epoch && current.generation==generation;
        ReleaseSRWLockShared(&state_lock);
        void* fresh_edit=guarded_find(find_edit,fresh_view.root,fields[field].edit);
        if(!same_opening || fresh_edit!=edits[j] || !edit_named(fresh_edit,fields[field].edit) ||
           !visible_edit(fresh_edit) || !read_input_active(fresh_edit,focused) || focused ||
           !snapshot_edit(fresh_edit,fields[field].edit,check) || !same_text(check,text[j].decoded) ||
           !guarded_refresh(fresh_edit)) {
            diagnostic(id,"prefill-render-refresh-failed-reopen-required",field,epoch,generation); return;
        }
        // The native refresh may notify observers; re-resolve before accepting it.
        View rendered_view{}; std::uint64_t rendered_epoch=0;
        if(inspect_window(window,rendered_view)!=ViewResult::visible || rendered_view.model!=view.model ||
           rendered_view.root!=view.root || !read_epoch(rendered_view.root,view.model,rendered_epoch) || rendered_epoch!=epoch) {
            diagnostic(id,"prefill-render-post-view-changed-reopen-required",field,epoch,generation); return;
        }
        AcquireSRWLockShared(&state_lock);
        const auto& rendered=lifecycle[view.model];
        same_opening=rendered.visible && rendered.attempted && rendered.epoch==epoch && rendered.generation==generation;
        ReleaseSRWLockShared(&state_lock);
        void* rendered_edit=guarded_find(find_edit,rendered_view.root,fields[field].edit);
        if(!same_opening || rendered_edit!=fresh_edit || !edit_named(rendered_edit,fields[field].edit) ||
           !visible_edit(rendered_edit) || !read_input_active(rendered_edit,focused) || focused ||
           !snapshot_edit(rendered_edit,fields[field].edit,check) || !same_text(check,text[j].decoded)) {
            diagnostic(id,"prefill-render-post-readback-failed-reopen-required",field,epoch,generation); return;
        }
        // A returned refresh is not visual acceptance; QA must inspect rendered text.
        diagnostic(id,"prefill-render-refresh-returned",field,epoch,generation);
        diagnostic(id,"prefill-readback-pass",field,epoch,generation,&check);
    }
    AcquireSRWLockExclusive(&state_lock);
    auto& state=lifecycle[view.model];
    if(state.visible && state.epoch==epoch && state.generation==generation) state.prefilled=true;
    ReleaseSRWLockExclusive(&state_lock);
}
void consume_diagnostic(const View& view,std::uint64_t epoch,std::uint64_t generation,unsigned long id) noexcept {
    for(std::size_t i=models[view.model].first;i<models[view.model].first+models[view.model].count;++i) {
        Pending request{};
        AcquireSRWLockExclusive(&state_lock); request=pending[i]; pending[i]={}; ReleaseSRWLockExclusive(&state_lock);
        if(!request.valid) continue;
        void* edit=guarded_find(find_edit,view.root,fields[i].edit); InputSnapshot current{};
        const auto age=GetTickCount64()-request.created;
        const bool valid=request.epoch==epoch && request.generation==generation && age<=2000 &&
            edit_named(edit,fields[i].edit) && input_active(edit) && snapshot_edit(edit,fields[i].edit,current) &&
            same_text(request.text,current);
        diagnostic(id,valid?"deferred-enter-validated-diagnostic":"deferred-enter-rejected",
            i,epoch,generation,&request.text,request.thread);
        // Enter remains diagnostic only. It never calls dispatch or injects command variables.
    }
}
void status_once(std::size_t model,unsigned status,const char* event) noexcept {
    bool changed=false;
    AcquireSRWLockExclusive(&state_lock);
    if(lifecycle[model].last_status!=status) {
        lifecycle[model].last_status=status; changed=true;
    }
    ReleaseSRWLockExclusive(&state_lock);
    if(changed) diagnostic(observation_id.fetch_add(1)+1,event,models[model].first,0,0);
}
void observe_tick(void* window) {
    View view{}; const auto result=inspect_window(window,view);
    if(result==ViewResult::unrelated) return;
    if(result!=ViewResult::visible) {
        status_once(view.model,result==ViewResult::hidden?1u:2u,
            result==ViewResult::hidden?"own-root-hidden":"own-root-guard-rejected");
        reset_model(view.model,result==ViewResult::hidden); return;
    }
    std::uint64_t epoch=0;
    if(!read_epoch(view.root,view.model,epoch)) {
        status_once(view.model,3u,"epoch-carrier-rejected");
        reset_model(view.model,false); return;
    }
    status_once(view.model,4u,"own-root-visible-epoch-valid");
    bool attempt=false; std::uint64_t generation=0;
    AcquireSRWLockExclusive(&state_lock);
    auto& state=lifecycle[view.model];
    if(state.epoch!=epoch) {
        state.attempted=false; state.prefilled=false; state.epoch=epoch; ++state.generation;
        for(std::size_t i=models[view.model].first;i<models[view.model].first+models[view.model].count;++i) pending[i]={};
    }
    state.visible=true;
    generation=state.generation;
    if(!state.attempted) { state.attempted=true; attempt=true; }
    ReleaseSRWLockExclusive(&state_lock);
    const auto id=observation_id.fetch_add(1)+1;
    if(attempt) prefill_open(window,view,epoch,generation,id);
    consume_diagnostic(view,epoch,generation,id);
}
// Added observations own only copied scalars; no text or engine-pointer cache.
enum class KeyCategory : std::size_t { meaningful, empty_poll, event_read_failed };
constexpr std::size_t key_category_count=3;
constexpr unsigned key_record_limits[key_category_count]={32,2,2}, tick_record_limit=64;
constexpr const char* key_category_names[key_category_count]={"meaningful","empty_poll","event_read_failed"};
std::atomic<unsigned> key_record_counts[2][field_count][key_category_count]{};
std::atomic<std::uint64_t> key_record_sequence{0};
struct KeyStatus {
    unsigned mask{};
    unsigned char flags75{},flags271{};
    bool manager_present{},widget_is_edit{},receiver_is_edit{};
    bool visible{},attempted{},prefilled{};
    std::uint64_t epoch{},generation{};
    bool operator==(const KeyStatus&) const =default;
};
struct KeyRecord {
    std::uint32_t values[8]{};
    std::uint32_t wrapper_type{};
    unsigned mask{};
    std::size_t field{field_count};
    bool wrapper{};
    DWORD thread{};
    KeyStatus status{};
};
static_assert(std::is_trivially_copyable_v<KeyStatus> && std::is_trivially_copyable_v<KeyRecord>);
struct TickRecord {
    bool seen{};
    unsigned view{},count{};
    KeyStatus status{};
};
TickRecord tick_records[field_count+model_count]{};
SRWLOCK logger_lock=SRWLOCK_INIT;
enum class LogSlot { record,cap,drop };
LogSlot take_key_slot(std::atomic<unsigned>& count,unsigned limit) noexcept {
    auto current=count.load(std::memory_order_relaxed);
    while(current<=limit) {
        if(count.compare_exchange_weak(current,current+1,std::memory_order_relaxed))
            return current<limit?LogSlot::record:LogSlot::cap;
    }
    return LogSlot::drop; // Saturating: unlimited repeats cannot wrap the cap.
}
void copy_key_lifecycle(std::size_t field,KeyStatus& out) noexcept {
    AcquireSRWLockShared(&state_lock);
    const auto& state=lifecycle[fields[field].model];
    out.visible=state.visible; out.attempted=state.attempted; out.prefilled=state.prefilled;
    out.epoch=state.epoch; out.generation=state.generation; out.mask|=32;
    ReleaseSRWLockShared(&state_lock);
}
void copy_key_status(void* edit,std::size_t field,KeyStatus& out) noexcept {
    if(member(edit,0x75,out.flags75)) out.mask|=1;
    if(member(edit,0x271,out.flags271)) out.mask|=2;
    void* manager=nullptr; void* widget=nullptr; void* receiver=nullptr;
    if(member(reinterpret_cast<void*>(image_base),0x3453230,manager)) out.mask|=4;
    out.manager_present=manager!=nullptr;
    if(member(manager,0x158,widget)) {
        out.mask|=8; out.widget_is_edit=widget==edit;
    }
    if(member(manager,0x230,receiver)) {
        out.mask|=16;
        out.receiver_is_edit=reinterpret_cast<std::uintptr_t>(receiver)==reinterpret_cast<std::uintptr_t>(edit)+0x1A0;
    }
    copy_key_lifecycle(field,out);
}
bool make_key_record(void* edit,void* event,bool wrapper,KeyRecord& out) noexcept {
    out={}; out.field=field_count;
    // Own exact primary/template/TB type+name first. Never copy foreign payload.
    for(std::size_t i=0;i<field_count;++i) if(edit_named(edit,fields[i].edit)) { out.field=i; break; }
    if(out.field==field_count) return false;
    out.wrapper=wrapper; out.thread=GetCurrentThreadId();
    if(guarded_copy(out.values,event,sizeof(out.values))) out.mask|=1;
    if(wrapper && member(event,0x7C,out.wrapper_type)) out.mask|=2;
    copy_key_status(edit,out.field,out.status);
    return true; // Read/visibility/focus/readiness rejection is itself observable.
}
KeyCategory key_category(const KeyRecord& value) noexcept {
    // Failed/partial 8-DWORD copy cannot classify the contents as a key or poll.
    if(!(value.mask&1)) return KeyCategory::event_read_failed;
    for(const auto word:value.values) if(word!=0) return KeyCategory::meaningful;
    return KeyCategory::empty_poll; // A readable zero event can be a normal poll.
}
KeyStatus routing_status(KeyStatus value) noexcept {
    // Only the routing bits established by the retained native wrapper proof.
    // Preserve read masks, manager relations, lifecycle and view separately.
    value.flags75&=0x08; value.flags271&=0x03;
    return value;
}
void emit_key_record(const KeyRecord& value) noexcept {
    const auto category=static_cast<std::size_t>(key_category(value));
    const auto limit=key_record_limits[category];
    const auto slot=take_key_slot(key_record_counts[value.wrapper?0:1][value.field][category],limit);
    if(slot==LogSlot::drop && category!=static_cast<std::size_t>(KeyCategory::meaningful)) return;
    char line[2048]{};
    const auto sequence=key_record_sequence.fetch_add(1,std::memory_order_relaxed)+1;
    const char* source=value.wrapper?"wrapper":"tb";
    if(slot==LogSlot::cap) {
        std::snprintf(line,sizeof(line),"RN_KEYLOG seq=%llu source=%s field=%s category=%s category_cap=%u; later originals still forwarded\r\n",
            static_cast<unsigned long long>(sequence),source,fields[value.field].edit,key_category_names[category],limit);
        write_log(line);
        if(category!=static_cast<std::size_t>(KeyCategory::meaningful)) return;
    }
    char words[384]{}; std::size_t position=0;
    for(std::size_t i=0;i<8;++i) position+=static_cast<std::size_t>(std::snprintf(words+position,sizeof(words)-position,
        "%s%u/0x%08X",i?",":"",value.values[i],value.values[i]));
    const auto& s=value.status;
    std::snprintf(line,sizeof(line),
        "RN_KEYLOG seq=%llu source=%s field=%s category=%s thread=%lu copy_mask=%u status_mask=%u event=[%s] "
        "wrapper_type=%u flags75=0x%02X flags271=0x%02X manager_present=%u widget_is_edit=%u receiver_is_edit=%u "
        "life_visible=%u attempted=%u prefilled=%u epoch=%llu generation=%llu "
        "event_read=%u wrapper_read=%u key0_enter=%u modifier2_zero=%u field7_zero=%u wrapper_type1=%u "
        "not_disabled02=%u visible08=%u active01=%u manager_read=%u; pre-original only\r\n",
        static_cast<unsigned long long>(sequence),source,fields[value.field].edit,key_category_names[category],value.thread,value.mask,s.mask,words,
        value.wrapper_type,static_cast<unsigned>(s.flags75),static_cast<unsigned>(s.flags271),
        s.manager_present?1u:0u,s.widget_is_edit?1u:0u,s.receiver_is_edit?1u:0u,
        s.visible?1u:0u,s.attempted?1u:0u,s.prefilled?1u:0u,
        static_cast<unsigned long long>(s.epoch),static_cast<unsigned long long>(s.generation),
        (value.mask&1)?1u:0u,(value.mask&2)?1u:0u,(value.mask&1) && value.values[0]==13?1u:0u,
        (value.mask&1) && value.values[2]==0?1u:0u,(value.mask&1) && value.values[7]==0?1u:0u,
        value.wrapper && (value.mask&2) && value.wrapper_type==1?1u:0u,
        (s.mask&2) && !(s.flags271&2)?1u:0u,(s.mask&1) && (s.flags75&8)?1u:0u,
        (s.mask&2) && (s.flags271&1)?1u:0u,(s.mask&28)==28 && s.manager_present?1u:0u);
    if(slot==LogSlot::record) write_log(line);
    else remember_log(line); // Latest meaningful data survives the file quota.
}
__declspec(noinline) void key_prelog(void* edit,void* event,bool wrapper) noexcept {
    if(!active.load(std::memory_order_acquire)) return;
    KeyRecord record{};
    if(make_key_record(edit,event,wrapper,record)) emit_key_record(record);
}
void key_prelog_from_buffer(void* buffer,void* event) noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(buffer);
    if(address>=0x80) key_prelog(reinterpret_cast<void*>(address-0x80),event,false);
}
// Opaque full RAX passthrough; no bool normalization, post-call deref or retry.
__declspec(noinline) std::uint64_t __fastcall wrapper_observed(void* edit,void* event) {
    key_prelog(edit,event,true);
    return original_wrapper(edit,event);
}
void emit_tick_change(std::size_t slot,const char* name,ViewResult view,const KeyStatus& status,bool model) noexcept {
    bool changed=false,cap=false,file_record=false;
    const auto semantic=routing_status(status);
    AcquireSRWLockExclusive(&logger_lock);
    auto& previous=tick_records[slot];
    if(!previous.seen || previous.view!=static_cast<unsigned>(view) || !(previous.status==semantic)) {
        previous.seen=true; previous.view=static_cast<unsigned>(view); previous.status=semantic;
        changed=true;
        if(previous.count<=tick_record_limit) {
            cap=previous.count==tick_record_limit; ++previous.count; file_record=true;
        }
    }
    ReleaseSRWLockExclusive(&logger_lock);
    if(!changed) return;
    char line[1024]{};
    if(cap) std::snprintf(line,sizeof(line),"RN_KEYTICK kind=%s name=%s cap=%u; no heartbeat\r\n",model?"model":"field",name,tick_record_limit);
    else std::snprintf(line,sizeof(line),
        "RN_KEYTICK kind=%s name=%s view=%u status_mask=%u flags75=0x%02X flags271=0x%02X routing75=0x%02X routing271=0x%02X "
        "manager_present=%u widget_is_edit=%u receiver_is_edit=%u "
        "life_visible=%u attempted=%u prefilled=%u epoch=%llu generation=%llu; transition only\r\n",
        model?"model":"field",name,static_cast<unsigned>(view),status.mask,
        static_cast<unsigned>(status.flags75),static_cast<unsigned>(status.flags271),
        static_cast<unsigned>(semantic.flags75),static_cast<unsigned>(semantic.flags271),
        status.manager_present?1u:0u,status.widget_is_edit?1u:0u,status.receiver_is_edit?1u:0u,
        status.visible?1u:0u,status.attempted?1u:0u,status.prefilled?1u:0u,
        static_cast<unsigned long long>(status.epoch),static_cast<unsigned long long>(status.generation));
    if(file_record) write_log(line); else remember_log(line);
}
void observe_key_tick(void* window) noexcept {
    if(!active.load(std::memory_order_acquire)) return;
    View view{}; const auto result=inspect_window(window,view);
    if(result==ViewResult::unrelated) return;
    const auto& model=models[view.model];
    KeyStatus model_status{}; copy_key_lifecycle(model.first,model_status);
    emit_tick_change(field_count+view.model,model.name,result,model_status,true);
    if(result==ViewResult::invalid) return; // No borrowed invalid-root dereference.
    for(std::size_t field=model.first;field<model.first+model.count;++field) {
        void* edit=guarded_find(find_edit,view.root,fields[field].edit);
        if(!edit_named(edit,fields[field].edit)) continue;
        KeyStatus status{}; copy_key_status(edit,field,status);
        emit_tick_change(field,fields[field].edit,result,status,false);
    }
}
void debug_snapshot(void* window,const View& view,ViewResult result,std::uint64_t serial) noexcept {
    const char* reason="ok";
    std::uint64_t epoch=0;
    Lifecycle opening{};
    InputSnapshot texts[2]{};
    KeyStatus statuses[2]{};
    const auto& model=models[view.model];
    if(result!=ViewResult::visible) reason=result==ViewResult::hidden?"own_root_hidden":"own_root_invalid";
    else if(!read_epoch(view.root,view.model,epoch)) reason="epoch_unreadable";
    else {
        AcquireSRWLockShared(&state_lock); opening=lifecycle[view.model]; ReleaseSRWLockShared(&state_lock);
        for(std::size_t j=0;j<model.count;++j) {
            const auto field=model.first+j;
            void* edit=guarded_find(find_edit,view.root,fields[field].edit);
            if(!edit_named(edit,fields[field].edit)) { reason="edit_identity"; break; }
            if(!visible_edit(edit)) { reason="edit_hidden"; break; }
            if(!snapshot_edit(edit,fields[field].edit,texts[j])) { reason="text_unreadable_or_oversize"; break; }
            copy_key_status(edit,field,statuses[j]);
        }
        View fresh{}; std::uint64_t fresh_epoch=0; Lifecycle after{};
        if(std::strcmp(reason,"ok")==0 && (inspect_window(window,fresh)!=ViewResult::visible ||
           fresh.model!=view.model || fresh.root!=view.root || !read_epoch(fresh.root,view.model,fresh_epoch) || fresh_epoch!=epoch))
            reason="view_or_epoch_changed";
        AcquireSRWLockShared(&state_lock); after=lifecycle[view.model]; ReleaseSRWLockShared(&state_lock);
        if(std::strcmp(reason,"ok")==0 && (after.epoch!=opening.epoch || after.generation!=opening.generation ||
           after.visible!=opening.visible || after.prefilled!=opening.prefilled || after.attempted!=opening.attempted))
            reason="lifecycle_changed";
        if(std::strcmp(reason,"ok")==0) for(std::size_t j=0;j<model.count;++j) {
            const auto field=model.first+j; InputSnapshot check{};
            void* edit=guarded_find(find_edit,fresh.root,fields[field].edit);
            if(!visible_edit(edit) || !snapshot_edit(edit,fields[field].edit,check) || !same_text(check,texts[j])) {
                reason="text_changed_during_snapshot"; break;
            }
        }
        // Widget lookup is native and may be reentrant. Check the binding and
        // lifecycle again after the last lookup, even when text stayed equal.
        if(std::strcmp(reason,"ok")==0) {
            View final_view{}; std::uint64_t final_epoch=0; Lifecycle final_life{};
            if(inspect_window(window,final_view)!=ViewResult::visible || final_view.model!=view.model ||
               final_view.root!=view.root || !read_epoch(final_view.root,view.model,final_epoch) || final_epoch!=epoch)
                reason="final_view_or_epoch_changed";
            AcquireSRWLockShared(&state_lock); final_life=lifecycle[view.model]; ReleaseSRWLockShared(&state_lock);
            if(std::strcmp(reason,"ok")==0 && (final_life.epoch!=opening.epoch || final_life.generation!=opening.generation ||
               final_life.visible!=opening.visible || final_life.prefilled!=opening.prefilled || final_life.attempted!=opening.attempted))
                reason="final_lifecycle_changed";
        }
    }
    char line[768]{};
    std::snprintf(line,sizeof(line),
        "RN_DEBUG stage=snapshot request=%llu model=%s reason=%s epoch=%llu generation=%llu "
        "life_visible=%u attempted=%u prefilled=%u; observation only, no Apply\r\n",
        static_cast<unsigned long long>(serial),model.name,reason,static_cast<unsigned long long>(epoch),
        static_cast<unsigned long long>(opening.generation),opening.visible?1u:0u,opening.attempted?1u:0u,opening.prefilled?1u:0u);
    write_log(line);
    if(std::strcmp(reason,"ok")!=0) return;
    for(std::size_t j=0;j<model.count;++j) {
        const auto field=model.first+j; const auto& s=statuses[j];
        std::snprintf(line,sizeof(line),
            "RN_DEBUG stage=field_status request=%llu field=%s status_mask=%u flags75=0x%02X flags271=0x%02X "
            "manager_present=%u widget_is_edit=%u receiver_is_edit=%u\r\n",
            static_cast<unsigned long long>(serial),fields[field].edit,s.mask,static_cast<unsigned>(s.flags75),
            static_cast<unsigned>(s.flags271),s.manager_present?1u:0u,s.widget_is_edit?1u:0u,s.receiver_is_edit?1u:0u);
        write_log(line);
        diagnostic(observation_id.fetch_add(1)+1,"debug-request-field-snapshot",field,epoch,opening.generation,&texts[j],GetCurrentThreadId());
    }
}
void service_debug(void* window) noexcept {
    View view{};
    const auto result=inspect_window(window,view);
    const auto work=debug_control.take(GetTickCount64(),result==ViewResult::unrelated?0u:1u<<view.model);
    char line[512]{};
    if(work.changed || work.expired) {
        std::snprintf(line,sizeof(line),
            "RN_DEBUG stage=request request=%llu operation=%zu pending_models=0x%X expired_models=0x%X "
            "coalescing=latest_request ttl_ms=%llu; post-original UI tick, queued is not completed\r\n",
            static_cast<unsigned long long>(work.serial),static_cast<std::size_t>(work.operation),work.pending,work.expired,
            static_cast<unsigned long long>(rn::debug::snapshot_ttl_ms));
        write_log(line);
    }
    if(work.snapshot) debug_snapshot(window,view,result,work.serial);
    if(work.dump) dump_debug_ring(work.serial);
}
void __fastcall tick_observed(void* window,char force) {
    original_tick(window,force);
    if(active.load(std::memory_order_acquire)) {
        observe_tick(window);
        observe_key_tick(window);
        service_debug(window);
    }
}
void __fastcall bind_observed(void* window) {
    // Native construction/rebind and recursive widget binding finish first.
    // Forward once; do not access an old root or cache a window/widget pointer.
    original_bind(window);
    if(!active.load(std::memory_order_acquire)) return;
    View view{}; const auto result=inspect_window(window,view);
    if(result==ViewResult::unrelated) return;
    if(result==ViewResult::invalid) {
        reset_model(view.model,false);
        status_once(view.model,5u,"bind-own-root-guard-rejected");
        return;
    }
    // inspect_window verified exact own model + fresh context/window/root/lists.
    // The newly bound root is normally hidden; its next visible tick prefills.
    const auto generation=reset_bound_model(view.model);
    diagnostic(observation_id.fetch_add(1)+1,"own-model-bound-readiness-reset",
        models[view.model].first,0,generation);
}
bool enter_event(void* event) noexcept {
    int values[8]{};
    //2273320 active: event[0]!=0 && event[2]!=-1;2272FA0 additionally checks[7].
    return guarded_copy(values,event,sizeof(values)) && values[0]==0x0D && values[2]==0 && values[7]==0;
}
std::uint64_t __fastcall key_observed(void* buffer,void* event) {
    key_prelog_from_buffer(buffer,event); // Before the unchanged Enter predicate.
    std::size_t field=field_count; std::uint64_t epoch=0,generation=0;
    bool eligible=active.load(std::memory_order_acquire) && enter_event(event);
    const auto address=reinterpret_cast<std::uintptr_t>(buffer);
    void* edit=address>=0x80?reinterpret_cast<void*>(address-0x80):nullptr;
    if(eligible) {
        eligible=false;
        for(std::size_t i=0;i<field_count;++i) if(edit_named(edit,fields[i].edit) && input_active(edit)) {
            field=i; eligible=true; break;
        }
    }
    if(eligible) {
        AcquireSRWLockShared(&state_lock);
        const auto& state=lifecycle[fields[field].model];
        eligible=state.visible && state.prefilled; epoch=state.epoch; generation=state.generation;
        ReleaseSRWLockShared(&state_lock);
    }
    // Every key forwarded exactly once. No mutation inside native NotifyEnter.
    const auto result=original_key(buffer,event);
    if(eligible && active.load(std::memory_order_acquire)) {
        InputSnapshot text{};
        if(edit_named(edit,fields[field].edit) && snapshot_edit(edit,fields[field].edit,text)) {
            const auto id=observation_id.fetch_add(1)+1; bool queued=false;
            AcquireSRWLockExclusive(&state_lock);
            const auto& state=lifecycle[fields[field].model];
            if(state.visible && state.prefilled && state.epoch==epoch && state.generation==generation) {
                pending[field]={true,epoch,generation,GetTickCount64(),GetCurrentThreadId(),text}; queued=true;
            }
            ReleaseSRWLockExclusive(&state_lock);
            if(queued) diagnostic(id,"enter-copied-no-Apply",field,epoch,generation,&text,GetCurrentThreadId());
        } else diagnostic(observation_id.fetch_add(1)+1,"enter-post-read-rejected",field,epoch,generation);
    }
    return result;
}

void bridge_rejected(unsigned long id,const char* reason) noexcept {
    char line[256]{};
    std::snprintf(line,sizeof(line),"RN_IEBR id=%lu guard=%s; no retry\r\n",id,reason);
    write_log(line);
}

bool own_commit(void* context,const MsvcString* selector,const MsvcString*& model_name,std::size_t& index) noexcept {
    std::uintptr_t context_vt=0,model_vt=0; void* model=nullptr;
    if(!member(context,0,context_vt) || context_vt!=image_base+0x294E568 ||
       !member(context,0x20,model) || !member(model,0,model_vt) || model_vt!=image_base+0x29DB558) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(model);
    if(address>(std::numeric_limits<std::uintptr_t>::max)()-8) return false;
    model_name=reinterpret_cast<const MsvcString*>(address+8);
    for(std::size_t i=0;i<model_count;++i) {
        if(name_equals(model_name,models[i].name) && name_equals(selector,models[i].apply)) { index=i; return true; }
    }
    return false;
}

bool action_view(void* context,std::size_t model,View& view,unsigned long id) noexcept {
    void* window=nullptr;
    if(!member(context,0x10,window)) { bridge_rejected(id,"context-window"); return false; }
    const auto address=reinterpret_cast<std::uintptr_t>(window);
    if(!address || address>(std::numeric_limits<std::uintptr_t>::max)()-0x110 ||
       reinterpret_cast<std::uintptr_t>(context)!=address+0x110 ||
       inspect_window(window,view)!=ViewResult::visible || view.model!=model) {
        bridge_rejected(id,"fresh-own-action-view"); return false;
    }
    return true;
}

bool ready_model(std::size_t model,std::uint64_t epoch,std::uint64_t& generation,bool compare_generation) noexcept {
    if(model>=model_count) return false;
    AcquireSRWLockShared(&state_lock);
    const auto& state=lifecycle[model];
    const bool ready=state.visible && state.attempted && state.prefilled && state.epoch==epoch &&
        (!compare_generation || state.generation==generation);
    if(ready && !compare_generation) generation=state.generation;
    ReleaseSRWLockShared(&state_lock);
    return ready;
}

bool capture_fields(Origin& current,InputSnapshot (&snapshots)[2]) noexcept {
    View view{};
    if(!action_view(current.context,current.model,view,current.id)) return false;
    if(!read_epoch(view.root,current.model,current.epoch) ||
       !ready_model(current.model,current.epoch,current.generation,false)) {
        bridge_rejected(current.id,"opening-epoch-prefill-state"); return false;
    }
    const auto& model=models[current.model];
    for(std::size_t j=0;j<model.count;++j) {
        const auto field=model.first+j;
        void* edit=guarded_find(find_edit,view.root,fields[field].edit);
        if(!edit_named(edit,fields[field].edit) || !visible_edit(edit) || !snapshot_edit(edit,fields[field].edit,snapshots[j])) {
            bridge_rejected(current.id,"own-edit-identity-visibility-text"); return false;
        }
        // A button click may release focus. Both fields belong to the same fresh View/epoch.
        diagnostic(current.id,"button-field-snapshot",field,current.epoch,current.generation,&snapshots[j],GetCurrentThreadId());
    }
    return true;
}

// Only this project's country DLL is trusted. Query afresh; never save capability.
// Loading a module without successful initialization yields zero and rejects dispatch.
bool country_ready() noexcept {
    using Query=DWORD(WINAPI*)();
    const auto module=GetModuleHandleW(L"rename_anything_country.dll");
    if(!module) return false;
    const auto query=reinterpret_cast<Query>(GetProcAddress(module,"RNCountryCapabilities"));
    if(!query) return false;
    __try { return query()==rn::country_capabilities_v1; }
    __except(memory_exception(GetExceptionCode())) { return false; }
}

bool still_current(const Origin& current,const InputSnapshot (&snapshots)[2]) noexcept {
    View view{}; std::uint64_t epoch=0,generation=current.generation;
    if(!action_view(current.context,current.model,view,current.id) || !read_epoch(view.root,current.model,epoch) ||
       epoch!=current.epoch || !ready_model(current.model,epoch,generation,true)) {
        bridge_rejected(current.id,"pre-dispatch-view-epoch-generation-changed"); return false;
    }
    const auto& model=models[current.model];
    for(std::size_t j=0;j<model.count;++j) {
        const auto field=model.first+j;
        void* edit=guarded_find(find_edit,view.root,fields[field].edit); InputSnapshot fresh{};
        if(!edit_named(edit,fields[field].edit) || !visible_edit(edit) ||
           !snapshot_edit(edit,fields[field].edit,fresh) || !same_text(snapshots[j],fresh)) {
            bridge_rejected(current.id,"pre-dispatch-edit-text-changed"); return false;
        }
    }
    // Re-read after the text snapshot as well; lifecycle locks never cross native dispatch.
    std::uint64_t after=0;
    if(!read_epoch(view.root,current.model,after) || after!=epoch || !ready_model(current.model,epoch,generation,true)) {
        bridge_rejected(current.id,"pre-dispatch-epoch-generation-changed"); return false;
    }
    if(current.model==country_model && !country_ready()) {
        bridge_rejected(current.id,"country-hooks-not-active-at-dispatch"); return false;
    }
    return true;
}

bool record_key(const Record& record,char (&out)[129]) noexcept {
    const auto& key=record.key;
    if(key.size>128 || key.size>key.capacity || key.capacity>1048576) return false;
    const void* data=key.capacity<=15?static_cast<const void*>(key.data.small):static_cast<const void*>(key.data.pointer);
    return guarded_copy(out,data,static_cast<std::size_t>(key.size)+1) && out[key.size]=='\0' &&
        std::memchr(out,'\0',static_cast<std::size_t>(key.size))==nullptr;
}

bool reserved_key(const char* key) noexcept {
    if(std::strcmp(key,"rn_native_ok")==0 || std::strcmp(key,"rn_native_len")==0 ||
       std::strcmp(key,"rn_nativeb_ok")==0 || std::strcmp(key,"rn_nativeb_len")==0 ||
       std::strcmp(key,"rn_native_cap")==0 || std::strcmp(key,command_epoch_key)==0) return true;
    for(unsigned i=0;i<32;++i) {
        char expected[16]{}; std::snprintf(expected,sizeof(expected),"rn_native_%u",i);
        if(std::strcmp(key,expected)==0) return true;
        std::snprintf(expected,sizeof(expected),"rn_nativeb_%u",i);
        if(std::strcmp(key,expected)==0) return true;
    }
    return false;
}

bool transport_record(Record& record,const char* key,std::uint64_t value) noexcept {
    const auto size=std::strlen(key);
    if(size>15 || value>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/fixed_point)) return false;
    record={};
    if(size) std::memcpy(record.key.data.small,key,size);
    record.key.size=size; record.key.capacity=15;
    record.raw_value=static_cast<std::int64_t>(value)*fixed_point;
    return true;
}

bool prepare_transport(Origin& current,const InputSnapshot (&snapshots)[2]) noexcept {
    if(current.model>=model_count) { bridge_rejected(current.id,"unknown-own-model"); return false; }
    const auto context_address=reinterpret_cast<std::uintptr_t>(current.context);
    if(context_address>(std::numeric_limits<std::uintptr_t>::max)()-0x470 || current.epoch==0 ||
       current.epoch>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/fixed_point)) {
        bridge_rejected(current.id,"scope-or-fixed-point-epoch"); return false;
    }
    if(current.model==country_model && !country_ready()) {
        bridge_rejected(current.id,"country-hooks-not-active"); return false;
    }
    current.scope=reinterpret_cast<const void*>(context_address+0x3C0);
    current.source=reinterpret_cast<const TempVector*>(context_address+0x470);
    TempVector source{};
    if(!guarded_copy(&source,current.source,sizeof(source)) || source.count<0 ||
       source.count>static_cast<int>(max_original_records) || source.capacity<source.count || source.capacity>65536 ||
       !readable(source.data,static_cast<std::size_t>(source.count)*sizeof(Record))) {
        bridge_rejected(current.id,"source-vector"); return false;
    }
    std::size_t kept=0,sanitized=0;
    for(int i=0;i<source.count;++i) {
        Record record{}; char key[129]{};
        if(!guarded_copy(&record,source.data+i,sizeof(record)) || !record_key(record,key)) {
            bridge_rejected(current.id,"source-record-or-key"); return false;
        }
        if(reserved_key(key)) { ++sanitized; continue; }
        current.records[kept++]=record; // Borrowed native key bytes; no plugin free/dtor.
    }
    const auto base_count=kept;
    const auto& model=models[current.model];
    for(std::size_t j=0;j<model.count;++j) {
        char32_t input[max_codepoints]{};
        for(std::size_t i=0;i<snapshots[j].size;++i) input[i]=static_cast<char32_t>(snapshots[j].points[i]);
        const auto encoded=rn::editor::encode_field(std::u32string_view(input,snapshots[j].size));
        rn::editor::FieldCodes field{};
        if(encoded.value) field=*encoded.value;
        const char* prefix=j==0?"rn_native":"rn_nativeb";
        char key[16]{};
        std::snprintf(key,sizeof(key),"%s_ok",prefix);
        if(!transport_record(current.records[kept++],key,encoded.value?1u:0u)) return false;
        std::snprintf(key,sizeof(key),"%s_len",prefix);
        if(!transport_record(current.records[kept++],key,field.length)) return false;
        for(unsigned i=0;i<32;++i) {
            std::snprintf(key,sizeof(key),"%s_%u",prefix,i);
            if(!transport_record(current.records[kept++],key,field.codes[i])) return false;
        }
        char line[512]{};
        std::snprintf(line,sizeof(line),
            "RN_IEBR id=%lu field=%zu prepared ok=%u len=%zu input_error=%u error_index=%zu; diagnostic transport\r\n",
            current.id,j+1,encoded.value?1u:0u,field.length,static_cast<unsigned>(encoded.error),encoded.error_index);
        write_log(line);
    }
    if(!transport_record(current.records[kept++],command_epoch_key,current.epoch)) return false;
    if(current.model==country_model && !transport_record(current.records[kept++],"rn_native_cap",1)) return false;
    const auto transport_records=model.count*34+1+(current.model==country_model?1u:0u);
    if(transport_records>max_transport_records || kept!=base_count+transport_records) return false;
    current.view={current.records,static_cast<std::int32_t>(kept),static_cast<std::int32_t>(kept),source.allocator};
    char line[512]{};
    std::snprintf(line,sizeof(line),
        "RN_IEBR id=%lu model=%s "
        "epoch=%llu epoch_raw=%lld generation=%llu original=%d kept=%zu sanitized=%zu appended=%zu total=%d; diagnostic transport\r\n",
        current.id,model.name,
        static_cast<unsigned long long>(current.epoch),static_cast<long long>(current.epoch*fixed_point),
        static_cast<unsigned long long>(current.generation),source.count,base_count,sanitized,transport_records,current.view.count);
    write_log(line);
    return true;
}

void* __fastcall constructor_bridged(void* self,const MsvcString* gui,const MsvcString* selector,
                                    const void* scope,const TempVector* source) {
    Origin* current=origin;
    if(!active.load(std::memory_order_acquire) || !current || gui!=current->model_name ||
       selector!=current->selector || scope!=current->scope || source!=current->source ||
       current->model>=model_count || !name_equals(gui,models[current->model].name) ||
       !name_equals(selector,models[current->model].apply)) {
        return original_ctor(self,gui,selector,scope,source);
    }
    ++current->ctor_calls;
    // Native constructor deep-copies the borrowed POD view. Exactly one call, no
    // cancellation/retry or engine-pointer lifetime extension after it starts.
    void* result=original_ctor(self,gui,selector,scope,&current->view);
    TempVector copied{};
    const bool independent=member(result,0x28,copied) && copied.count==current->view.count &&
        copied.data!=current->view.data && readable(copied.data,static_cast<std::size_t>(copied.count)*sizeof(Record));
    char line[256]{};
    std::snprintf(line,sizeof(line),
        "RN_IEBR id=%lu ctor_calls=%u result_is_this=%u independent_record_storage=%u records=%d\r\n",
        current->id,current->ctor_calls,result==self?1u:0u,independent?1u:0u,copied.count);
    write_log(line);
    return result; // Preserve native result even when post-ctor diagnostics fail.
}

struct DebugAction { const char* model; const char* selector; const char* action; };
constexpr DebugAction debug_actions[]={
    {"rn_ip_leader_launcher_gui","rn_ip_leader_open_click","open"},
    {"rn_ip_leader_gui","rn_ip_leader_apply_click","apply"},
    {"rn_ip_leader_gui","rn_ip_leader_cancel_click","cancel"},
    {"rn_ip_party_neutrality_launcher_gui","rn_ip_party_neutrality_open_click","open"},
    {"rn_ip_party_neutrality_gui","rn_ip_party_neutrality_apply_click","apply"},
    {"rn_ip_party_neutrality_gui","rn_ip_party_neutrality_cancel_click","cancel"},
    {"rn_ip_country_launcher_gui","rn_ip_country_open_click","open"},
    {"rn_ip_country_gui","rn_ip_country_apply_click","apply"},
    {"rn_ip_country_gui","rn_ip_country_cancel_click","cancel"},
    {"rn_ilqa_guard_gui","rn_ilqa_guards_click","qa_guards"}
};
void debug_own_action(void* context,const MsvcString* selector) noexcept {
    std::uintptr_t context_vt=0,model_vt=0; void* model=nullptr;
    if(!member(context,0,context_vt) || context_vt!=image_base+0x294E568 ||
       !member(context,0x20,model) || !member(model,0,model_vt) || model_vt!=image_base+0x29DB558) return;
    const auto model_address=reinterpret_cast<std::uintptr_t>(model);
    if(model_address>(std::numeric_limits<std::uintptr_t>::max)()-8) return;
    for(const auto& action:debug_actions) {
        if(!name_equals(reinterpret_cast<void*>(model_address+8),action.model) || !name_equals(selector,action.selector)) continue;
        void* window=nullptr; void* window_model=nullptr; void* root=nullptr; void* context_root=nullptr;
        std::uintptr_t root_vt=0; unsigned char flags=0,desired=0;
        const bool has_window=member(context,0x10,window);
        const auto window_address=reinterpret_cast<std::uintptr_t>(window);
        const bool valid=has_window && window_address && window_address<=(std::numeric_limits<std::uintptr_t>::max)()-0x110 &&
            reinterpret_cast<std::uintptr_t>(context)==window_address+0x110 &&
            member(window,0xE8,window_model) && window_model==model && member(context,0x28,context_root) &&
            member(window,0xF8,root) && root==context_root && member(root,0,root_vt) && root_vt==image_base+0x2B45278 &&
            member(root,0xA5,flags) && member(window,0xB79,desired) && readable(root,0x1360) &&
            bounded_list(root,0x1240) && bounded_list(root,0x11E0) && bounded_list(root,0x1330);
        char line[512]{};
        std::snprintf(line,sizeof(line),
            "RN_DEBUG stage=pre_dispatch model=%s selector=%s action=%s reason=%s visible=%u "
            "desired=%u root_flags=0x%02X; recognized selector, execution and effect not yet observed\r\n",
            action.model,action.selector,action.action,valid?"fresh_context":"context_guard_failed",
            valid && desired && (flags&8) && !(flags&(4|16))?1u:0u,static_cast<unsigned>(desired),static_cast<unsigned>(flags));
        write_log(line); return;
    }
}
struct ExecuteObservation { bool own{}; std::size_t model{}; unsigned long id{}; };
ExecuteObservation debug_execute_command(void* command) noexcept {
    ExecuteObservation observation{}; std::uintptr_t vt=0;
    if(!member(command,0,vt) || vt!=image_base+0x2996ED0) return observation;
    const auto address=reinterpret_cast<std::uintptr_t>(command);
    if(address>(std::numeric_limits<std::uintptr_t>::max)()-0x78) return observation;
    for(std::size_t model=0;model<model_count;++model) {
        if(!name_equals(reinterpret_cast<void*>(address+0x78),models[model].name) ||
           !name_equals(reinterpret_cast<void*>(address+0x58),models[model].apply)) continue;
        observation={true,model,observation_id.fetch_add(1)+1};
        TempVector vector{}; int scopes=0,scope_capacity=0;
        const bool scope_read=member(command,0x4C,scopes) && member(command,0x48,scope_capacity) &&
            scopes>=0 && scopes<=4096 && scope_capacity>=scopes && scope_capacity<=65536;
        const bool vector_read=member(command,0x28,vector) && vector.count>=0 &&
            vector.count<=static_cast<int>(max_original_records+max_transport_records) && vector.capacity>=vector.count &&
            vector.capacity<=65536 && readable(vector.data,static_cast<std::size_t>(vector.count)*sizeof(Record));
        unsigned header_mask=0; std::int64_t epoch=0,a_ok=0,a_len=0,b_ok=0,b_len=0,capability=0;
        bool records_valid=vector_read;
        if(vector_read) for(int i=0;i<vector.count;++i) {
            Record record{}; char key[129]{};
            if(!guarded_copy(&record,vector.data+i,sizeof(record)) || !record_key(record,key)) { records_valid=false; break; }
            if(std::strcmp(key,command_epoch_key)==0) { header_mask|=1; epoch=record.raw_value; }
            else if(std::strcmp(key,"rn_native_ok")==0) { header_mask|=2; a_ok=record.raw_value; }
            else if(std::strcmp(key,"rn_native_len")==0) { header_mask|=4; a_len=record.raw_value; }
            else if(std::strcmp(key,"rn_nativeb_ok")==0) { header_mask|=8; b_ok=record.raw_value; }
            else if(std::strcmp(key,"rn_nativeb_len")==0) { header_mask|=16; b_len=record.raw_value; }
            else if(std::strcmp(key,"rn_native_cap")==0) { header_mask|=32; capability=record.raw_value; }
        }
        char line[1024]{};
        std::snprintf(line,sizeof(line),
            "RN_DEBUG stage=pre_execute id=%lu model=%s selector=%s reason=%s records=%d scopes_read=%u scopes=%d "
            "header_mask=%u epoch_raw=%lld a_ok_raw=%lld a_len_raw=%lld b_ok_raw=%lld b_len_raw=%lld capability_raw=%lld "
            "fixed_point=%lld; queue reached Execute, target authority and effect success unverified\r\n",
            observation.id,models[model].name,models[model].apply,records_valid?"copied_headers":"record_guard_failed",
            vector.count,scope_read?1u:0u,scopes,header_mask,static_cast<long long>(epoch),static_cast<long long>(a_ok),
            static_cast<long long>(a_len),static_cast<long long>(b_ok),static_cast<long long>(b_len),
            static_cast<long long>(capability),static_cast<long long>(fixed_point));
        write_log(line); break;
    }
    return observation;
}
void __fastcall execute_observed(void* command) {
    ExecuteObservation copied{};
    if(active.load(std::memory_order_acquire)) copied=debug_execute_command(command);
    original_execute(command); // Always once, including foreign/malformed commands.
    // The queue may destroy command. Only our copied scalars are used here.
    if(copied.own) {
        char line[256]{};
        std::snprintf(line,sizeof(line),"RN_DEBUG stage=execute_return id=%lu model=%s effect_success=unverified; void native returned\r\n",
            copied.id,models[copied.model].name);
        write_log(line);
    }
}
void __fastcall dispatch_observed(void* context,void* opaque_selector) {
    const auto* selector=static_cast<const MsvcString*>(opaque_selector);
    if(active.load(std::memory_order_acquire)) debug_own_action(context,selector);
    const MsvcString* model_name=nullptr; std::size_t model=model_count;
    if(!active.load(std::memory_order_acquire) || !own_commit(context,selector,model_name,model)) {
        original_dispatch(context,opaque_selector); return;
    }
    Origin current{};
    current.context=context; current.model_name=model_name; current.selector=selector; current.model=model;
    current.id=observation_id.fetch_add(1)+1;
    InputSnapshot snapshots[2]{};
    if(!original_ctor || !capture_fields(current,snapshots) || !prepare_transport(current,snapshots) ||
       !still_current(current,snapshots)) {
        bridge_rejected(current.id,"own-action-dropped-before-dispatch"); return;
    }
    {
        OriginFrame frame(&current);
        original_dispatch(context,opaque_selector);
    }
    if(current.ctor_calls!=1) bridge_rejected(current.id,"unexpected-ctor-count-no-retry");
}

template<std::size_t N> bool unique_at(const unsigned char* base, const IMAGE_SECTION_HEADER* text,
                                     std::size_t rva, const unsigned char (&bytes)[N]) noexcept {
    if (rva < text->VirtualAddress || rva - text->VirtualAddress + N > text->Misc.VirtualSize) return false;
    const auto* data = base + text->VirtualAddress;
    std::size_t count = 0;
    for (std::size_t i = 0; i + N <= text->Misc.VirtualSize; ++i) {
        if (std::memcmp(data + i, bytes, N) == 0) {
            if (i + text->VirtualAddress != rva || ++count > 1) return false;
        }
    }
    return count == 1;
}

} // namespace
// Suitable for the fixed development controller. This remote-thread entry point
// queues scalars only: no engine reads, file I/O, focus, SetText or dispatch.
extern "C" __declspec(dllexport) DWORD WINAPI RNDebugRequest(void* argument) {
    if(!active.load(std::memory_order_acquire)) return 41;
    const auto code=reinterpret_cast<std::uintptr_t>(argument);
    if(code<1 || code>4) return 42;
    const auto operation=static_cast<rn::debug::Operation>(code);
    AcquireSRWLockExclusive(&debug_request_lock);
    if(!debug_control.request(operation,GetTickCount64())) {
        ReleaseSRWLockExclusive(&debug_request_lock); return 43;
    }
    if(operation==rn::debug::Operation::pause) debug_ring.arm(false);
    else if(operation==rn::debug::Operation::resume) debug_ring.arm(true);
    ReleaseSRWLockExclusive(&debug_request_lock);
    return 0; // Queued. Completion appears only in the post-Tick log.
}
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    unsigned expected = 0;
    if (!init_state.compare_exchange_strong(expected, 1)) return active.load() ? 0 : 33;
    try {
        // Unique logger; frozen predecessors cannot know this new basename.
        // Always use one variant in a fresh process, never inverse later co-load.
        const auto same_name=GetModuleHandleW(L"rename_anything_inline_editor_debug_bridge.dll");
        if (same_name!=own_module) return 40;
        if (GetModuleHandleW(L"rename_anything_inline_editor_key_logger_v2.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_key_logger.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_editor_focus_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_editor_observer.dll") ||
            GetModuleHandleW(L"rename_anything_editor_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_prefill.dll") ||
            GetModuleHandleW(L"rename_anything_inline_leader_bridge.dll")) return 40;
        wchar_t executable[32768]{};
        if (!GetModuleFileNameW(nullptr, executable, 32768) ||
            rn::sha256_file(executable) != rn::exe_sha256) return 10;
        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!readable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew <= 0 || dos->e_lfanew > 65536) return 11;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.NumberOfSections > 96) return 11;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (!readable(sections, nt->FileHeader.NumberOfSections * sizeof(*sections))) return 11;
        const IMAGE_SECTION_HEADER* text = nullptr;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (std::memcmp(sections[i].Name, ".text\0\0\0", 8) == 0) text = sections + i;
        if (!text || text->VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            text->Misc.VirtualSize > nt->OptionalHeader.SizeOfImage - text->VirtualAddress ||
            !readable(base + text->VirtualAddress, text->Misc.VirtualSize)) return 12;
        constexpr unsigned char tick_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x20,0x01,0x00,0x00,0x44,0x0f,0xb6,0xf2,0x48,0x8b,0xd9,0x48,0x8d,0x51,0x28,0x33,0xff,0x48,0x85,0xc9,0x48,0x0f,0x44,0xd7,0x41,0xb9,0x02,0x00,0x00,0x00};
        // Exact pinned EXE: B86320 void(window*), fresh-create and rebind callers.
        constexpr unsigned char bind_bytes[] = {
            0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57,
            0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0xf9,0xc7,
            0x40,0x08,0x00,0x00,0x00,0x00,0x48,0x8d,0xb1,0xe8,0x00,0x00,0x00,0x4c,0x8b,0x2e,
            0x41,0x8b,0x45,0x2c};
        constexpr unsigned char key_bytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xda,0x48,0x8b,0xf9,0x48,0x8b,0xca,0xe8,0xc6,0x22,0xf8,0xff};
        constexpr unsigned char find_edit_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,
            0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,
            0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0x40,0x12,0x00,0x00,0x48,0x8d,0x91,0x58,0x12,
            0x00,0x00,0x49,0x8b,0xcf};
        constexpr unsigned char find_instant_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0xe0,0x11,0x00,0x00};
        constexpr unsigned char set_text_bytes[] = {0x48,0x83,0x7a,0x18,0x0f,0x76,0x03,0x48,0x8b,0x12,0xe9,0x01,0x00,0x00,0x00,0xcc,0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x0f,0x57,0xc0,0x0f,0x11,0x44,0x24,0x20,0x66,0x0f,0x6f,0x0d,0xc3,0xf6,0x84,0x00};
        // Native void(CEditBox*) refresh; complete 55-byte prefix ends at 22F4DB7.
        constexpr unsigned char refresh_edit_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,
            0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec,0xd0,0x00,0x00,0x00,
            0x48,0x8b,0xf1,0x45,0x33,0xff,0x44,0x89,0x7d,0x67,0x4c,0x39,0xb9,0xb0,0x01,0x00,
            0x00,0x0f,0x84,0xbd,0x03,0x00,0x00
        };
        static_assert(sizeof(refresh_edit_bytes)==55);
        constexpr unsigned char wrapper_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xea,0x48,0x8b,0xf1,0xf6,0x81,0x71,0x02,0x00,0x00,0x02,0x0f,0x85,0xc4,0x01,0x00,0x00,0x48,0x8b,0xca,0xe8,0x89,0x15,0xf8,0xff,0x85,0xc0};
        static_assert(sizeof(wrapper_bytes)==41);
        constexpr unsigned char dispatch_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
            0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x4c,0x8b,0xfa,0x48,0x8b,0xf9,0x48,0x8d,
            0x99,0xc0,0x03,0x00,0x00,0x45,0x33,0xc0};
        constexpr unsigned char ctor_bytes[] = {
            0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,
            0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x0f,0x29,0x74,0x24,0x50,0x4d,0x8b,
            0xf1,0x49,0x8b,0xf8,0x48,0x8b,0xf2,0x4c,0x8b,0xe1};
        // REA / retained read-only decompilation: CExecuteScriptedWindowEffect,
        // vtable2996ED0 slot50, void(command*), whole-instruction 50-byte prefix.
        constexpr unsigned char execute_bytes[]={
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
            0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x60,0x4C,0x8B,0xF1,0xE8,0x40,0xC2,
            0x95,0xFF,0x49,0x8D,0x56,0x78,0x48,0x8B,0xC8,0xE8,0x74,0x2E,0x96,0xFF,0x4C,
            0x8B,0xF8,0x48,0x85,0xC0};
        static_assert(sizeof(execute_bytes)==50);
        if (!unique_at(base, text, bind_rva, bind_bytes) ||
            !unique_at(base, text, tick_rva, tick_bytes) || !unique_at(base, text, key_rva, key_bytes) ||
            !unique_at(base, text, find_edit_rva, find_edit_bytes) ||
            !unique_at(base, text, find_instant_rva, find_instant_bytes) ||
            !unique_at(base, text, set_text_rva, set_text_bytes) ||
            !unique_at(base, text, refresh_edit_rva, refresh_edit_bytes) ||
            !unique_at(base, text, wrapper_rva, wrapper_bytes) ||
            !unique_at(base, text, dispatch_rva, dispatch_bytes) ||
            !unique_at(base, text, ctor_rva, ctor_bytes) || !unique_at(base,text,execute_rva,execute_bytes)) return 20;
        wchar_t log_path[32768]{};
        if (!GetModuleFileNameW(own_module, log_path, 32768)) return 13;
        auto* slash = std::wcsrchr(log_path, L'\\');
        if (!slash || wcscpy_s(slash + 1, 32768 - static_cast<std::size_t>(slash + 1 - log_path),
                              L"inline_editor_debug_bridge.log") != 0) return 13;
        log_file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_file == INVALID_HANDLE_VALUE) return 13;
        image_base = reinterpret_cast<std::uintptr_t>(base);
        find_edit = reinterpret_cast<FindWidget>(image_base + find_edit_rva);
        find_instant = reinterpret_cast<FindWidget>(image_base + find_instant_rva);
        set_text = reinterpret_cast<SetText>(image_base + set_text_rva);
        refresh_edit = reinterpret_cast<RefreshEdit>(image_base + refresh_edit_rva);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&RNInitialize), &pinned)) return 14;
        if (MH_Initialize() != MH_OK) return 30;
        auto* dispatch_target = const_cast<unsigned char*>(base) + dispatch_rva;
        auto* ctor_target = const_cast<unsigned char*>(base) + ctor_rva;
        auto* execute_target = const_cast<unsigned char*>(base) + execute_rva;
        auto* bind_target = const_cast<unsigned char*>(base) + bind_rva;
        auto* tick_target = const_cast<unsigned char*>(base) + tick_rva;
        auto* key_target = const_cast<unsigned char*>(base) + key_rva;
        auto* wrapper_target = const_cast<unsigned char*>(base) + wrapper_rva;
        if (MH_CreateHook(execute_target,reinterpret_cast<void*>(&execute_observed),
                          reinterpret_cast<void**>(&original_execute))!=MH_OK ||
            MH_CreateHook(bind_target, reinterpret_cast<void*>(&bind_observed),
                          reinterpret_cast<void**>(&original_bind)) != MH_OK ||
            MH_CreateHook(tick_target, reinterpret_cast<void*>(&tick_observed),
                          reinterpret_cast<void**>(&original_tick)) != MH_OK ||
            MH_CreateHook(wrapper_target, reinterpret_cast<void*>(&wrapper_observed),
                          reinterpret_cast<void**>(&original_wrapper)) != MH_OK ||
            MH_CreateHook(key_target, reinterpret_cast<void*>(&key_observed),
                          reinterpret_cast<void**>(&original_key)) != MH_OK ||
            MH_CreateHook(dispatch_target, reinterpret_cast<void*>(&dispatch_observed),
                          reinterpret_cast<void**>(&original_dispatch)) != MH_OK ||
            MH_CreateHook(ctor_target, reinterpret_cast<void*>(&constructor_bridged),
                          reinterpret_cast<void**>(&original_ctor)) != MH_OK) {
            MH_Uninitialize(); return 31;
        }
        if (MH_EnableHook(execute_target)!=MH_OK || MH_EnableHook(bind_target) != MH_OK ||
            MH_EnableHook(ctor_target) != MH_OK || MH_EnableHook(dispatch_target) != MH_OK ||
            MH_EnableHook(wrapper_target) != MH_OK ||
            MH_EnableHook(key_target) != MH_OK || MH_EnableHook(tick_target) != MH_OK) {
            // No integrated action was enabled. Preserve trampolines that
            // could have an in-flight forwarding caller; restart after failure.
            MH_DisableHook(bind_target);
            MH_DisableHook(dispatch_target);
            MH_DisableHook(ctor_target);
            MH_DisableHook(tick_target);
            MH_DisableHook(key_target);
            MH_DisableHook(wrapper_target);
            MH_DisableHook(execute_target);
            write_log("RN_IEBR initialization failed; all actions disabled; restart required\r\n");
            return 32;
        }
        active.store(true, std::memory_order_release);
        write_log("RN_DEBUG initialized format=1 ring=256 ttl_ms=10000 models=7 selectors=10 fields=5; exact image + 11 unique fingerprints; 7 hooks; own copied history, scalar remote requests, fresh post-Tick snapshots, observed Execute; dispatch is not execution and Execute is not effect success; no auto-Apply/focus/Escape\r\n");
        return 0;
    } catch (...) { return 99; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) own_module = instance;
    return TRUE;
}
