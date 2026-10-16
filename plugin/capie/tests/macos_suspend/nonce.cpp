/** @file nonce.cpp
 * @brief Owned-child endpoint identity test: no task-port transfer, suspension or code writes.
 */
#include <mach/mach.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
struct Message { mach_msg_header_t header; unsigned long long nonce; };
struct ChildResult { int lookup; unsigned count; unsigned type; int sent; };
int main() {
    mach_port_t channel=0;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&channel) ||
       mach_port_insert_right(mach_task_self(),channel,channel,MACH_MSG_TYPE_MAKE_SEND) ||
       mach_ports_register(mach_task_self(),&channel,1)) return 1;
    int pipefd[2];if(pipe(pipefd))return 2;
    auto child=fork();if(child<0)return 3;
    constexpr unsigned long long nonce=0x43415049454e4f4e;
    if(!child) {
        close(pipefd[0]);ChildResult result{};
        mach_port_array_t ports=nullptr;mach_msg_type_number_t count=0;
        result.lookup=mach_ports_lookup(mach_task_self(),&ports,&count);result.count=count;
        if(!result.lookup && count>0) {
            mach_port_type(mach_task_self(),ports[0],&result.type);
            Message msg{};msg.header.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
            msg.header.msgh_size=sizeof(msg);msg.header.msgh_remote_port=ports[0];
            msg.header.msgh_id=0x4341;msg.nonce=nonce;
            result.sent=mach_msg(&msg.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(msg),0,0,1000,0);
        } else result.sent=-1;
        write(pipefd[1],&result,sizeof(result));_exit(0);
    }
    close(pipefd[1]);
    struct {Message msg;mach_msg_max_trailer_t trailer;} response{};
    const auto received=mach_msg(&response.msg.header,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(response),channel,2000,0);
    kill(child,SIGKILL);int status=0;waitpid(child,&status,0);
    ChildResult result{};const auto bytes=read(pipefd[0],&result,sizeof(result));close(pipefd[0]);
    const bool pass=received==0 && response.msg.nonce==nonce && response.msg.header.msgh_id==0x4341;
    std::printf("{\"receive\":%d,\"child_report_bytes\":%ld,\"lookup\":%d,\"registered_count\":%u,\"send_type\":%u,\"send\":%d,\"nonce_matches\":%s}\n",received,bytes,result.lookup,result.count,result.type,result.sent,pass?"true":"false");
    return pass?0:4;
}
