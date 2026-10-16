/** @file macos_widgets_test.cpp @brief Offline hostile-memory tests for macOS root guards. */
#include "../src/engine/macos_widgets.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
namespace {
unsigned checks=0, failures=0;
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
struct Memory {
 static constexpr std::uintptr_t base=0x1000;
 std::array<unsigned char,0x3000> bytes{};
 std::size_t mapped=bytes.size();
 bool read(std::uintptr_t p,void* out,std::size_t n) const {
  if(p<base || p-base>mapped || n>mapped-(p-base))return false;
  std::memcpy(out,bytes.data()+p-base,n);return true;
 }
 template<class T> void put(std::size_t off,T value){std::memcpy(bytes.data()+off,&value,sizeof(value));}
 void list(std::size_t off,std::uintptr_t data,int count,int capacity){put(off,data);put(off+8,capacity);put(off+12,count);}
};
constexpr capie::engine::macos::WidgetVtables vtables{0x8000,0x8010,0x8020};
Memory fixture(){
 Memory m;m.put(0,vtables.root);
 for(auto off:{0xe20,0xe80})m.list(off,0x2000,1,1);
 for(auto off:{0xe38,0xe98})m.list(off,0x2100,1,1);
 m.put(0x1000,std::uintptr_t{0x2200});m.put(0x1200,vtables.instant);
 // Separate edit pointer and widget.
 m.list(0xe80,0x2300,1,1);m.put(0x1300,std::uintptr_t{0x2400});m.put(0x1400,vtables.edit);
 m.bytes[0x1100]=6;std::memcpy(m.bytes.data()+0x1101,"abc",4);return m;
}
}
int main(){
 namespace w=capie::engine::macos;
 auto valid=[](const Memory& m){return w::root_guard(m,Memory::base,vtables);};
 Memory m=fixture();check(valid(m),"four valid arrays");
 m.list(0xe20,0,0,0);m.list(0xe38,0,0,0);check(valid(m),"empty paired arrays");
 m=fixture();m.put(0xe44,0);check(!valid(m),"paired counts mismatch");
 for(auto off:{0xe20,0xe38,0xe80,0xe98}){
  m=fixture();m.put(off+12,-1);check(!valid(m),"negative count");
  m=fixture();m.put(off+8,-1);check(!valid(m),"negative capacity");
  m=fixture();m.put(off+8,0);check(!valid(m),"count exceeds capacity");
  m=fixture();m.put(off+12,4097);m.put(off+8,4097);check(!valid(m),"count bound");
  m=fixture();m.put(off+8,65537);check(!valid(m),"capacity bound");
  m=fixture();m.put(off,std::uintptr_t{0});check(!valid(m),"nonempty null data");
  m=fixture();m.put(off,std::numeric_limits<std::uintptr_t>::max()-3);check(!valid(m),"array address overflow");
 }
 m=fixture();m.mapped=0xeaf;check(!valid(m),"full prefix required");
 m=fixture();m.list(0xe98,Memory::base+m.bytes.size()-8,1,1);check(!valid(m),"name stride is24 not8");
 m=fixture();m.put(0x1000,std::uintptr_t{0});check(!valid(m),"null widget");
 m=fixture();m.put(0x1200,vtables.edit);check(!valid(m),"wrong instant vtable");
 m=fixture();m.put(0,vtables.edit);check(!valid(m),"wrong root vtable");
 m=fixture();m.bytes[0x1104]=1;check(!valid(m),"missing name terminator");
 m=fixture();m.bytes[0x1102]=0;check(!valid(m),"embedded NUL name");
 m=fixture();m.put(0x1100,std::uint64_t{33});m.put(0x1108,std::uint64_t{3});m.put(0x1110,std::uintptr_t{0x2500});std::memcpy(m.bytes.data()+0x1500,"abc",4);check(valid(m),"long names through policy");
 m.mapped=0x1503;check(!valid(m),"long terminator unreadable");
 check(!w::root_guard(m,0,vtables),"null root");
 check(!w::root_guard(m,std::numeric_limits<std::uintptr_t>::max()-3,vtables),"root overflow");
 m=fixture();check(!w::root_guard(m,Memory::base,w::WidgetVtables{}),"missing verified vtables");
 std::printf("macOS widgets: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
