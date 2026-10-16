/* Known fixture dependency calls an unexported target via an exported callback. */
#include <dlfcn.h>
#include <unistd.h>
int fixture_dependency(void) {return 1;}
__attribute__((constructor)) static void initialise(void) {
    void (*callback)(void)=(void(*)(void))dlsym(RTLD_DEFAULT,"fixture_callback");
    if(!callback)_exit(23);
    callback();write(1,"later_ctor target_called\n",sizeof("later_ctor target_called\n")-1);
}
