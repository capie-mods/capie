// Exercises the actual isolated builder without installing hooks or dispatching.
#include "inline_editor_focus_bridge.cpp"
#include <array>
#include <cstdlib>

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

// Exercise the real fresh-view/focus orchestration on owned fake game objects.
// Native callbacks are stubs: these tests prove rejection/order, not native ABI.
template<class T> void put(void* object,std::size_t offset,const T& value) {
    std::memcpy(static_cast<unsigned char*>(object)+offset,&value,sizeof(value));
}
struct FakeEdit {
    alignas(8) std::array<unsigned char,0x280> bytes{};
    alignas(8) std::array<unsigned char,0x50> definition{};
    std::array<std::uint32_t,max_codepoints> points{};
};
enum class CallbackPhase { focus,select,refresh };
enum class Mutation { none,epoch,generation,root,text,other_text,widget,receiver,hide,edit,manager,active_bit };
struct FocusFixture {
    alignas(8) std::array<unsigned char,0xB80> window{};
    alignas(8) std::array<unsigned char,0x1360> root{},other_root{};
    alignas(8) std::array<unsigned char,0x30> model{};
    alignas(8) std::array<unsigned char,0x238> manager{},other_manager{};
    alignas(8) std::array<unsigned char,0x130> epoch_carrier{};
    std::array<FakeEdit,3> edit{};
    void* identities[2]{};
    Utf8Snapshot expected[2]{};
    View view{};
    Mutation mutation{Mutation::none};
    CallbackPhase phase{CallbackPhase::focus};
    bool replacement{};
    unsigned focus_calls{},select_calls{},refresh_calls{},sequence{};
    int begin{-1},end{-1};
    void* focused{};

