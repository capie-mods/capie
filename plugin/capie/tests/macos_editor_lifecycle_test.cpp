/** @file macos_editor_lifecycle_test.cpp @brief Shared opening lifecycle on native locks; no game. */
#include "../src/features/editor/lifecycle.hpp"
#include <cstdio>
namespace e=capie::editor;
namespace {unsigned checks=0,failures=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
}
int main(){
 std::uint64_t generation=0;
 check(!e::ready_model(e::model_count,1,generation,false),"invalid model refused");
 auto& state=e::lifecycle[0];
 state={true,true,true,7,3,0};
 check(e::ready_model(0,7,generation,false)&&generation==3,"ready opening returns generation");
 check(!e::ready_model(0,8,generation,true),"foreign epoch refused");
 generation=2;check(!e::ready_model(0,7,generation,true),"stale generation refused");
 e::pending[0].valid=true;e::reset_model(0,true);
 check(!state.visible&&!state.attempted&&!state.prefilled&&state.generation==4&&!e::pending[0].valid,
       "hide rearms prefill and clears copied pending text");
 state={true,true,true,7,5,0};e::reset_model(0,false);
 check(!state.visible&&state.attempted&&!state.prefilled&&state.generation==6,"invalid opening never overwrites draft on retry");
 check(e::reset_bound_model(0)==7&&!state.attempted&&!state.visible&&state.epoch==0,"bind fully resets opening");
 check(!e::ready_model(0,0,generation,false),"reset opening cannot apply");
 std::printf("macOS editor lifecycle: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
