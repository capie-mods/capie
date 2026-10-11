// Exercises the actual isolated builder without installing hooks or dispatching.
#include "inline_editor_render_bridge.cpp"
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
    std::printf("inline transport: %u checks passed; no hooks/dispatch\n",checks);
}
