/* Minimal inserted observer, no target writes or hook installation. */
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <mach/mach.h>
#ifdef WITH_DEPENDENCY
extern int fixture_dependency(void);
#endif
__attribute__((constructor)) static void initialise(void) {
#ifdef WITH_DEPENDENCY
    if(fixture_dependency()!=1)_exit(20);
#endif
    unsigned (*observe)(void)=(unsigned(*)(void))dlsym(RTLD_DEFAULT,"fixture_observe");
    if(!observe)_exit(21);
    thread_act_array_t threads=0;mach_msg_type_number_t count=0;
    if(task_threads(mach_task_self(),&threads,&count)!=KERN_SUCCESS)_exit(22);
    char out[100];int size=snprintf(out,sizeof(out),"first_ctor observed=%u threads=%u\n",observe(),count);
    write(1,out,size);
    for(unsigned i=0;i<count;++i)mach_port_deallocate(mach_task_self(),threads[i]);
    vm_deallocate(mach_task_self(),(vm_address_t)threads,count*sizeof(thread_t));
}
