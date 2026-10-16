/* OWNED FIXTURE ONLY. Hash/VA supplied at build, never accepts a game profile. */
#include "profile.h"
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
static int (*original)(void);
static _Atomic int armed;
int fixture_installed(void){return original!=0;}
void fixture_arm(void){if(original)atomic_store(&armed,1);}
static int replacement(void){return atomic_load(&armed)?42:original();}
static int hash_matches(const char* path,const unsigned char* expected){
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
 CC_SHA256_CTX state;CC_SHA256_Init(&state);unsigned char buffer[16384],digest[32];ssize_t n;
 while((n=read(fd,buffer,sizeof(buffer)))>0)CC_SHA256_Update(&state,buffer,(CC_LONG)n);
 close(fd);if(n<0)return 0;CC_SHA256_Final(digest,&state);return memcmp(digest,expected,32)==0;
}
static void jump(unsigned char* out,uintptr_t target){
 const unsigned char op[6]={0xff,0x25,0,0,0,0};memcpy(out,op,6);memcpy(out+6,&target,8);
}
__attribute__((constructor)) static void init(void){
 unsigned stage=1;
 const char* inserted=getenv("DYLD_INSERT_LIBRARIES");
 if(!inserted||strcmp(inserted,EXPECTED_INSERTED)||getenv("DYLD_FORCE_FLAT_NAMESPACE")||getenv("DYLD_LIBRARY_PATH")||getenv("DYLD_FRAMEWORK_PATH"))goto refused;
 stage=2;
 unsigned main_index=0,main_count=0;
 for(unsigned i=0;i<_dyld_image_count();++i){const struct mach_header* h=_dyld_get_image_header(i);if(h&&h->filetype==MH_EXECUTE){main_index=i;++main_count;}}
 if(main_count!=1)goto refused;
 const char* path=_dyld_get_image_name(main_index);if(!path||!hash_matches(path,expected_sha)||!hash_matches(EXPECTED_LATER,later_sha))goto refused;
 stage=3;
 const struct mach_header* header=_dyld_get_image_header(main_index);
 if(!header||memcmp(header,expected_header,sizeof(expected_header)))goto refused;
 stage=4;
 const intptr_t slide=_dyld_get_image_vmaddr_slide(main_index);
 const uintptr_t source=EXPECTED_TARGET+slide;
 const size_t page=(size_t)getpagesize();const uintptr_t base=source&~(page-1);
 if(source-base+14>page)goto refused;
 stage=5;
 unsigned char old[14];mach_vm_size_t copied=0;
 if(mach_vm_read_overwrite(mach_task_self(),source,14,(mach_vm_address_t)old,&copied)||copied!=14)goto refused;
 for(unsigned i=0;i<14;++i)if(old[i]!=0x90)goto refused;
 stage=6;
 unsigned char* trampoline=mmap(0,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 if(trampoline==MAP_FAILED)goto refused;
 memcpy(trampoline,old,14);jump(trampoline+14,source+14);
 sys_icache_invalidate(trampoline,28);
 if(mprotect(trampoline,page,PROT_READ|PROT_EXEC)){munmap(trampoline,page);goto refused;}
 stage=7;
 unsigned char patch[14];jump(patch,(uintptr_t)replacement);
 if(mach_vm_protect(mach_task_self(),base,page,0,VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)){munmap(trampoline,page);goto refused;}
 original=(int(*)(void))trampoline;
 if(mach_vm_write(mach_task_self(),source,(vm_offset_t)patch,14))_exit(80);
 sys_icache_invalidate((void*)source,14);
 if(mach_vm_protect(mach_task_self(),base,page,0,VM_PROT_READ|VM_PROT_EXECUTE))_exit(81);
 if(mach_vm_read_overwrite(mach_task_self(),source,14,(mach_vm_address_t)old,&copied)||copied!=14||memcmp(old,patch,14))_exit(82);
 write(1,"bootstrap_installed_forward\n",sizeof("bootstrap_installed_forward\n")-1);return;
refused:
 {char digit=(char)('0'+stage);write(1,&digit,1);}
 write(1,"bootstrap_refused\n",sizeof("bootstrap_refused\n")-1);
}
