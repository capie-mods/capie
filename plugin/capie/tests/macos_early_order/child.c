/* Owned executable: hidden target reached through an exported callback. */
#include <stdatomic.h>
#include <unistd.h>
static _Atomic unsigned calls;
static _Atomic unsigned constructed;
__attribute__((visibility("hidden"),noinline)) void fixture_hidden_target(void) {
    atomic_fetch_add(&calls,1);
}
unsigned fixture_observe(void) {return atomic_load(&calls)+100*atomic_load(&constructed);}
void fixture_callback(void) {fixture_hidden_target();}
__attribute__((constructor)) static void initialise(void) {
    atomic_store(&constructed,1);write(1,"executable_ctor\n",sizeof("executable_ctor\n")-1);
}
int main(void) {write(1,"main\n",5);return 0;}