    FocusFixture(std::size_t which,std::size_t length) {
        view={root.data(),which};
        const auto& spec=models[which];
        put(model.data(),0,image_base+0x29DB558);
        put(model.data(),8,borrowed_string(spec.name,std::strlen(spec.name)));
        put(window.data(),0xE8,static_cast<void*>(model.data()));
        put(window.data(),0xF8,static_cast<void*>(root.data()));
        put(window.data(),0x110,image_base+0x294E568);
        put(window.data(),0x120,static_cast<void*>(window.data()));
        put(window.data(),0x130,static_cast<void*>(model.data()));
        put(window.data(),0x138,static_cast<void*>(root.data()));
        put(window.data(),0xB79,static_cast<unsigned char>(1));
        put(root.data(),0,image_base+0x2B45278);
        put(root.data(),0xA5,static_cast<unsigned char>(8));
        put(epoch_carrier.data(),0,image_base+0x2B468C8);
        put(epoch_carrier.data(),0x110,borrowed_string("7",1));
        for(std::size_t j=0;j<spec.count;++j) {
            auto& item=edit[j]; const auto field=spec.first+j;
            put(item.bytes.data(),0,image_base+0x2B4A940);
            put(item.bytes.data(),0x80,image_base+0x2B4ABD0);
            put(item.bytes.data(),0x60,static_cast<void*>(item.definition.data()));
            put(item.definition.data(),0,image_base+0x2B54050);
            put(item.definition.data(),0x28,borrowed_string(fields[field].edit,std::strlen(fields[field].edit)));
            put(item.bytes.data(),0x75,static_cast<unsigned char>(8));
            const auto size=j==0?length:std::size_t{4};
            for(std::size_t k=0;k<size;++k) item.points[k]=j==0?0x017B:'b';
            put(item.bytes.data(),0xC0,static_cast<std::uint64_t>(size));
            put(item.bytes.data(),0xC8,std::uint64_t{128});
            put(item.bytes.data(),0xB0,static_cast<void*>(item.points.data()));
            expected[j].decoded.size=size;
            std::memcpy(expected[j].decoded.points,item.points.data(),size*sizeof(std::uint32_t));
            identities[j]=item.bytes.data();
        }
        put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(manager.data()));
        for(auto& state:lifecycle) state={};
        lifecycle[which]={true,true,false,7,11,0};
    }
    void mutate(CallbackPhase at) {
        if(at!=phase) return;
        switch(mutation) {
        case Mutation::none: break;
        case Mutation::epoch: put(epoch_carrier.data(),0x110,borrowed_string("8",1)); break;
        case Mutation::generation: ++lifecycle[view.model].generation; break;
        case Mutation::root:
            other_root=root;
            put(window.data(),0xF8,static_cast<void*>(other_root.data()));
            put(window.data(),0x138,static_cast<void*>(other_root.data())); break;
        case Mutation::text: edit[0].points[0]='x'; break;
        case Mutation::other_text: edit[1].points[0]='x'; break;
        case Mutation::widget: put(manager.data(),0x158,static_cast<void*>(window.data())); break;
        case Mutation::receiver: put(manager.data(),0x230,static_cast<void*>(window.data())); break;
        case Mutation::hide: put(root.data(),0xA5,static_cast<unsigned char>(0)); break;
        case Mutation::edit: edit[2].bytes=edit[0].bytes; replacement=true; break;
        case Mutation::manager:
            other_manager=manager;
            put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(other_manager.data())); break;
        case Mutation::active_bit: put(edit[0].bytes.data(),0x271,static_cast<unsigned char>(0)); break;
        }
    }
};
FocusFixture* current_fixture=nullptr;
void* __fastcall fake_find_edit(void*,const MsvcString* name,bool,bool) {
    auto& f=*current_fixture; const auto& model=models[f.view.model];
    for(std::size_t j=0;j<model.count;++j)
        if(name_equals(name,fields[model.first+j].edit)) return f.edit[j==0 && f.replacement?2:j].bytes.data();
    return nullptr;
}
void* __fastcall fake_find_instant(void*,const MsvcString* name,bool,bool) {
    auto& f=*current_fixture;
    return name_equals(name,models[f.view.model].epoch)?f.epoch_carrier.data():nullptr;
}
void __fastcall fake_focus(void* manager,void* primary) {
    auto& f=*current_fixture; ++f.focus_calls; f.sequence=f.sequence*10+1; f.focused=primary;
    const bool unlocked=TryAcquireSRWLockExclusive(&state_lock)!=0;
    check(unlocked,"no lifecycle lock across focus callback");
    if(unlocked) ReleaseSRWLockExclusive(&state_lock);
    check(manager==f.manager.data() && primary==f.identities[0],"focus exact fresh manager/primary");
    put(manager,0x158,primary);
    put(manager,0x230,static_cast<void*>(static_cast<unsigned char*>(primary)+0x1A0));
    put(primary,0x271,static_cast<unsigned char>(1));
    f.mutate(CallbackPhase::focus);
}
void __fastcall fake_select(void* buffer,int begin,int end) {
    auto& f=*current_fixture; ++f.select_calls; f.sequence=f.sequence*10+2; f.begin=begin; f.end=end;
    const bool unlocked=TryAcquireSRWLockExclusive(&state_lock)!=0;
    check(unlocked,"no lifecycle lock across selection callback");
    if(unlocked) ReleaseSRWLockExclusive(&state_lock);
    check(buffer==static_cast<unsigned char*>(f.identities[0])+0x80,"selection exact primary textbuffer");
    f.mutate(CallbackPhase::select);
}
void __fastcall fake_refresh(void* primary) {
    auto& f=*current_fixture; ++f.refresh_calls; f.sequence=f.sequence*10+3;
    const bool unlocked=TryAcquireSRWLockExclusive(&state_lock)!=0;
    check(unlocked,"no lifecycle lock across refresh callback");
    if(unlocked) ReleaseSRWLockExclusive(&state_lock);
    check(primary==f.identities[0],"refresh exact primary");
    f.mutate(CallbackPhase::refresh);
}
bool run_focus(FocusFixture& fixture) {
    current_fixture=&fixture;
    return focus_open(fixture.window.data(),fixture.view,7,11,1,fixture.identities,fixture.expected);
}
void focus_tests() {
    void* memory=VirtualAlloc(nullptr,0x3454000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(memory!=nullptr,"owned fake image allocation");
    image_base=reinterpret_cast<std::uintptr_t>(memory);
    find_edit=fake_find_edit; find_instant=fake_find_instant;
    focus_widget=fake_focus; select_range=fake_select; refresh_edit=fake_refresh;
    for(std::size_t model=0;model<model_count;++model) for(const auto length:{std::size_t{1},std::size_t{32},max_codepoints}) {
        FocusFixture f(model,length);
        check(run_focus(f),"every primary/model endpoint accepted");
        check(f.sequence==123 && f.focus_calls==1 && f.select_calls==1 && f.refresh_calls==1,"single ordered native phase calls");
        check(f.begin==0 && f.end==static_cast<int>(length),"selection uses full UTF32 length");
        check(f.focused==f.edit[0].bytes.data(),"never focus second field");
    }
    for(const auto length:{std::size_t{0},max_codepoints+1}) {
        FocusFixture f(0,1); f.expected[0].decoded.size=length;
        check(!run_focus(f) && f.sequence==0,"empty/oversize never call native focus");
    }
    {
        FocusFixture f(0,1); f.edit[0].points[0]=0x1F600; f.expected[0].decoded.points[0]=0x1F600;
        check(run_focus(f) && f.end==1,"current non-alphabet astral scalar selected as one UTF32 point");
    }
    for(unsigned condition=0;condition<5;++condition) {
        FocusFixture f(0,3);
        if(condition==0) put(f.manager.data(),0x158,static_cast<void*>(f.window.data()));
        if(condition==1) put(f.manager.data(),0x230,static_cast<void*>(f.window.data()));
        if(condition==2) put(f.edit[0].bytes.data(),0x271,static_cast<unsigned char>(1));
        if(condition==3) put(reinterpret_cast<void*>(image_base),0x3453230,static_cast<void*>(nullptr));
        if(condition==4) put(f.edit[0].bytes.data(),0x271,static_cast<unsigned char>(2));
        check(!run_focus(f) && f.sequence==0,"foreign widget/receiver/existing focus/unreadable manager/disabled edit preserved");
    }
    for(const auto phase:{CallbackPhase::focus,CallbackPhase::select,CallbackPhase::refresh}) {
        for(const auto mutation:{Mutation::epoch,Mutation::generation,Mutation::root,Mutation::text,
                                Mutation::other_text,Mutation::widget,Mutation::receiver,Mutation::hide,
                                Mutation::edit,Mutation::manager,Mutation::active_bit}) {
            FocusFixture f(1,4); f.phase=phase; f.mutation=mutation;
            check(!run_focus(f),"callback context/text/focus mutation rejected");
            const auto expected=phase==CallbackPhase::focus?1u:phase==CallbackPhase::select?12u:123u;
            check(f.sequence==expected,"no later native phase after callback mutation");
        }
    }
    // Existing opening state is preserved: a failed focus helper never retries
    // the prefill or changes readiness, epoch, pending requests, or text itself.
    FocusFixture f(0,2); put(f.manager.data(),0x158,static_cast<void*>(f.window.data()));
    const auto before=lifecycle[0];
    check(!run_focus(f),"blocked opening remains blocked");
    check(lifecycle[0].epoch==before.epoch && lifecycle[0].generation==before.generation &&
          lifecycle[0].attempted==before.attempted && lifecycle[0].prefilled==before.prefilled,
          "focus helper does not mutate lifecycle");
    check(same_text(f.expected[0].decoded,text(2,0x017B)),"focus helper does not alter copied prefill");
    current_fixture=nullptr; image_base=0;
    find_edit=nullptr; find_instant=nullptr; focus_widget=nullptr; select_range=nullptr; refresh_edit=nullptr;
    check(VirtualFree(memory,0,MEM_RELEASE)!=0,"owned fake image released");
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
    focus_tests();
    std::printf("inline focus/transport: %u checks passed; no hooks/dispatch/native game calls\n",checks);
}
