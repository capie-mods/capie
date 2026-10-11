// Isolated inline prefill + leader command-payload DIAGNOSTIC. No automatic Apply.
// Enter is copied diagnostic only; ordinary leader button dispatch is the sole transport origin.
// Exact pinned image only; build into a separate output directory.
#include "../src/image_guard.hpp"
#include "../src/editor_input.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <type_traits>
namespace {
constexpr std::uintptr_t tick_rva=0xB88950, key_rva=0x22F1030;
constexpr std::uintptr_t dispatch_rva=0xB867D0, ctor_rva=0x1144840;
constexpr std::size_t max_original_records=256, transport_records=35;
constexpr std::int64_t fixed_point=100000;
constexpr char command_epoch_key[]="rn_ip_cmd_epoch";
static_assert(sizeof(command_epoch_key)-1==15);
constexpr std::uintptr_t find_edit_rva=0x22BAD40, find_instant_rva=0x22BC600, set_text_rva=0x22F27C0;
constexpr std::size_t max_codepoints=128, max_utf8=max_codepoints*4;
struct MsvcString {
    union { unsigned char small[16]; const char* pointer; } data;
    std::uint64_t size, capacity;
};
static_assert(sizeof(MsvcString)==0x20 && offsetof(MsvcString,size)==0x10 && offsetof(MsvcString,capacity)==0x18);
using Tick=void(__fastcall*)(void*,char);
using HandleKey=std::uint64_t(__fastcall*)(void*,void*);
using FindWidget=void*(__fastcall*)(void*,const MsvcString*,bool,bool);
using SetText=void(__fastcall*)(void*,const MsvcString*);
using Dispatch=void(__fastcall*)(void*,void*);
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
struct Model { const char* name; const char* epoch; std::size_t first; std::size_t count; };
constexpr Field fields[]={
    {"rn_ip_leader_edit","rn_ip_leader_prefill",0},
    {"rn_ip_party_neutrality_edit","rn_ip_party_neutrality_prefill",1},
    {"rn_ip_party_neutrality_long_edit","rn_ip_party_neutrality_long_prefill",1}
};
constexpr Model models[]={
    {"rn_ip_leader_gui","rn_ip_leader_epoch",0,1},
    {"rn_ip_party_neutrality_gui","rn_ip_party_neutrality_epoch",1,2}
};
constexpr std::size_t field_count=sizeof(fields)/sizeof(fields[0]), model_count=sizeof(models)/sizeof(models[0]);
static_assert(field_count==3 && model_count==2 && models[0].count<=2 && models[1].count<=2);
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
    Record records[max_original_records+transport_records]{};
    TempVector view{};
    unsigned ctor_calls{};
    unsigned long id{};
    std::uint64_t epoch{},generation{};
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
HandleKey original_key=nullptr;
FindWidget find_edit=nullptr, find_instant=nullptr;
SetText set_text=nullptr;
Dispatch original_dispatch=nullptr;
Constructor original_ctor=nullptr;
HANDLE log_file=INVALID_HANDLE_VALUE;
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

void write_log(const char* text) noexcept {
    if (log_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(log_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
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
        if(state.visible || state.attempted || state.prefilled) ++state.generation;
        state.visible=false; state.attempted=false; state.prefilled=false;
    } else {
        // Unreadable epoch suspends the same opening; recovery must not reset a draft.
        state.visible=true; state.attempted=true; state.prefilled=false; ++state.generation;
    }
    for(std::size_t i=models[model].first;i<models[model].first+models[model].count;++i) pending[i]={};
    ReleaseSRWLockExclusive(&state_lock);
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
void prefill_open(const View& view,std::uint64_t epoch,std::uint64_t generation,unsigned long id) {
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
        reset_model(view.model,true); return;
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
    if(!state.visible || state.epoch!=epoch) {
        state.visible=true; state.attempted=false; state.prefilled=false; state.epoch=epoch; ++state.generation;
        for(std::size_t i=models[view.model].first;i<models[view.model].first+models[view.model].count;++i) pending[i]={};
    }
    generation=state.generation;
    if(!state.attempted) { state.attempted=true; attempt=true; }
    ReleaseSRWLockExclusive(&state_lock);
    const auto id=observation_id.fetch_add(1)+1;
    if(attempt) prefill_open(view,epoch,generation,id);
    consume_diagnostic(view,epoch,generation,id);
}
void __fastcall tick_observed(void* window,char force) {
    original_tick(window,force);
    if(active.load(std::memory_order_acquire)) observe_tick(window);
}
bool enter_event(void* event) noexcept {
    int values[8]{};
    //2273320 active: event[0]!=0 && event[2]!=-1;2272FA0 additionally checks[7].
    return guarded_copy(values,event,sizeof(values)) && values[0]==0x0D && values[2]==0 && values[7]==0;
}
std::uint64_t __fastcall key_observed(void* buffer,void* event) {
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
    std::snprintf(line,sizeof(line),"RN_ILBR id=%lu guard=%s; no retry\r\n",id,reason);
    write_log(line);
}

bool own_commit(void* context,const MsvcString* selector,const MsvcString*& model_name) noexcept {
    std::uintptr_t context_vt=0,model_vt=0; void* model=nullptr;
    if(!member(context,0,context_vt) || context_vt!=image_base+0x294E568 ||
       !member(context,0x20,model) || !member(model,0,model_vt) || model_vt!=image_base+0x29DB558) return false;
    const auto address=reinterpret_cast<std::uintptr_t>(model);
    if(address>(std::numeric_limits<std::uintptr_t>::max)()-8) return false;
    model_name=reinterpret_cast<const MsvcString*>(address+8);
    return name_equals(model_name,"rn_ip_leader_gui") && name_equals(selector,"rn_ip_leader_apply_click");
}

bool leader_view(void* context,View& view,unsigned long id) noexcept {
    void* window=nullptr;
    if(!member(context,0x10,window)) { bridge_rejected(id,"context-window"); return false; }
    const auto address=reinterpret_cast<std::uintptr_t>(window);
    if(!address || address>(std::numeric_limits<std::uintptr_t>::max)()-0x110 ||
       reinterpret_cast<std::uintptr_t>(context)!=address+0x110 ||
       inspect_window(window,view)!=ViewResult::visible || view.model!=0) {
        bridge_rejected(id,"fresh-leader-view"); return false;
    }
    return true;
}

bool ready_leader(std::uint64_t epoch,std::uint64_t& generation,bool compare_generation) noexcept {
    AcquireSRWLockShared(&state_lock);
    const auto& state=lifecycle[0];
    const bool ready=state.visible && state.attempted && state.prefilled && state.epoch==epoch &&
        (!compare_generation || state.generation==generation);
    if(ready && !compare_generation) generation=state.generation;
    ReleaseSRWLockShared(&state_lock);
    return ready;
}

bool capture_leader(Origin& current,InputSnapshot& snapshot) noexcept {
    View view{};
    if(!leader_view(current.context,view,current.id)) return false;
    if(!read_epoch(view.root,0,current.epoch) ||
       !ready_leader(current.epoch,current.generation,false)) {
        bridge_rejected(current.id,"opening-epoch-prefill-state"); return false;
    }
    void* edit=guarded_find(find_edit,view.root,fields[0].edit);
    if(!edit_named(edit,fields[0].edit) || !visible_edit(edit) || !snapshot_edit(edit,fields[0].edit,snapshot)) {
        bridge_rejected(current.id,"leader-edit-identity-visibility-text"); return false;
    }
    // A button click may have released focus. No active-receiver requirement here.
    diagnostic(current.id,"leader-button-snapshot",0,current.epoch,current.generation,&snapshot,GetCurrentThreadId());
    return true;
}

bool still_current(const Origin& current,const InputSnapshot& snapshot) noexcept {
    View view{}; std::uint64_t epoch=0,generation=current.generation;
    if(!leader_view(current.context,view,current.id) || !read_epoch(view.root,0,epoch) ||
       epoch!=current.epoch || !ready_leader(epoch,generation,true)) {
        bridge_rejected(current.id,"pre-dispatch-view-epoch-generation-changed"); return false;
    }
    void* edit=guarded_find(find_edit,view.root,fields[0].edit); InputSnapshot fresh{};
    if(!edit_named(edit,fields[0].edit) || !visible_edit(edit) ||
       !snapshot_edit(edit,fields[0].edit,fresh) || !same_text(snapshot,fresh)) {
        bridge_rejected(current.id,"pre-dispatch-edit-text-changed"); return false;
    }
    // Re-read after the text snapshot as well; lifecycle locks never cross native dispatch.
    std::uint64_t after=0;
    if(!read_epoch(view.root,0,after) || after!=epoch || !ready_leader(epoch,generation,true)) {
        bridge_rejected(current.id,"pre-dispatch-epoch-generation-changed"); return false;
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
       std::strcmp(key,command_epoch_key)==0) return true;
    for(unsigned i=0;i<32;++i) {
        char expected[16]{}; std::snprintf(expected,sizeof(expected),"rn_native_%u",i);
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

bool prepare_transport(Origin& current,const InputSnapshot& snapshot) noexcept {
    const auto context_address=reinterpret_cast<std::uintptr_t>(current.context);
    if(context_address>(std::numeric_limits<std::uintptr_t>::max)()-0x470 || current.epoch==0 ||
       current.epoch>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/fixed_point)) {
        bridge_rejected(current.id,"scope-or-fixed-point-epoch"); return false;
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
    char32_t input[max_codepoints]{};
    for(std::size_t i=0;i<snapshot.size;++i) input[i]=static_cast<char32_t>(snapshot.points[i]);
    const auto encoded=rn::editor::encode_field(std::u32string_view(input,snapshot.size));
    rn::editor::FieldCodes field{};
    if(encoded.value) field=*encoded.value;
    const auto base_count=kept;
    if(!transport_record(current.records[kept++],"rn_native_ok",encoded.value?1u:0u) ||
       !transport_record(current.records[kept++],"rn_native_len",field.length)) return false;
    for(unsigned i=0;i<32;++i) {
        char key[16]{}; std::snprintf(key,sizeof(key),"rn_native_%u",i);
        if(!transport_record(current.records[kept++],key,field.codes[i])) return false;
    }
    if(!transport_record(current.records[kept++],command_epoch_key,current.epoch)) return false;
    current.view={current.records,static_cast<std::int32_t>(kept),static_cast<std::int32_t>(kept),source.allocator};
    char line[512]{};
    std::snprintf(line,sizeof(line),
        "RN_ILBR id=%lu prepared ok=%u len=%zu input_error=%u error_index=%zu "
        "epoch=%llu epoch_raw=%lld generation=%llu original=%d kept=%zu sanitized=%zu appended=%zu total=%d; diagnostic transport\r\n",
        current.id,encoded.value?1u:0u,field.length,static_cast<unsigned>(encoded.error),encoded.error_index,
        static_cast<unsigned long long>(current.epoch),static_cast<long long>(current.records[kept-1].raw_value),
        static_cast<unsigned long long>(current.generation),source.count,base_count,sanitized,transport_records,current.view.count);
    write_log(line);
    return true;
}

void* __fastcall constructor_bridged(void* self,const MsvcString* gui,const MsvcString* selector,
                                    const void* scope,const TempVector* source) {
    Origin* current=origin;
    if(!active.load(std::memory_order_acquire) || !current || gui!=current->model_name ||
       selector!=current->selector || scope!=current->scope || source!=current->source ||
       !name_equals(gui,"rn_ip_leader_gui") || !name_equals(selector,"rn_ip_leader_apply_click")) {
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
        "RN_ILBR id=%lu ctor_calls=%u result_is_this=%u independent_record_storage=%u records=%d\r\n",
        current->id,current->ctor_calls,result==self?1u:0u,independent?1u:0u,copied.count);
    write_log(line);
    return result; // Preserve native result even when post-ctor diagnostics fail.
}

void __fastcall dispatch_observed(void* context,void* opaque_selector) {
    const auto* selector=static_cast<const MsvcString*>(opaque_selector);
    const MsvcString* model_name=nullptr;
    if(!active.load(std::memory_order_acquire) || !own_commit(context,selector,model_name)) {
        original_dispatch(context,opaque_selector); return;
    }
    Origin current{};
    current.context=context; current.model_name=model_name; current.selector=selector;
    current.id=observation_id.fetch_add(1)+1;
    InputSnapshot snapshot{};
    if(!original_ctor || !capture_leader(current,snapshot) || !prepare_transport(current,snapshot) ||
       !still_current(current,snapshot)) {
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
extern "C" __declspec(dllexport) DWORD WINAPI RNInitialize(void*) {
    unsigned expected = 0;
    if (!init_state.compare_exchange_strong(expected, 1)) return active.load() ? 0 : 33;
    try {
        if (GetModuleHandleW(L"rename_anything_editor_observer.dll") ||
            GetModuleHandleW(L"rename_anything_editor_bridge.dll") ||
            GetModuleHandleW(L"rename_anything_inline_prefill.dll")) return 40;
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
        constexpr unsigned char key_bytes[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xda,0x48,0x8b,0xf9,0x48,0x8b,0xca,0xe8,0xc6,0x22,0xf8,0xff};
        constexpr unsigned char find_edit_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,
            0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,
            0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0x40,0x12,0x00,0x00,0x48,0x8d,0x91,0x58,0x12,
            0x00,0x00,0x49,0x8b,0xcf};
        constexpr unsigned char find_instant_bytes[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x45,0x0f,0xb6,0xe1,0x45,0x0f,0xb6,0xf0,0x4c,0x8b,0xfa,0x48,0x8b,0xe9,0x4c,0x8d,0x81,0xe0,0x11,0x00,0x00};
        constexpr unsigned char set_text_bytes[] = {0x48,0x83,0x7a,0x18,0x0f,0x76,0x03,0x48,0x8b,0x12,0xe9,0x01,0x00,0x00,0x00,0xcc,0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xd9,0x0f,0x57,0xc0,0x0f,0x11,0x44,0x24,0x20,0x66,0x0f,0x6f,0x0d,0xc3,0xf6,0x84,0x00};
        constexpr unsigned char dispatch_bytes[] = {
            0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
            0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x4c,0x8b,0xfa,0x48,0x8b,0xf9,0x48,0x8d,
            0x99,0xc0,0x03,0x00,0x00,0x45,0x33,0xc0};
        constexpr unsigned char ctor_bytes[] = {
            0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,
            0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x60,0x0f,0x29,0x74,0x24,0x50,0x4d,0x8b,
            0xf1,0x49,0x8b,0xf8,0x48,0x8b,0xf2,0x4c,0x8b,0xe1};
        if (!unique_at(base, text, tick_rva, tick_bytes) || !unique_at(base, text, key_rva, key_bytes) ||
            !unique_at(base, text, find_edit_rva, find_edit_bytes) ||
            !unique_at(base, text, find_instant_rva, find_instant_bytes) ||
            !unique_at(base, text, set_text_rva, set_text_bytes) ||
            !unique_at(base, text, dispatch_rva, dispatch_bytes) ||
            !unique_at(base, text, ctor_rva, ctor_bytes)) return 20;
        wchar_t log_path[32768]{};
        if (!GetModuleFileNameW(own_module, log_path, 32768)) return 13;
        auto* slash = std::wcsrchr(log_path, L'\\');
        if (!slash || wcscpy_s(slash + 1, 32768 - static_cast<std::size_t>(slash + 1 - log_path),
                              L"inline_leader_bridge.log") != 0) return 13;
        log_file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log_file == INVALID_HANDLE_VALUE) return 13;
        image_base = reinterpret_cast<std::uintptr_t>(base);
        find_edit = reinterpret_cast<FindWidget>(image_base + find_edit_rva);
        find_instant = reinterpret_cast<FindWidget>(image_base + find_instant_rva);
        set_text = reinterpret_cast<SetText>(image_base + set_text_rva);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&RNInitialize), &pinned)) return 14;
        if (MH_Initialize() != MH_OK) return 30;
        auto* dispatch_target = const_cast<unsigned char*>(base) + dispatch_rva;
        auto* ctor_target = const_cast<unsigned char*>(base) + ctor_rva;
        auto* tick_target = const_cast<unsigned char*>(base) + tick_rva;
        auto* key_target = const_cast<unsigned char*>(base) + key_rva;
        if (MH_CreateHook(tick_target, reinterpret_cast<void*>(&tick_observed),
                          reinterpret_cast<void**>(&original_tick)) != MH_OK ||
            MH_CreateHook(key_target, reinterpret_cast<void*>(&key_observed),
                          reinterpret_cast<void**>(&original_key)) != MH_OK ||
            MH_CreateHook(dispatch_target, reinterpret_cast<void*>(&dispatch_observed),
                          reinterpret_cast<void**>(&original_dispatch)) != MH_OK ||
            MH_CreateHook(ctor_target, reinterpret_cast<void*>(&constructor_bridged),
                          reinterpret_cast<void**>(&original_ctor)) != MH_OK) {
            MH_Uninitialize(); return 31;
        }
        if (MH_EnableHook(ctor_target) != MH_OK || MH_EnableHook(dispatch_target) != MH_OK ||
            MH_EnableHook(key_target) != MH_OK || MH_EnableHook(tick_target) != MH_OK) {
            // No integrated action was enabled. Preserve trampolines that
            // could have an in-flight forwarding caller; restart after failure.
            MH_DisableHook(dispatch_target);
            MH_DisableHook(ctor_target);
            MH_DisableHook(tick_target);
            MH_DisableHook(key_target);
            write_log("RN_ILBR initialization failed; all actions disabled; restart required\r\n");
            return 32;
        }
        active.store(true, std::memory_order_release);
        write_log("RN_ILBR initialized: exact image + 7 unique fingerprints; 4 hooks; prefill + Enter diagnostic + leader-button transport; no auto-Apply/focus/Escape\r\n");
        return 0;
    } catch (...) { return 99; }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) own_module = instance;
    return TRUE;
}

