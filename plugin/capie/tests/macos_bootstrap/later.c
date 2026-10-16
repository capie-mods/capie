#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void later(void){
 unsigned(*state)(void)=dlsym(RTLD_DEFAULT,"capie_macos_bootstrap_state");
 if(!state || state()!=(getenv("EXPECT_REFUSAL")?0U:1U))exit(20);
 puts("later_initializer_state_ok");
}
