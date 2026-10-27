// Exercises the actual isolated builder without installing hooks or dispatching.
#include "../compat/rn_editor_bridge.hpp"
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
/// @brief Shared empty bucket of a child name map (the game points empty maps at a static one).
alignas(8) std::array<unsigned char, capie::core::off::map_entry_size> empty_map_bucket{};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
/// @brief Gives a fixture root the two empty child name maps a 1.19.4.0 CContainerWindow has.
void put_empty_maps(void* root) {
    for (const auto map : {capie::core::off::root_edit_map, capie::core::off::root_instant_map}) {
        void* bucket = empty_map_bucket.data();
        std::memcpy(static_cast<unsigned char*>(root) + map + capie::core::off::map_entries, &bucket, sizeof(bucket));
    }
}
template<class T> void put(void* object,std::size_t offset,const T& value) {
    std::memcpy(static_cast<unsigned char*>(object)+offset,&value,sizeof(value));
}
struct LoggerFixture {
    alignas(8) std::array<unsigned char,0xC00> window{};
    alignas(8) std::array<unsigned char, off::root_readable_span> root{};
    alignas(8) std::array<unsigned char,0x30> model{};
    alignas(8) std::array<unsigned char,0x238> manager{};
    alignas(8) std::array<std::array<unsigned char,0x290>,field_count> edit{};
    alignas(8) std::array<std::array<unsigned char,0x50>,field_count> definition{};
    alignas(8) std::array<unsigned char,0x80> event{};
    std::uint32_t words[8]={13,0x76543210,0,0xFFFFFFFE,4,5,6,0};
    LoggerFixture() {
        put(model.data(),0,image_base+0x2A54398);
        put(model.data(),8,borrowed_string(models[0].name,std::strlen(models[0].name)));
        put(window.data(),0xE8,static_cast<void*>(model.data()));
        put(window.data(),0xF8,static_cast<void*>(root.data()));
        put(window.data(),0x110,image_base+0x29CB310);
        put(window.data(),0x120,static_cast<void*>(window.data()));
        put(window.data(),0x130,static_cast<void*>(model.data()));
        put(window.data(),0x138,static_cast<void*>(root.data()));
        put(window.data(),0xBF9,static_cast<unsigned char>(1));
        put(root.data(),0,image_base+0x2BB9FE0);
        put_empty_maps(root.data());
        put(root.data(),0xA5,static_cast<unsigned char>(8));
        for(std::size_t field=0;field<field_count;++field) {
            put(edit[field].data(),0,image_base+0x2BBF068);
            put(edit[field].data(),0x80,image_base+0x2BBF308);
            put(edit[field].data(),0x60,static_cast<void*>(definition[field].data()));
            put(definition[field].data(),0,image_base+0x2BC6978);
            put(definition[field].data(),0x28,borrowed_string(fields[field].edit,std::strlen(fields[field].edit)));
            put(edit[field].data(),0x75,static_cast<unsigned char>(8));
            put(edit[field].data(),0x281,static_cast<unsigned char>(1));
            // Text is intentionally unreadable; the added reader must never use it.
            put(edit[field].data(),0xB0,reinterpret_cast<void*>(1));
            put(edit[field].data(),0xC0,std::uint64_t{128});
            put(edit[field].data(),0xC8,std::uint64_t{128});
        }
        std::memcpy(event.data(),words,sizeof(words)); put(event.data(),0x7C,std::uint32_t{1});
        put(reinterpret_cast<void*>(image_base),0x3500FC0,static_cast<void*>(manager.data()));
        focus(0);
        for(auto& state:lifecycle) state={false,false,false,7,11,0};
    }
    void focus(std::size_t field) {
        put(manager.data(),0x138,static_cast<void*>(edit[field].data()));
        put(manager.data(),0x210,static_cast<void*>(edit[field].data()+0x1A0));
    }
};
LoggerFixture* current_fixture=nullptr;
unsigned debug_edit_lookups=0;
bool debug_mutate_second_lookup=false;
bool debug_rebind_second_lookup=false;
std::array<unsigned char,0x140> debug_epoch_widget{};
unsigned original_calls=0;
unsigned execute_calls=0;
void* expected_command=nullptr;
bool invalidate_command=false;
void __fastcall fake_execute(void* command) {
    ++execute_calls; check(command==expected_command,"Execute original argument unchanged");
    const bool unlocked=TryAcquireSRWLockExclusive(&debug_file_lock)!=0;
    check(unlocked,"no debug file lock across native Execute");
    if(unlocked) ReleaseSRWLockExclusive(&debug_file_lock);
    debug_ring.copy(debug_dump_buffer); // Would deadlock if trace lock crossed native.
    if(invalidate_command) {
        DWORD before=0;
        check(VirtualProtect(command,4096,PAGE_NOACCESS,&before)!=0,"fake native releases command before returning");
    }
}
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
void* __fastcall fake_find(void*, const capie::engine::NameView* view,bool,bool) {
    const auto name_text = capie::engine::borrowed_string(view->data, static_cast<std::size_t>(view->size));
    const MsvcString* name = &name_text;
    for(std::size_t field=0;field<field_count;++field)
        if(name_equals(name,fields[field].edit)) {
            ++debug_edit_lookups;
            if(debug_mutate_second_lookup && debug_edit_lookups==2) put(current_fixture->edit[field].data(),0xB0,std::uint32_t{'z'});
            if(debug_rebind_second_lookup && debug_edit_lookups==2) ++lifecycle[fields[field].model].generation;
            return current_fixture->edit[field].data();
        }
    return nullptr;
}
void* __fastcall fake_instant(void*, const capie::engine::NameView* view,bool,bool) {
    const auto name_text = capie::engine::borrowed_string(view->data, static_cast<std::size_t>(view->size));
    const MsvcString* name = &name_text;
    for(const auto& model:models) if(name_equals(name,model.epoch)) return debug_epoch_widget.data();
    return nullptr;
}
void reset_logs() {
    for(auto& source:key_record_counts) for(auto& field:source) for(auto& count:field) count.store(0);
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
    void* memory=VirtualAlloc(nullptr,0x3502000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(memory!=nullptr,"owned image allocation"); image_base=reinterpret_cast<std::uintptr_t>(memory);
    LoggerFixture f; current_fixture=&f; find_edit=fake_find;
    original_wrapper=fake_original; original_key=fake_original;
    log_file=CreateFileW(L"inline_debug_bridge_test.log",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
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
    check(key_category(r)==KeyCategory::event_read_failed,"failed event copy cannot be classified as empty");
    forwarded(true,f.edit[0].data(),nullptr); forwarded(false,f.edit[0].data()+0x80,nullptr);
    put(f.edit[0].data(),0,std::uintptr_t{0});
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"wrong primary type rejected before payload");
    forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    put(f.edit[0].data(),0,image_base+0x2BBF068);
    put(f.edit[0].data(),0x80,std::uintptr_t{0});
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"wrong TB type rejected before payload");
    forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    put(f.edit[0].data(),0x80,image_base+0x2BBF308);
    put(f.definition[0].data(),0,std::uintptr_t{0});
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"wrong definition type rejected before payload");
    forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    put(f.definition[0].data(),0,image_base+0x2BC6978);
    put(f.definition[0].data(),0x28,borrowed_string("foreign",7));
    check(!make_key_record(f.edit[0].data(),f.event.data(),true,r),"foreign name rejected");
    reset_logs(); forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    check(logs().empty(),"foreign payload never emitted");
    put(f.definition[0].data(),0x28,borrowed_string(fields[0].edit,std::strlen(fields[0].edit)));
    forwarded(false,reinterpret_cast<void*>(0x7F),f.event.data());
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(0));
    put(f.edit[0].data(),0x281,static_cast<unsigned char>(2));
    put(reinterpret_cast<void*>(image_base),0x3500FC0,static_cast<void*>(nullptr));
    check(make_key_record(f.edit[0].data(),f.event.data(),true,r) && !r.status.manager_present &&
          r.status.flags75==0 && r.status.flags271==2,"false visibility/disabled/focus separately observable");
    forwarded(true,f.edit[0].data(),f.event.data());
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(8)); put(f.edit[0].data(),0x281,static_cast<unsigned char>(1));
    put(reinterpret_cast<void*>(image_base),0x3500FC0,static_cast<void*>(f.manager.data())); f.focus(0);
    void* guarded=VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); check(guarded!=nullptr,"owned guarded event allocation");
    DWORD old_protection=0;
    check(VirtualProtect(static_cast<unsigned char*>(guarded)+4096,4096,PAGE_NOACCESS,&old_protection)!=0,"guard event boundary");
    void* partial=static_cast<unsigned char*>(guarded)+4096-sizeof(f.words); std::memcpy(partial,f.words,sizeof(f.words));
    check(make_key_record(f.edit[0].data(),partial,true,r) && r.mask==1 && std::memcmp(r.values,f.words,sizeof(f.words))==0,"DWORDs readable but wrapper7C unreadable distinguished");
    check(key_category(r)==KeyCategory::meaningful,"wrapper7C failure cannot hide readable meaningful event");
    forwarded(true,f.edit[0].data(),partial);
    void* partial_words=static_cast<unsigned char*>(guarded)+4096-sizeof(f.words)/2;
    std::memcpy(partial_words,f.words,sizeof(f.words)/2);
    check(make_key_record(f.edit[0].data(),partial_words,true,r) && !(r.mask&1) &&
          key_category(r)==KeyCategory::event_read_failed,"incomplete DWORD array is readfailed even with nonzero readable prefix");
    forwarded(true,f.edit[0].data(),partial_words); forwarded(false,f.edit[0].data()+0x80,partial_words);
    check(VirtualFree(guarded,0,MEM_RELEASE)!=0,"guarded event released");
    active.store(false); reset_logs(); forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data());
    check(logs().empty(),"inactive logger silently forwards"); active.store(true);
    for(std::size_t word=0;word<8;++word) {
        std::memset(f.event.data(),0,sizeof(f.words)); put(f.event.data(),word*sizeof(std::uint32_t),std::uint32_t{1});
        check(make_key_record(f.edit[0].data(),f.event.data(),true,r) &&
              key_category(r)==KeyCategory::meaningful,"each of the eight DWORDs independently makes a meaningful event");
    }
    // Actual emitted categories and forwarding after thousands of normal polls
    // and failed event reads. Neither may consume the meaningful-key budget.
    std::memset(f.event.data(),0,sizeof(f.words)); reset_logs();
    for(unsigned i=0;i<2000;++i) {
        forwarded(true,f.edit[0].data(),f.event.data());
        forwarded(false,f.edit[0].data()+0x80,f.event.data());
    }
    for(unsigned i=0;i<2000;++i) {
        forwarded(true,f.edit[0].data(),nullptr);
        forwarded(false,f.edit[0].data()+0x80,nullptr);
    }
    for(auto& source:key_record_counts) {
        check(source[0][0].load()==0,"empty/read failure never consume meaningful quota");
        check(source[0][1].load()==3 && source[0][2].load()==3,"independent empty/readfailed budgets saturate at2+cap");
    }
    for(const std::uint32_t key:{8u,13u}) {
        put(f.event.data(),0,key);
        forwarded(true,f.edit[0].data(),f.event.data());
        forwarded(false,f.edit[0].data()+0x80,f.event.data());
    }
    const auto after_polls=logs();
    check(occurrences(after_polls,"category=meaningful thread=")==4,"Backspace and Return emitted after8000 polls/readfails");
    check(occurrences(after_polls,"event=[8/0x00000008")==2 && occurrences(after_polls,"event=[13/0x0000000D")==2,"actual raw Backspace/Return preserved");
    check(occurrences(after_polls,"category=empty_poll thread=")==4 && occurrences(after_polls,"category=empty_poll category_cap=2")==2,"empty has two records plus explicit cap per source");
    check(occurrences(after_polls,"category=event_read_failed thread=")==4 && occurrences(after_polls,"category=event_read_failed category_cap=2")==2,"readfailed has independent records and cap per source");
    check(occurrences(after_polls,"category=meaningful category_cap=")==0,"meaningful remains uncapped after other category caps");
    put(f.event.data(),0,std::uint32_t{'a'});
    for(unsigned i=0;i<100;++i) { forwarded(true,f.edit[0].data(),f.event.data()); forwarded(false,f.edit[0].data()+0x80,f.event.data()); }
    const auto capped=logs();
    check(occurrences(capped,"category=meaningful thread=")==64 && occurrences(capped,"category=meaningful category_cap=32")==2,"32 meaningful plus cap per source, unaffected by polling");
    check(key_record_counts[0][0][0].load()==33 && key_record_counts[1][0][0].load()==33,"meaningful counters saturate without wrap");
    forwarded(true,f.edit[1].data(),f.event.data()); forwarded(false,f.edit[1].data()+0x80,f.event.data());
    check(key_record_counts[0][1][0].load()==1 && key_record_counts[1][1][0].load()==1,"other own field/source retains its budget");
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(0xE8));
    put(f.edit[0].data(),0x281,static_cast<unsigned char>(0x91));
    reset_logs(); observe_key_tick(f.window.data());
    for(unsigned i=0;i<100;++i) observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==2,"one fresh model+field observation; no heartbeat");
    check(logs().find("flags75=0xE8 flags271=0x91 routing75=0x08 routing271=0x01")!=std::string::npos,"first full flags retained alongside documented routing masks");
    for(unsigned i=0;i<2000;++i) {
        put(f.edit[0].data(),0x75,static_cast<unsigned char>(i%2?0xE8:0xA8));
        put(f.edit[0].data(),0x281,static_cast<unsigned char>(i%2?0x91:0x81));
        observe_key_tick(f.window.data());
    }
    check(occurrences(logs(),"RN_KEYTICK ")==2 && tick_records[0].count==1,"unknown raw bit changes do not consume semantic tick budget");

    put(f.edit[0].data(),0x281,static_cast<unsigned char>(0)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==3,"focus transition emitted");
    ++lifecycle[0].generation; observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==5,"bind/open generation transition emitted");
    put(f.root.data(),0xA5,static_cast<unsigned char>(0)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==7,"hidden transition emitted");
    put(f.window.data(),0xF8,static_cast<void*>(nullptr)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==8,"invalid own view logs model only without invalid-root lookup");
    put(f.window.data(),0xF8,static_cast<void*>(f.root.data())); put(f.root.data(),0xA5,static_cast<unsigned char>(8));
    reset_logs(); observe_key_tick(f.window.data());
    put(f.edit[0].data(),0x75,static_cast<unsigned char>(0)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==3,"documented visible08 routing transition retained");
    put(f.edit[0].data(),0x281,static_cast<unsigned char>(2)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==4,"documented disabled02 routing transition retained");
    f.focus(1); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==5,"manager widget/receiver relation transition retained");
    lifecycle[0].attempted=true; lifecycle[0].prefilled=true; observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==7,"readiness change retained in field and model status");
    put(reinterpret_cast<void*>(image_base),0x3500FC0,static_cast<void*>(nullptr)); observe_key_tick(f.window.data());
    check(occurrences(logs(),"RN_KEYTICK ")==8,"manager read/presence change retained");
    put(reinterpret_cast<void*>(image_base),0x3500FC0,static_cast<void*>(f.manager.data())); f.focus(0);
    reset_logs();
    for(unsigned i=0;i<200;++i) { ++lifecycle[0].generation; observe_key_tick(f.window.data()); }
    check(occurrences(logs(),"cap=64")==2 && tick_records[0].count==65 && tick_records[field_count].count==65,"tick transitions bounded per field/model");
    // Additional integration checks exercise actual trace/snapshot/action code.
    reset_logs();
    for(unsigned i=0;i<40;++i) { put(f.event.data(),0,std::uint32_t{1000+i}); forwarded(true,f.edit[0].data(),f.event.data()); }
    debug_ring.copy(debug_dump_buffer);
    bool latest=false;
    for(std::size_t i=0;i<debug_dump_buffer.count;++i)
        if(std::strstr(debug_dump_buffer.entries[i].text,"event=[1039/")) latest=true;
    check(latest && occurrences(logs(),"category_cap=32")==1 && occurrences(logs(),"event=[")==32,"latest meaningful key retained after file cap");
    for(const auto& action:debug_actions) {
        put(f.model.data(),8,borrowed_string(action.model,std::strlen(action.model)));
        const auto selector=borrowed_string(action.selector,std::strlen(action.selector));
        debug_own_action(f.window.data()+0x110,&selector);
    }
    check(occurrences(logs(),"stage=pre_dispatch")==10,"all seven own models and ten exact selectors observable");
    reset_logs();
    const auto foreign_selector=borrowed_string("foreign",7);
    debug_own_action(f.window.data()+0x110,&foreign_selector);
    check(logs().empty(),"foreign selector is not captured");
    put(f.model.data(),8,borrowed_string("foreign_model",13));
    auto selector=borrowed_string(debug_actions[0].selector,std::strlen(debug_actions[0].selector));
    debug_own_action(f.window.data()+0x110,&selector);
    check(logs().empty(),"foreign model is not captured");
    put(f.model.data(),8,borrowed_string(debug_actions[0].model,std::strlen(debug_actions[0].model)));
    put(f.window.data(),0x138,static_cast<void*>(nullptr));
    debug_own_action(f.window.data()+0x110,&selector);
    check(logs().find("reason=context_guard_failed")!=std::string::npos,"recognized action with stale context reports refusal observation");
    put(f.window.data(),0x138,static_cast<void*>(f.root.data()));
    put(debug_epoch_widget.data(),0,image_base+0x2BBB568);
    put(debug_epoch_widget.data(),0x110,borrowed_string("7",1)); find_instant=fake_instant;
    for(std::size_t field=0;field<field_count;++field) {
        put(f.edit[field].data(),0x75,static_cast<unsigned char>(8));
        put(f.edit[field].data(),0xB0,std::uint32_t{0x017B});
        put(f.edit[field].data(),0xC0,std::uint64_t{1}); put(f.edit[field].data(),0xC8,std::uint64_t{3});
    }
    for(auto& state:lifecycle) state={true,true,true,7,11,0};
    reset_logs();
    for(std::size_t model=0;model<model_count;++model) {
        put(f.model.data(),8,borrowed_string(models[model].name,std::strlen(models[model].name)));
        View view{}; const auto result=inspect_window(f.window.data(),view);
        check(result==ViewResult::visible && view.model==model,"fresh snapshot own View");
        debug_snapshot(f.window.data(),view,result,9);
    }
    check(occurrences(logs(),"event=debug-request-field-snapshot")==5 && occurrences(logs(),"codepoints=[017B]")==5,"all five owned UTF32 fields copied without typing or setters");
    put(f.model.data(),8,borrowed_string(models[0].name,std::strlen(models[0].name)));
    View view{}; inspect_window(f.window.data(),view);
    reset_logs(); debug_edit_lookups=0;
    debug_snapshot(f.window.data(),view,ViewResult::hidden,10);
    check(debug_edit_lookups==0 && logs().find("reason=own_root_hidden")!=std::string::npos && logs().find("codepoints=")==std::string::npos,"hidden refuses text lookup");
    reset_logs(); debug_edit_lookups=0; debug_mutate_second_lookup=true;
    debug_snapshot(f.window.data(),view,ViewResult::visible,11); debug_mutate_second_lookup=false;
    check(logs().find("reason=text_changed_during_snapshot")!=std::string::npos && logs().find("codepoints=")==std::string::npos,"changed text never published as coherent snapshot");
    reset_logs(); debug_edit_lookups=0; debug_rebind_second_lookup=true;
    debug_snapshot(f.window.data(),view,ViewResult::visible,12); debug_rebind_second_lookup=false;
    check(logs().find("reason=final_lifecycle_changed")!=std::string::npos && logs().find("codepoints=")==std::string::npos,"last native lookup rebind rejects equal-text snapshot");
    reset_logs(); debug_edit_lookups=0;
    check(RNDebugRequest(reinterpret_cast<void*>(5))==42 && RNDebugRequest(reinterpret_cast<void*>(1))==0,"export accepts only fixed scalar request");
    check(debug_edit_lookups==0 && logs().empty(),"remote request does not read engine or write log");
    service_debug(f.window.data());
    check(logs().find("stage=request")!=std::string::npos && logs().find("event=debug-request-field-snapshot")!=std::string::npos,"requested own snapshot fulfilled only by service point");
    check(RNDebugRequest(reinterpret_cast<void*>(3))==0,"pause queued"); service_debug(f.window.data());
    check(RNDebugRequest(reinterpret_cast<void*>(4))==0,"resume queued"); service_debug(f.window.data());
    void* queued_command=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(queued_command!=nullptr,"owned command allocation");
    expected_command=queued_command; original_execute=fake_execute;
    std::array<Record,3> headers{};
    transport_record(headers[0],command_epoch_key,7); transport_record(headers[1],"rn_native_ok",1);
    transport_record(headers[2],"rn_native_len",9);
    const TempVector command_vector{headers.data(),3,3,nullptr};
    put(queued_command,0,image_base+0x2A12ED0); put(queued_command,0x28,command_vector);
    put(queued_command,0x48,1); put(queued_command,0x4C,1);
    for(const auto& model:models) {
        put(queued_command,0x78,borrowed_string(model.name,std::strlen(model.name)));
        put(queued_command,0x58,borrowed_string(model.apply,std::strlen(model.apply)));
        reset_logs(); const auto before=execute_calls; execute_observed(queued_command);
        check(execute_calls==before+1 && occurrences(logs(),"stage=pre_execute")==1 && occurrences(logs(),"stage=execute_return")==1,"exact three own Apply commands reach Execute once");
        check(logs().find("header_mask=7 epoch_raw=700000 a_ok_raw=100000 a_len_raw=900000")!=std::string::npos,"copied native fixed-point transport headers");
        check(logs().find("effect_success=unverified")!=std::string::npos,"void return is never labeled successful effect");
    }
    reset_logs(); put(queued_command,0,std::uintptr_t{0});
    auto before=execute_calls; execute_observed(queued_command);
    check(execute_calls==before+1 && logs().empty(),"foreign command forwarded without diagnostic capture");
    put(queued_command,0,image_base+0x2A12ED0);
    const TempVector invalid_vector{reinterpret_cast<Record*>(1),1,1,nullptr}; put(queued_command,0x28,invalid_vector);
    reset_logs(); before=execute_calls; execute_observed(queued_command);
    check(execute_calls==before+1 && logs().find("reason=record_guard_failed")!=std::string::npos,"malformed own command is observed and always forwarded");
    put(queued_command,0x28,command_vector); reset_logs(); invalidate_command=true;
    before=execute_calls; execute_observed(queued_command);
    check(execute_calls==before+1 && logs().find("stage=execute_return")!=std::string::npos,"no command dereference after original frees it");
    invalidate_command=false; original_execute=nullptr;
    check(VirtualFree(queued_command,0,MEM_RELEASE)!=0,"owned command released");
    active.store(false); check(RNDebugRequest(reinterpret_cast<void*>(1))==41,"inactive export refuses");
    CloseHandle(log_file); log_file=INVALID_HANDLE_VALUE;
    current_fixture=nullptr; image_base=0; find_edit=nullptr; original_wrapper=nullptr; original_key=nullptr;
    check(VirtualFree(memory,0,MEM_RELEASE)!=0,"owned image released");
    // Load only into this disposable non-HOI4 test process. Host hash fails before
    // hooks, module pinning, or native engine calls; a second init cannot retry.
    HMODULE candidate=LoadLibraryW(L"capie_editor.dll");
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
    for(unsigned i=0;i<31;++i) {
        char key[16]{}; std::snprintf(key,sizeof(key),"rn_nativeb_%u",i);
        check(raw(invalid,key)==0,"invalid B all slots zero");
    }
    check(raw(invalid,"rn_nativeb_err")==200000,"invalid B carries InputError too_long in its last slot");
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
    std::printf("inline debug bridge/transport: %u checks passed; no game hooks/dispatch\n",checks);
}
