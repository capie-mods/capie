/** @file macos_game_calls_test.cpp @brief Native call boundary against owned functions only. */
#include "../src/engine/game_calls.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
namespace e = capie::engine;
namespace { unsigned checks=0, failures=0, calls=0; void* answer=nullptr;
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void* lookup(void*, const e::EngineString* name, bool report){++calls;check(!report && e::name_equals(name,"field"),"three native lookup args");return answer;}
void set(void*, const e::EngineString*){++calls;}
void refresh(void*){++calls;}
void caption(void*,int index){++calls;check(index==12,"caption slot preserved");}
void focus(void*,void* receiver){++calls;check(receiver==nullptr,"release focus only");}
}
int main(){
 std::array<unsigned char,0x400> root{},owner{},widget{};
 void* owner_ptr=owner.data(); std::memcpy(root.data()+0x88,&owner_ptr,sizeof(owner_ptr));
 e::find_edit=lookup; e::set_text=set; e::refresh_edit=refresh; e::caption_request=caption; e::focus_switch=focus;
 answer=widget.data();
 check(e::guarded_find(e::find_edit,root.data(),"field")==nullptr && calls==0,"native calls disabled before admission");
 e::native_call_thread=capie::core::platform::thread_id();
 check(e::guarded_find(e::find_edit,root.data(),"field")==answer,"native lookup result");
 answer=owner.data()+0x1b8;
 check(e::guarded_find(e::find_edit,root.data(),"field")==nullptr,"dummy edit rejected");
 e::find_edit=nullptr; e::find_instant=lookup;answer=owner.data()+0x1c0;
 check(e::guarded_find(e::find_instant,root.data(),"field")==nullptr,"dummy instant rejected");
 const auto text=e::borrowed_string("field",5);
 check(e::guarded_set(widget.data(),&text) && e::guarded_refresh(widget.data()),"native set and refresh fixture");
 check(!e::guarded_set(nullptr,&text) && !e::guarded_refresh(nullptr),"null widgets refused");
 check(!e::caption_call(widget.data(),11) && e::caption_call(widget.data(),12),"caption allowed slot only");
 check(e::focus_call_null(widget.data()),"focus fixture");
 const auto before_foreign = calls;
 bool foreign_rejected = false;
 std::thread foreign([&] { foreign_rejected = !e::guarded_refresh(widget.data()) &&
     !e::caption_call(widget.data(),12) && !e::focus_call_null(widget.data()); });
 foreign.join();
 check(foreign_rejected && calls == before_foreign, "foreign thread cannot call engine");
 e::native_call_thread=0;
 check(!e::caption_call(widget.data(),12) && !e::focus_call_null(widget.data()),"call admission revoked");
 std::printf("macOS call boundary: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
