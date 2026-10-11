// Exercises the actual isolated builder without installing hooks or dispatching.
#include "inline_editor_key_logger.cpp"
#include <array>
#include <cstdlib>
#include <string>

namespace {
unsigned checks=0;
void check(bool value,const char* label) {
    ++checks;
    if(!value) { std::fprintf(stderr,"FAIL %s\n",label); std::exit(1); }
}
std::int64_t raw(const Origin& command,const char* key) {
    unsigned matches=0; std::int64_t result=0;
    for(int i=0;i<command.view.count;++i) {
        char name[129]{};
        check(record_key(command.view.data[i],name),"output key readable");
        if(std::strcmp(name,key)==0) { ++matches; result=command.view.data[i].raw_value; }
    }
    check(matches==1,"exactly one output key");
    return result;
}
InputSnapshot text(std::size_t length,std::uint32_t point) {
    InputSnapshot result{}; result.size=length;
    for(std::size_t i=0;i<length;++i) result.points[i]=point;
    return result;
}
struct FakeContext {
    std::array<unsigned char,0x470> prefix{};
    TempVector vector{};
};
static_assert(offsetof(FakeContext,vector)==0x470);
Origin command(FakeContext& context,std::size_t model) {
    Origin result{}; result.context=&context; result.model=model; result.epoch=7;
    return result;
}
template<class T> void put(void* object,std::size_t offset,const T& value) {
    std::memcpy(static_cast<unsigned char*>(object)+offset,&value,sizeof(value));
}
struct LoggerFixture {
    alignas(8) std::array<unsigned char,0xB80> window{};
    alignas(8) std::array<unsigned char,0x1360> root{};
    alignas(8) std::array<unsigned char,0x30> model{};
    alignas(8) std::array<unsigned char,0x238> manager{};
    alignas(8) std::array<std::array<unsigned char,0x280>,field_count> edit{};
    alignas(8) std::array<std::array<unsigned char,0x50>,field_count> definition{};
    alignas(8) std::array<unsigned char,0x80> event{};
    std::uint32_t words[8]={13,0x76543210,0,0xFFFFFFFE,4,5,6,0};
    LoggerFixture() {
        put(model.data(),0,image_base+0x29DB558);
        put(model.data(),8,borrowed_string(models[0].name,std::strlen(models[0].name)));
        put(window.data(),0xE8,static_cast<void*>(model.data()));
        put(window.data(),0xF8,static_cast<void*>(root.data()));
        put(window.data(),0x110,image_base+0x294E568);
        put(window.data(),0x120,static_cast<void*>(window.data()));
        put(window.data(),0x130,static_cast<void*>(model.data()));
        put(window.data(),0x138,static_cast<void*>(root.data()));
        put(window.data(),0xB79,static_cast<unsigned char>(1));
        put(root.data(),0,image_base+0x2B45278);
        put(root.data(),0xA5,static_cast<unsigned char>(8));
        for(std::size_t field=0;field<field_count;++field) {
            put(edit[field].data(),0,image_base+0x2B4A940);
            put(edit[field].data(),0x80,image_base+0x2B4ABD0);
            put(edit[field].data(),0x60,static_cast<void*>(definition[field].data()));
            put(definition[field].data(),0,image_base+0x2B54050);
            put(definition[field].data(),0x28,borrowed_string(fields[field].edit,std::strlen(fields[field].edit)));
            put(edit[field].data(),0x75,static_cast<unsigned char>(8));
            put(edit[field].data(),0x271,static_cast<unsigned char>(1));
            // Text is intentionally unreadable; the added reader must never use it.
            put(edit[field].data(),0xB0,reinterpret_cast<void*>(1));
            put(edit[field].data(),0xC0,std::uint64_t{128});
            put(edit[field].data(),0xC8,std::uint64_t{128});
        }
        std::memcpy(event.data(),words,sizeof(words)); put(event.data(),0x7C,std::uint32_t{1});
        put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(manager.data()));
        focus(0);
        for(auto& state:lifecycle) state={false,false,false,7,11,0};
    }
    void focus(std::size_t field) {
        put(manager.data(),0x158,static_cast<void*>(edit[field].data()));
        put(manager.data(),0x230,static_cast<void*>(edit[field].data()+0x1A0));
    }
};
LoggerFixture* current_fixture=nullptr;
unsigned original_calls=0;
constexpr std::uint64_t opaque_result=0xFEDCBA9876543210ULL;
void* expected_first=nullptr; void* expected_second=nullptr;
std::uint64_t __fastcall fake_original(void* first,void* second) {
    ++original_calls;
    check(first==expected_first && second==expected_second,"original arguments unchanged");
    const bool state_unlocked=TryAcquireSRWLockExclusive(&state_lock)!=0;
    check(state_unlocked,"no lifecycle lock across original");
    if(state_unlocked) ReleaseSRWLockExclusive(&state_lock);
    const bool logger_unlocked=TryAcquireSRWLockExclusive(&logger_lock)!=0;
    check(logger_unlocked,"no logger lock across original");
    if(logger_unlocked) ReleaseSRWLockExclusive(&logger_lock);
    return opaque_result;
}
void* __fastcall fake_find(void*,const MsvcString* name,bool,bool) {
    for(std::size_t field=0;field<field_count;++field)
        if(name_equals(name,fields[field].edit)) return current_fixture->edit[field].data();
    return nullptr;
}
void reset_logs() {
    for(auto& source:key_record_counts) for(auto& count:source) count.store(0);
    for(auto& record:tick_records) record={};
    LARGE_INTEGER zero{};
    check(SetFilePointerEx(log_file,zero,nullptr,FILE_BEGIN)!=0 && SetEndOfFile(log_file)!=0,"reset owned log");
}
std::string logs() {
    LARGE_INTEGER size{},zero{};
    check(GetFileSizeEx(log_file,&size)!=0 && size.QuadPart>=0 && size.QuadPart<262144,"bounded owned log size");
    check(SetFilePointerEx(log_file,zero,nullptr,FILE_BEGIN)!=0,"rewind owned log");
    std::string value(static_cast<std::size_t>(size.QuadPart),'\0'); DWORD read=0;
    check(ReadFile(log_file,value.data(),static_cast<DWORD>(value.size()),&read,nullptr)!=0 && read==value.size(),"read owned log");
    check(SetFilePointerEx(log_file,zero,nullptr,FILE_END)!=0,"append owned log");
    return value;
}
std::size_t occurrences(const std::string& value,const char* marker) {
    std::size_t count=0,position=0;
    while((position=value.find(marker,position))!=std::string::npos) { ++count; position+=std::strlen(marker); }
    return count;
}
void forwarded(bool wrapper,void* first,void* event) {
    expected_first=first; expected_second=event; const auto before=original_calls;
    const auto result=wrapper?wrapper_observed(first,event):key_observed(first,event);
    check(original_calls==before+1,"every rejection/success forwards exactly once");
    check(result==opaque_result,"full opaque RAX preserved");
}
void logger_tests() {
    void* memory=VirtualAlloc(nullptr,0x3454000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(memory!=nullptr,"owned image allocation"); image_base=reinterpret_cast<std::uintptr_t>(memory);
    LoggerFixture f; current_fixture=&f; find_edit=fake_find;
    original_wrapper=fake_original; original_key=fake_original;
    log_file=CreateFileW(L"inline_key_logger_test.log",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(log_file!=INVALID_HANDLE_VALUE,"owned log creation"); active.store(true);
    for(std::size_t field=0;field<field_count;++field) {
        f.focus(field); KeyRecord r{};
        check(make_key_record(f.edit[field].data(),f.event.data(),true,r),"five exact own fields accepted");
        check(r.field==field && r.mask==3 && r.wrapper_type==1 && std::memcmp(r.values,f.words,sizeof(f.words))==0,"actual8DWORD and wrapper7C copy");
        check(r.status.mask==63 && r.status.widget_is_edit && r.status.receiver_is_edit &&
              !r.status.prefilled && r.status.epoch==7 && r.status.generation==11,"focus/lifecycle copied without readiness gate");
        forwarded(true,f.edit[field].data(),f.event.data());
        forwarded(false,f.edit[field].data()+0x80,f.event.data());
    }
    KeyRecord r{};
    check(make_key_record(f.edit[0].data(),nullptr,true,r) && r.mask==0,"own event read failure observable");
    forwarded(true,f.edit[0].data(),nullptr); forwarded(false,f.edit[0].data()+0x80,nullptr);
    put(f.edit[0].data(),0,std::uintptr_t{0});
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"wrong primary type rejected before payload");
    forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    put(f.edit[0].data(),0,image_base+0x2B4A940);
    put(f.definition[0].data(),0x28,borrowed_string("foreign",7));
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"foreign name rejected");
    reset_logs(); forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    check(logs().empty(),"foreign payload never emitted");
    put(f.definition[0].data(),0x28,borrowed_string(fields[0].edit,std::strlen(fields[0].edit)));
    forwarded(false,reinterpret_cast<void*>(0x7F),f.event.data());
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(0));
    put(f.edit[0].data(),0x271,static_cast<unsigned char>(2));
    put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(nullptr));
    check(make_key_record(f.edit[0].data(),f.event.data(),true,r) && !r.status.manager_present &&
          r.status.flags75==0 && r.status.flags271==2,"false visibility/disabled/focus separately observable");
    forwarded(true,f.edit[0].data(),f.event.data());
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(8)); put(f.edit[0].data(),0x271,static_cast<unsigned char>(1));
    put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(f.manager.data())); f.focus(0);
    void* guarded=VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); check(guarded!=nullptr,"owned guarded event allocation");
    DWORD old_protection=0;
    check(VirtualProtect(static_cast<unsigned char*>(guarded)+4096,4096,PAGE_NOACCESS,&old_protection)!=0,"guard event boundary");
    void* partial=static_cast<unsigned char*>(guarded)+4096-sizeof(f.words); std::memcpy(partial,f.words,sizeof(f.words));
    check(make_key_record(f.edit[0].data(),partial,true,r) && r.mask==1 && std::memcmp(r.values,f.words,sizeof(f.words))==0,"DWORDs readable but wrapper7C unreadable distinguished");
    forwarded(true,f.edit[0].data(),partial);
    check(VirtualFree(guarded,0,MEM_RELEASE)!=0,"guarded event released");
    active.store(false); reset_logs(); forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    check(logs().empty(),"inactive logger silently forwards"); active.store(true);
    put(f.event.data(),0,std::uint32_t{'a'}); reset_logs();
    for(unsigned i=0;i<100;++i) { forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data()); }
    const auto capped=logs();
    check(occurrences(capped,"source=wrapper")==33 && occurrences(capped,"source=tb")==33 && occurrences(capped,"cap=32")==2,"32 records plus one cap marker per source");
    check(key_record_counts[0][0].load()==33 && key_record_counts[1][0].load()==33,"caps saturate without counter wrap");
    reset_logs(); observe_key_tick(f.window.data());
    for(unsigned i=0;i<100;++i) observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==2,"one fresh model+field observation; no heartbeat");
    put(f.edit[0].data(),0x271,static_cast<unsigned char>(0)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==3,"focus transition emitted");
    ++lifecycle[0].generation; observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==5,"bind/open generation transition emitted");
    put(f.root.data(),0xA5,static_cast<unsigned char>(0)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==7,"hidden transition emitted");
    put(f.window.data(),0xF8,static_cast<void*>(nullptr)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==8,"invalid own view logs model only without invalid-root lookup");
    put(f.window.data(),0xF8,static_cast<void*>(f.root.data())); put(f.root.data(),0xA5,static_cast<unsigned char>(8));
    reset_logs();
    for(unsigned i=0;i<200;++i) { ++lifecycle[0].generation; observe_key_tick(f.window.data()); }
    check(occurrences(logs(),"cap=64")==2 && tick_records[0].count==65 && tick_records[field_count].count==65,"tick transitions bounded per field/model");
    active.store(false); CloseHandle(log_file); log_file=INVALID_HANDLE_VALUE;
    current_fixture=nullptr; image_base=0; find_edit=nullptr; original_wrapper=nullptr; original_key=nullptr;
    check(VirtualFree(memory,0,MEM_RELEASE)!=0,"owned image released");
    // Load only into this disposable non-HOI4 test process. Host hash fails before
    // hooks, module pinning, or native engine calls; a second init cannot retry.
    HMODULE candidate=LoadLibraryW(L"rename_anything_inline_editor_key_logger.dll");
    check(candidate!=nullptr,"wrong-host candidate load into own test process");
    using Initialize=DWORD(WINAPI*)(void*);
    const auto initialize=reinterpret_cast<Initialize>(GetProcAddress(candidate,"RNInitialize"));
    check(initialize!=nullptr && initialize(nullptr)==10,"wrong executable hash rejected before hooks");
    check(initialize(nullptr)==33,"failed initialization cannot retry");
    check(FreeLibrary(candidate)!=0,"uninitialized wrong-host module released");
}
}

