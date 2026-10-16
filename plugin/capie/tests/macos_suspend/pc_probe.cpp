// Owned-process suspension experiment: no code writes, no foreign PID input.
#include <mach/mach.h>
#include <mach/i386/thread_status.h>
// SDK hides ARM structs for x86 clients; query flavor6 into generic buffer without decoding.
#include <servers/bootstrap.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>
#include <chrono>
extern "C" void controlled_worker(void*);
extern "C" const char controlled_start[], controlled_end[];
asm(".text\n.globl _controlled_worker,_controlled_start,_controlled_end\n_controlled_worker:\n_controlled_start:\nlock incq (%rdi)\njmp _controlled_start\n_controlled_end:\nret\n");
struct Shared { std::atomic<unsigned long long> count{0}; std::atomic<bool> ready{false}; std::atomic<int> stage{0}; std::atomic<int> send_result{0}; std::atomic<long long> deadline{0}; };
long long now_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main() {
    auto* data = static_cast<Shared*>(mmap(nullptr, sizeof(Shared), PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0));
    if (data == MAP_FAILED) return 1;
    new(data) Shared;
    mach_port_t channel=MACH_PORT_NULL;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&channel)!=KERN_SUCCESS ||
       mach_port_insert_right(mach_task_self(),channel,channel,MACH_MSG_TYPE_MAKE_SEND)!=KERN_SUCCESS) return 6;
    name_t service{};std::snprintf(service,sizeof(service),"org.capie.owned-hold.%d",getpid());
    if(bootstrap_register(bootstrap_port,service,channel)!=KERN_SUCCESS)return 6;
    const pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        mach_port_t endpoint=0;
        if(bootstrap_look_up(bootstrap_port,service,&endpoint)!=KERN_SUCCESS) _exit(7);
        data->stage.store(1);
        struct Transfer { mach_msg_header_t header; mach_msg_body_t body; mach_msg_port_descriptor_t port; } msg{};
        msg.header.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0)|MACH_MSGH_BITS_COMPLEX;
        msg.header.msgh_size=sizeof(msg);msg.header.msgh_remote_port=endpoint;
        msg.body.msgh_descriptor_count=1;msg.port.name=mach_task_self();
        msg.port.disposition=MACH_MSG_TYPE_COPY_SEND;msg.port.type=MACH_MSG_PORT_DESCRIPTOR;
        const auto sent=mach_msg(&msg.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(msg),0,MACH_PORT_NULL,1000,MACH_PORT_NULL);
        data->send_result.store(sent);data->stage.store(2);
        if(sent!=KERN_SUCCESS) _exit(8);
        std::thread worker([&]{ controlled_worker(&data->count); });
        data->ready.store(true);
        for (;;) pause();
    }
    // Independent watchdog owns only this forked child; no PID is supplied externally.
    const pid_t watchdog = fork();
    if (watchdog == 0) {
        const auto overall=now_ns()+20000000000LL;
        for(;;){const auto limit=data->deadline.load();if(now_ns()>overall || (limit!=0 && now_ns()>limit)){kill(child,SIGKILL);_exit(0);}usleep(1000);}
    }
    if (watchdog < 0) {kill(child,SIGKILL);waitpid(child,nullptr,0);return 3;}
    auto cleanup = [&]{kill(child,SIGKILL);kill(watchdog,SIGKILL);waitpid(watchdog,nullptr,0);waitpid(child,nullptr,0);mach_port_destroy(mach_task_self(),channel);};
    struct Receive { mach_msg_header_t header; mach_msg_body_t body; mach_msg_port_descriptor_t port; mach_msg_max_trailer_t trailer; } received{};
    const auto access=mach_msg(&received.header,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(received),channel,2000,MACH_PORT_NULL);
    if(access!=KERN_SUCCESS || !(received.header.msgh_bits&MACH_MSGH_BITS_COMPLEX) ||
       received.body.msgh_descriptor_count!=1 || received.port.type!=MACH_MSG_PORT_DESCRIPTOR) {
        std::printf("{\"access\":\"transfer_denied_or_unavailable\",\"mach_error\":%d,\"cycles\":0,\"child_stage\":%d,\"send_result\":%d}\n",access,data->stage.load(),data->send_result.load());
        cleanup();return 0;
    }
    const mach_port_t task=received.port.name;
    int identity_pid=0;
    if(pid_for_task(task,&identity_pid)!=KERN_SUCCESS || identity_pid!=child){cleanup();return 9;}
    for (int i=0;i<1000 && !data->ready.load();++i) usleep(1000);
    if (!data->ready.load()) {cleanup();return 4;}
    unsigned cycles=0;const char* status="pass";long long maximum_hold_ns=0;
    unsigned guest_ok=0,guest_failed=0,host_ok=0,host_failed=0,guest_inside=0;int guest_error=0,host_error=0;unsigned host_words_min=9999,host_words_max=0;
    for (;cycles<100;++cycles) {
        data->deadline.store(now_ns()+100000000LL);
        if(task_suspend(task)!=KERN_SUCCESS){status="suspend_failed";break;}
        const auto begin=std::chrono::steady_clock::now();
        thread_act_array_t threads=nullptr;mach_msg_type_number_t count=0;
        if(task_threads(task,&threads,&count)==KERN_SUCCESS){
            for(unsigned i=0;i<count;++i){
                x86_thread_state64_t guest{};mach_msg_type_number_t guest_count=x86_THREAD_STATE64_COUNT;
                const auto gr=thread_get_state(threads[i],x86_THREAD_STATE64,reinterpret_cast<thread_state_t>(&guest),&guest_count);
                if(gr==KERN_SUCCESS && guest_count==x86_THREAD_STATE64_COUNT){++guest_ok;if(guest.__rip>=reinterpret_cast<uintptr_t>(controlled_start)&&guest.__rip<reinterpret_cast<uintptr_t>(controlled_end))++guest_inside;}else{++guest_failed;guest_error=gr;}
                thread_state_data_t host{};mach_msg_type_number_t host_count=THREAD_STATE_MAX;
                const auto hr=thread_get_state(threads[i],6,host,&host_count);
                if(hr==KERN_SUCCESS){++host_ok;host_words_min=std::min(host_words_min,host_count);host_words_max=std::max(host_words_max,host_count);}else{++host_failed;host_error=hr;}
                mach_port_deallocate(mach_task_self(),threads[i]);
            }
            vm_deallocate(mach_task_self(),reinterpret_cast<vm_address_t>(threads),count*sizeof(thread_t));
        }

        auto before=data->count.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto after=data->count.load();
        const auto resumed=task_resume(task);
        data->deadline.store(0);
        const auto elapsed=std::chrono::steady_clock::now()-begin;
        maximum_hold_ns=std::max(maximum_hold_ns,static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
        if(elapsed>std::chrono::milliseconds(100)){status="hold_deadline_exceeded";break;}
        if(resumed!=KERN_SUCCESS){status="resume_failed";break;}
        if(before!=after){status="counter_changed_while_suspended";break;}
        for(int i=0;i<1000 && data->count.load()==after;++i) usleep(1000);
        if(data->count.load()==after){status="no_progress_after_resume";break;}
    }
    std::printf("{\"access\":\"granted\",\"status\":\"%s\",\"cycles\":%u,\"requested_hold_ms\":10,\"maximum_hold_ns\":%lld}\n",status,cycles,maximum_hold_ns);
    std::printf("{\"guest_ok\":%u,\"guest_failed\":%u,\"guest_error\":%d,\"guest_inside_controlled_loop\":%u,\"host_ok\":%u,\"host_failed\":%u,\"host_error\":%d}\n",guest_ok,guest_failed,guest_error,guest_inside,host_ok,host_failed,host_error);
    std::printf("{\"flavor6_words_min\":%u,\"flavor6_words_max\":%u}\n",host_words_min,host_words_max);
    mach_port_deallocate(mach_task_self(),task);cleanup();
    return cycles==100?0:5;
}
