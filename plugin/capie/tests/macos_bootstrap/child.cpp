#include "../../src/bootstrap/macos_bootstrap_api.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
extern "C" long target0(long,long,long,long,long,long,long,double);
struct Triple { long a,b,c; };
extern "C" Triple target1();
#define T(N) extern "C" long target##N();
T(2) T(3) T(4) T(5) T(6) T(7) T(8) T(9) T(10) T(11) T(12) T(13) T(14)
static long replacement(long a,long b,long c,long d,long e,long f,long g,double h) {
    using Original = long(*)(long,long,long,long,long,long,long,double);
    auto get = reinterpret_cast<void*(*)(uint32_t)>(dlsym(RTLD_DEFAULT,"capie_macos_bootstrap_trampoline"));
    return reinterpret_cast<Original>(get(0))(a,b,c,d,e,f,g,h)+100;
}
int main() {
    auto state = reinterpret_cast<uint32_t(*)()>(dlsym(RTLD_DEFAULT,"capie_macos_bootstrap_state"));
    auto set = reinterpret_cast<bool(*)(uint32_t,void*)>(dlsym(RTLD_DEFAULT,"capie_macos_bootstrap_set_dispatch"));
    auto get = reinterpret_cast<void*(*)(uint32_t)>(dlsym(RTLD_DEFAULT,"capie_macos_bootstrap_trampoline"));
    if (!state || !set || !get) return 1;
    bool refused = std::getenv("EXPECT_REFUSAL");
    if (state() != (refused ? 0U : 1U)) return 2;
    if (target0(1,2,3,4,5,6,7,9.0)!=37) return 3;
    Triple result=target1(); if(result.a!=7 || result.b!=8 || result.c!=9) return 4;
    long(*targets[])()={target2,target3,target4,target5,target6,target7,target8,target9,target10,target11,target12,target13,target14};
    for(auto fn:targets)if(fn()!=7)return 5;
    if(get(15) || set(15,nullptr))return 6;
    if(refused) { if(get(0)||set(0,reinterpret_cast<void*>(replacement)))return 7; puts("refused_originals_ok");return 0; }
    for(unsigned i=0;i<15;++i)if(!get(i))return 8;
    if(!set(0,reinterpret_cast<void*>(replacement))||target0(1,2,3,4,5,6,7,9.0)!=137)return 9;
    if(!set(0,nullptr)||target0(1,2,3,4,5,6,7,9.0)!=37)return 10;
    puts("all15_forward_armed_restored_abi_ok");
}
