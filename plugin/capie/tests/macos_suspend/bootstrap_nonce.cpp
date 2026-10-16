/** @file nonce.cpp
 * @brief Owned-child endpoint identity test: no task-port transfer, suspension or code writes.
 */
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
struct Message { mach_msg_header_t header; unsigned long long nonce; };
struct ChildResult { int lookup; unsigned count; unsigned type; int sent; };
int main() {
    mach_port_t channel=0;
    if(mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&channel) ||
       mach_port_insert_right(mach_task_self(),channel,channel,MACH_MSG_TYPE_MAKE_SEND)) return 1;
    name_t service{};std::snprintf(service,sizeof(service),"org.capie.owned-nonce.%d",getpid());
    const auto registration=bootstrap_register(bootstrap_port,service,channel);
    if(registration){std::printf("{\"registration\":%d}\n",registration);return 5;}
    int pipefd[2];if(pipe(pipefd))return 2;
    auto child=fork();if(child<0)return 3;
    constexpr unsigned long long nonce=0x43415049454e4f4e;
    if(!child) {
        close(pipefd[0]);ChildResult result{};
        mach_port_t endpoint=0;
        result.lookup=bootstrap_look_up(bootstrap_port,service,&endpoint);result.count=1;
        if(!result.lookup) {
            mach_port_type(mach_task_self(),endpoint,&result.type);
            Message msg{};msg.header.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
            msg.header.msgh_size=sizeof(msg);msg.header.msgh_remote_port=endpoint;
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
    mach_port_destroy(mach_task_self(),channel);
    mach_port_t stale=0;int lookup_after=0;
    for(int i=0;i<100;++i){lookup_after=bootstrap_look_up(bootstrap_port,service,&stale);if(lookup_after!=0)break;mach_port_deallocate(mach_task_self(),stale);usleep(1000);}
    std::printf("{\"lookup_after_receive_destroy\":%d}\n",lookup_after);
    return pass && lookup_after==BOOTSTRAP_UNKNOWN_SERVICE?0:4;
}
