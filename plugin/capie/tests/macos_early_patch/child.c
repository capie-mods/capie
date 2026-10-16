#include <dlfcn.h>
#include <stdio.h>
extern int owned_target(void);
__attribute__((constructor)) static void init(void){printf("executable_ctor=%d\n",owned_target());}
int main(void){
 int before=owned_target();
 void (*arm)(void)=(void(*)(void))dlsym(RTLD_DEFAULT,"fixture_arm");
 if(arm)arm();
 printf("main_before=%d main_after=%d\n",before,owned_target());
 return 0;
}
