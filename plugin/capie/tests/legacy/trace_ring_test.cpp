#include "../compat/rn_trace.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool value,const char* label) {
    ++checks;
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
}
}
int main() {
    using namespace rn::debug;
    auto ring=std::make_unique<Ring>();
    auto dump=std::make_unique<Dump>(); Stamp stamp{};
    ring->copy(*dump); require(dump->count==0,"empty ring");
    char text[64]{};
    for(unsigned i=0;i<300;++i) {
        std::snprintf(text,sizeof(text),"record %u",i);
        require(ring->append(text,std::strlen(text),1000+i,7,stamp),"append enabled");
    }
    ring->copy(*dump);
    require(dump->count==256 && dump->overwritten==44,"bounded rolling window");
    for(std::size_t i=0;i<dump->count;++i) {
        const auto& entry=dump->entries[i];
        std::snprintf(text,sizeof(text),"record %zu",i+44);
        require(entry.stamp.sequence==i+45 && entry.stamp.ms==1044+i && entry.stamp.thread==7 &&
            !entry.stamp.truncated && std::strcmp(entry.text,text)==0,"oldest-to-newest coherent copied data");
    }
    ring->arm(false); require(!ring->append("ignored",7,1,1,stamp),"paused does not replace history");
    require(ring->append("forced",6,2,2,stamp,true),"explicit force allowed");
    ring->arm(true);
    std::array<char,line_capacity+20> long_text{}; long_text.fill('a');
    ring->append(long_text.data(),long_text.size(),3,3,stamp);
    ring->copy(*dump);
    const auto& truncated=dump->entries[255];
    require(truncated.stamp.truncated && truncated.size==line_capacity-1 && truncated.text[line_capacity-1]==0,"truncation explicit and terminated");

    Control control;
    require(!control.request(static_cast<Operation>(0),0) && !control.request(static_cast<Operation>(5),0),"unknown operations rejected");
    require(control.request(Operation::snapshot,100),"snapshot request");
    auto work=control.take(100,0);
    require(work.serial==1 && work.changed && work.pending==7 && !work.snapshot && !work.dump,"unrelated tick keeps all models pending");
    work=control.take(101,3); require(!work.snapshot && work.pending==7,"multi-bit cannot consume snapshot");
    work=control.take(101,8); require(!work.snapshot && work.pending==7,"foreign bit cannot consume snapshot");
    work=control.take(102,4); require(work.snapshot && work.pending==3 && !work.dump,"out-of-order country");
    work=control.take(103,4); require(!work.snapshot && work.pending==3,"same model consumed once");
    work=control.take(104,1); require(work.snapshot && work.pending==2,"leader");
    work=control.take(105,2); require(work.snapshot && !work.pending && work.dump,"last model releases queued dump");
    work=control.take(106,2); require(!work.snapshot && !work.dump && !work.changed,"no heartbeat or automatic retry");
    control.request(Operation::snapshot,200);
    work=control.take(10199,1); require(work.snapshot && !work.expired,"before TTL");
    work=control.take(10200,2); require(!work.snapshot && work.expired==6 && !work.pending && work.dump,"TTL reports missing models explicitly");
    control.request(Operation::snapshot,30000); control.request(Operation::snapshot,30001);
    work=control.take(30002,1); require(work.serial==4 && work.pending==6 && work.changed,"latest request coalesces and re-arms model bits");
    control.request(Operation::pause,30003);
    work=control.take(30004,2); require(work.operation==Operation::pause && !work.snapshot && !work.pending && work.dump,"pause cancels snapshots and requests final copied dump");
    control.request(Operation::resume,30005);
    work=control.take(30006,1); require(work.operation==Operation::resume && work.changed && !work.snapshot && !work.dump,"resume does not synthesize snapshot");
    control.request(Operation::dump,30007);
    work=control.take(30008,0); require(work.dump && !work.snapshot,"dump can complete on unrelated post-Tick");
    control.request(Operation::snapshot,(std::numeric_limits<std::uint64_t>::max)()-5);
    work=control.take((std::numeric_limits<std::uint64_t>::max)()-1,1); require(work.snapshot && !work.expired,"deadline overflow saturates");
    work=control.take((std::numeric_limits<std::uint64_t>::max)(),0); require(work.expired==6 && work.dump,"saturated deadline expires");

    auto concurrent=std::make_unique<Ring>(); std::atomic<bool> done{false},coherent{true};
    std::thread reader([&] {
        auto snapshot=std::make_unique<Dump>();
        do {
            concurrent->copy(*snapshot);
            for(std::size_t i=0;i<snapshot->count;++i) {
                const auto& entry=snapshot->entries[i]; unsigned producer=0,iteration=0;
                if(std::sscanf(entry.text,"producer=%u iteration=%u",&producer,&iteration)!=2 ||
                   entry.stamp.thread!=producer || entry.stamp.ms!=iteration || entry.size!=std::strlen(entry.text) ||
                   (i && entry.stamp.sequence!=snapshot->entries[i-1].stamp.sequence+1)) coherent=false;
            }
            std::this_thread::yield();
        } while(!done.load());
    });
    std::vector<std::thread> writers;
    for(unsigned producer=1;producer<=4;++producer) writers.emplace_back([&,producer] {
        for(unsigned iteration=0;iteration<1000;++iteration) {
            char payload[64]{}; Stamp entry_stamp{};
            std::snprintf(payload,sizeof(payload),"producer=%u iteration=%u",producer,iteration);
            concurrent->append(payload,std::strlen(payload),iteration,producer,entry_stamp);
        }
    });
    for(auto& writer:writers) writer.join(); done=true; reader.join();
    concurrent->copy(*dump);
    require(coherent && dump->count==256 && dump->overwritten==3744 && dump->entries[255].stamp.sequence==4000,"concurrent writers and snapshots remain coherent");
    Control concurrent_control; writers.clear();
    for(unsigned i=0;i<4;++i) writers.emplace_back([&] { for(unsigned j=0;j<100;++j) concurrent_control.request(Operation::snapshot,j); });
    for(auto& writer:writers) writer.join();
    work=concurrent_control.take(200,0);
    require(work.serial==400 && work.pending==7 && work.changed,"concurrent requests serialized and coalesced");
    std::printf("debug trace: %u checks passed\n", checks);
    std::puts("debug trace: ring wrap/pause/truncation/concurrency and scalar queue model-order/coalescing/TTL PASS; no game");
}