int main() {
    // Includes stale/duplicate reserved keys and an unrelated similarly named key.
    std::array<Record,5> old{};
    check(transport_record(old[0],"rn_native_ok",99),"seed A");
    check(transport_record(old[1],"rn_native_ok",98),"seed duplicate A");
    check(transport_record(old[2],"rn_nativeb_31",97),"seed B");
    check(transport_record(old[3],"rn_native_cap",96),"seed capability");
    check(transport_record(old[4],"rn_native_00",95),"seed unrelated lookalike");
    FakeContext context{}; context.vector={old.data(),5,5,nullptr};
    InputSnapshot pair[2]={text(1,'a'),text(32,0x017B)}; // code1 / code84
    auto party=command(context,1);
    check(prepare_transport(party,pair),"party valid A1 B32");
    check(party.view.count==70,"69 fresh plus one unrelated");
    check(raw(party,"rn_native_00")==9500000,"unrelated exact value retained");
    check(raw(party,"rn_native_ok")==100000 && raw(party,"rn_native_len")==100000,"A header");
    check(raw(party,"rn_nativeb_ok")==100000 && raw(party,"rn_nativeb_len")==3200000,"B header");
    check(raw(party,command_epoch_key)==700000,"fresh epoch fixed point");
    for(unsigned i=0;i<32;++i) {
        char key[16]{}; std::snprintf(key,sizeof(key),"rn_native_%u",i);
        check(raw(party,key)==(i==0?100000:0),"A active and zero padding");
        std::snprintf(key,sizeof(key),"rn_nativeb_%u",i);
        check(raw(party,key)==8400000,"B all 32 Polish codes");
    }
    pair[0]=text(32,'b'); pair[1]=text(1,'a');
    auto reverse=command(context,1);
    check(prepare_transport(reverse,pair),"party valid A32 B1");
    check(raw(reverse,"rn_native_len")==3200000 && raw(reverse,"rn_nativeb_len")==100000,"reverse lengths");
    check(raw(reverse,"rn_native_31")==200000 && raw(reverse,"rn_nativeb_31")==0,"reverse endpoints");

    pair[1]=text(33,'a');
    auto invalid=command(context,1);
    check(prepare_transport(invalid,pair),"invalid B still carries rejection marker");
    check(raw(invalid,"rn_native_ok")==100000 && raw(invalid,"rn_native_len")==3200000,"valid A intact with invalid B");
    check(raw(invalid,"rn_nativeb_ok")==0 && raw(invalid,"rn_nativeb_len")==0,"invalid B header zero");
    for(unsigned i=0;i<32;++i) {
        char key[16]{}; std::snprintf(key,sizeof(key),"rn_nativeb_%u",i);
        check(raw(invalid,key)==0,"invalid B all slots zero");
    }
    auto leader=command(context,0);
    check(prepare_transport(leader,pair) && leader.view.count==36,"leader remains 35 fresh records");
    auto country=command(context,2);
    check(!country_ready() && !prepare_transport(country,pair),"country without initialized DLL rejected");
    auto bad_epoch=command(context,1); bad_epoch.epoch=0;
    check(!prepare_transport(bad_epoch,pair),"zero epoch rejected");
    bad_epoch.epoch=static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/fixed_point)+1;
    check(!prepare_transport(bad_epoch,pair),"epoch overflow rejected");
    auto bad_model=command(context,model_count);
    check(!prepare_transport(bad_model,pair),"unknown model rejected");
    Record fixed{};
    check(!transport_record(fixed,"sixteenbytekeyxx",1),"overlong SSO key rejected");
    check(!transport_record(fixed,"ok",static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()/fixed_point)+1),"fixed point overflow rejected");
    context.vector.count=257; context.vector.capacity=257;
    auto oversized=command(context,1);
    check(!prepare_transport(oversized,pair),"oversized source vector rejected");
    logger_tests();
    std::printf("inline key logger/transport: %u checks passed; no game hooks/dispatch\n",checks);
}
