#include <dlfcn.h>
#include <stdio.h>
__attribute__((constructor)) static void init(void){
 int (*target)(void)=(int(*)(void))dlsym(RTLD_DEFAULT,"owned_target");
 int (*installed)(void)=(int(*)(void))dlsym(RTLD_DEFAULT,"fixture_installed");
 printf("later_ctor=%d installed=%d\n",target?target():-1,installed?installed():0);
}
