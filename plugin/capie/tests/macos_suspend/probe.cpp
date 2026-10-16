// Owned-process suspension experiment: no code writes, no foreign PID input.
#include <mach/mach.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>
#include <chrono>
struct Shared { std::atomic<unsigned long long> count{0}; std::atomic<bool> ready{false}; std::atomic<int> stage{0}; std::atomic<int> send_result{0}; };
int main() {
    auto* data = static_cast<Shared*>(mmap(nullptr, sizeof(Shared), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0));
    if (data == MAP_FAILED) return 1;
    new(data) Shared;
    mach_port_t channel=MACH_PORT_NULL;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&channel)!=KERN_SUCCESS ||
       mach_port_insert_right(mach_task_self(),channel,channel,MACH_MSG_TYPE_MAKE_SEND)!=KERN_SUCCESS ||
       mach_ports_register(mach_task_self(),&channel,1)!=KERN_SUCCESS) return 6;
    const pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        mach_port_array_t inherited=nullptr;mach_msg_type_number_t count=0;
        if(mach_ports_lookup(mach_task_self(),&inherited,&count)!=KERN_SUCCESS || count<1) _exit(7);
        data->stage.store(1);
        struct Transfer { mach_msg_header_t header; mach_msg_body_t body; mach_msg_port_descriptor_t port; } msg{};
        msg.header.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0)|MACH_MSGH_BITS_COMPLEX;
        msg.header.msgh_size=sizeof(msg);msg.header.msgh_remote_port=inherited[0];
        msg.body.msgh_descriptor_count=1;msg.port.name=mach_task_self();
        msg.port.disposition=MACH_MSG_TYPE_COPY_SEND;msg.port.type=MACH_MSG_PORT_DESCRIPTOR;
        const auto sent=mach_msg(&msg.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(msg),0,MACH_PORT_NULL,1000,MACH_PORT_NULL);
        data->send_result.store(sent);data->stage.store(2);
        if(sent!=KERN_SUCCESS) _exit(8);
        std::thread worker([&]{ for (;;) data->count.fetch_add(1, std::memory_order_relaxed); });
        data->ready.store(true);
        for (;;) { std::thread short_worker([&]{data->count.fetch_add(1);}); short_worker.join(); }
    }
    // Independent watchdog owns only this forked child; no PID is supplied externally.
    const pid_t watchdog = fork();
    if (watchdog == 0) { sleep(20); kill(child,SIGKILL); _exit(0); }
    if (watchdog < 0) {kill(child,SIGKILL);waitpid(child,nullptr,0);return 3;}
    auto cleanup = [&]{kill(child,SIGKILL);kill(watchdog,SIGKILL);waitpid(watchdog,nullptr,0);waitpid(child,nullptr,0);};
    struct Receive { mach_msg_header_t header; mach_msg_body_t body; mach_msg_port_descriptor_t port; mach_msg_max_trailer_t trailer; } received{};
    const auto access=mach_msg(&received.header,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(received),channel,2000,MACH_PORT_NULL);
    if(access!=KERN_SUCCESS || !(received.header.msgh_bits&MACH_MSGH_BITS_COMPLEX) ||
       received.body.msgh_descriptor_count!=1 || received.port.type!=MACH_MSG_PORT_DESCRIPTOR) {
        std::printf("{\"access\":\"transfer_denied_or_unavailable\",\"mach_error\":%d,\"cycles\":0,\"child_stage\":%d,\"send_result\":%d}\n",access,data->stage.load(),data->send_result.load());
        cleanup();return 0;
    }
    const mach_port_t task=received.port.name;
    for (int i=0;i<1000 && !data->ready.load();++i) usleep(1000);
    if (!data->ready.load()) {cleanup();return 4;}
    unsigned cycles=0;const char* status="pass";
    for (;cycles<100;++cycles) {
        if(task_suspend(task)!=KERN_SUCCESS){status="suspend_failed";break;}
        const auto begin=std::chrono::steady_clock::now();
        auto before=data->count.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto after=data->count.load();
        const auto resumed=task_resume(task);
        const auto elapsed=std::chrono::steady_clock::now()-begin;
        if(elapsed>std::chrono::milliseconds(100)){status="hold_deadline_exceeded";break;}
        if(resumed!=KERN_SUCCESS){status="resume_failed";break;}
        if(before!=after){status="counter_changed_while_suspended";break;}
        for(int i=0;i<1000 && data->count.load()==after;++i) usleep(1000);
        if(data->count.load()==after){status="no_progress_after_resume";break;}
    }
    std::printf("{\"access\":\"granted\",\"status\":\"%s\",\"cycles\":%u,\"requested_hold_ms\":10}\n",status,cycles);
    mach_port_deallocate(mach_task_self(),task);cleanup();
    return cycles==100?0:5;
}
