// Owned-process suspension experiment: no code writes, no foreign PID input.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <cstring>
#include <libkern/OSCacheControl.h>
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
struct Shared { std::atomic<unsigned long long> count{0}; std::atomic<bool> ready{false}; std::atomic<int> stage{0}; std::atomic<int> send_result{0}; std::atomic<long long> deadline{0}; std::atomic<uintptr_t> code{0}; std::atomic<int> command{0},value{0}; };
long long now_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char**) {
    const bool local_flush=argc==2;
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
        const auto page=static_cast<size_t>(getpagesize());
        auto* code=static_cast<unsigned char*>(mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));
        if(code==MAP_FAILED)_exit(10);
        const unsigned char initial[]={0xb8,7,0,0,0,0xc3};std::memcpy(code,initial,sizeof(initial));
        if(mprotect(code,page,PROT_READ|PROT_EXEC)!=0)_exit(11);
        auto function=reinterpret_cast<int(*)()>(code);
        for(unsigned i=0;i<10000;++i)if(function()!=7)_exit(12);
        data->code.store(reinterpret_cast<uintptr_t>(code));data->ready.store(true);
        while(data->command.load()==0)usleep(1000);
        if(local_flush)sys_icache_invalidate(code,6);
        data->value.store(function());data->command.store(2);
        for(;;)pause();
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
    const auto address=data->code.load();const auto page=static_cast<size_t>(getpagesize());
    data->deadline.store(now_ns()+100000000LL);
    const auto hold=task_suspend(task);
    int protection=-1,written=-1,cache=-1,restored=-1,resumed=-1;bool readback=false;
    if(hold==KERN_SUCCESS){
        protection=mach_vm_protect(task,address,page,false,VM_PROT_READ|VM_PROT_WRITE);
        unsigned char replacement[]={0xb8,42,0,0,0,0xc3};
        if(protection==KERN_SUCCESS)written=mach_vm_write(task,address,reinterpret_cast<vm_offset_t>(replacement),sizeof(replacement));
        unsigned char actual[6]{};mach_vm_size_t copied=0;
        readback=mach_vm_read_overwrite(task,address,sizeof(actual),reinterpret_cast<mach_vm_address_t>(actual),&copied)==KERN_SUCCESS&&copied==sizeof(actual)&&std::memcmp(actual,replacement,sizeof(actual))==0;
        vm_machine_attribute_val_t value=MATTR_VAL_CACHE_FLUSH;
        if(written==KERN_SUCCESS)cache=mach_vm_machine_attribute(task,address,sizeof(replacement),MATTR_CACHE,&value);
        if(protection==KERN_SUCCESS)restored=mach_vm_protect(task,address,page,false,VM_PROT_READ|VM_PROT_EXECUTE);
        if(readback&&cache==KERN_SUCCESS&&restored==KERN_SUCCESS){data->command.store(1);resumed=task_resume(task);}
    }
    data->deadline.store(0);
    if(resumed==KERN_SUCCESS)for(int i=0;i<1000&&data->command.load()!=2;++i)usleep(1000);
    const bool pass=resumed==KERN_SUCCESS&&data->command.load()==2&&data->value.load()==42;
    std::printf("{\"hold\":%d,\"protect_rw\":%d,\"write\":%d,\"remote_cache_attribute\":%d,\"restore_rx\":%d,\"resume\":%d,\"observed_value\":%d,\"pass\":%s}\n",hold,protection,written,cache,restored,resumed,data->value.load(),pass?"true":"false");
    std::printf("{\"readback_matches_42\":%s,\"child_local_flush\":%s}\n",readback?"true":"false",local_flush?"true":"false");
    mach_port_deallocate(mach_task_self(),task);cleanup();
    return pass?0:5;
}
