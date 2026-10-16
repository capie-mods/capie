/** @file macos_pins_test.cpp @brief Offline exact-byte and relocated target pin tests. */
#include "../src/core/macos_pins.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
namespace {
unsigned checks=0,failures=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
struct Memory {
 std::uintptr_t base=0x100002000;
 std::array<unsigned char,256> bytes{};
 std::size_t readable=bytes.size();
 bool read(std::uintptr_t address,void* out,std::size_t size) const {
  if(address<base || address-base>readable || size>readable-(address-base))return false;
  std::memcpy(out,bytes.data()+(address-base),size);return true;
 }
 template<class T>void put(std::size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
};
}
int main(){
 namespace pins=capie::core::macos::pins;
 namespace image=capie::core::macos::image;
 image::Image img;img.count=1;img.slide=0x2000;
 img.segments[0]={0x100000000,512,0,256,VM_PROT_READ|VM_PROT_EXECUTE};
 Memory mem;
 const std::array<unsigned char,3> expected{0x55,0x48,0x89};
 std::memcpy(mem.bytes.data(),expected.data(),expected.size());
 const pins::BytesPin bytes{0x100000000,expected};
 std::array<unsigned char,130> long_expected{};
 check(pins::bytes_match(mem,img,{0x100000070,long_expected}),"multi-chunk bytes match");
 mem.bytes[0x70+129]=1;check(!pins::bytes_match(mem,img,{0x100000070,long_expected}),"last byte of third chunk mismatch");mem.bytes[0x70+129]=0;
 check(pins::bytes_match(mem,img,bytes),"exact bytes with slide");
 mem.bytes[1]^=1;check(!pins::bytes_match(mem,img,bytes),"tampered code");mem.bytes[1]^=1;
 check(!pins::bytes_match(mem,img,{bytes.address,{}}),"empty bytes pin");
 check(!pins::bytes_match(mem,img,{0,expected}),"null bytes address");
 check(!pins::bytes_match(mem,img,{0x1000000ff,expected}),"cross file backed range");
 check(!pins::bytes_match(mem,img,{0x100000100,expected}),"zero fill is not pinned");
 mem.readable=2;check(!pins::bytes_match(mem,img,bytes),"truncated read");mem.readable=256;
 const pins::RelativePin forward{0x100000010,0x100000014,0x100000040};
 mem.put(0x10,std::int32_t{0x2c});check(pins::relative_matches(mem,img,forward),"positive rel32 with slide");
 mem.put(0x10,std::int32_t{0x2d});check(!pins::relative_matches(mem,img,forward),"wrong rel32 target");
 const pins::RelativePin backward{0x100000060,0x100000064,0x100000040};
 mem.put(0x60,std::int32_t{-0x24});check(pins::relative_matches(mem,img,backward),"negative rel32 with slide");
 check(!pins::relative_matches(mem,img,{forward.displacement,forward.next_instruction,0x100000200,false}),"target beyond virtual range rejected");
 // RIP-relative instruction has an immediate byte after its displacement.
 const pins::RelativePin rip_data{0x100000020,0x100000025,0x100000180,false};
 mem.put(0x20,std::int32_t{0x15b});
 check(pins::relative_matches(mem,img,rip_data),"RIP data includes trailing immediate and resolves BSS");
 check(!pins::relative_matches(mem,img,{rip_data.displacement,0x100000024,rip_data.target,false}),"incorrect next instruction rejected");
 check(!pins::relative_matches(mem,img,{rip_data.displacement,0x100000023,rip_data.target,false}),"next before displacement end rejected");
 mem.put(0x88,std::uintptr_t{0x100002180});
 check(pins::pointer_matches(mem,img,{0x100000088,0x100000180}),"pointer target may be BSS");
 auto split=img;split.count=2;split.segments[0].memory_size=256;
 split.segments[1]={0x100000100,256,256,0,VM_PROT_READ|VM_PROT_WRITE};
 check(pins::relative_matches(mem,split,rip_data),"code source to separate nonexecutable BSS");
 check(!pins::relative_matches(mem,split,{rip_data.displacement,rip_data.next_instruction,rip_data.target,true}),"branch code target rejects nonexecutable BSS");
 split.segments[1].initprot=0;
 check(!pins::relative_matches(mem,split,rip_data),"RIP target must be readable");
 check(!pins::pointer_matches(mem,split,{0x100000088,0x100000180}),"pointer target must be readable");
 // Runtime values are deliberately separate from pre-init file/code checks.
 auto initialized=img;Memory runtime_mem;runtime_mem.base=0x100002100;
 runtime_mem.put(0x80,std::uint32_t{0x3ab2});
 const std::array runtime_values{pins::RuntimeU32Pin{0x100000180,0x3ab2}};
 check(pins::after_init_values_match(runtime_mem,initialized,runtime_values),"initialized BSS DWORD with slide");
 const std::array runtime_pair{runtime_values[0],pins::RuntimeU32Pin{0x100000184,0x3a70}};
 check(!pins::after_init_values_match(runtime_mem,initialized,runtime_pair),"later required token mismatch rejects set");
 check(!pins::after_init_values_match(runtime_mem,initialized,{}),"empty runtime value set rejected");
 runtime_mem.put(0x80,std::uint32_t{0});
 check(!pins::after_init_values_match(runtime_mem,initialized,runtime_values),"uninitialized token rejected");
 runtime_mem.put(0x80,std::uint32_t{0x3a70});
 check(!pins::after_init_values_match(runtime_mem,initialized,runtime_values),"wrong initialized token rejected");
 runtime_mem.put(0x80,std::uint32_t{0x3ab2});runtime_mem.readable=0x83;
 check(!pins::after_init_values_match(runtime_mem,initialized,runtime_values),"truncated runtime DWORD read");
 runtime_mem.readable=256;initialized.segments[0].initprot=0;
 check(!pins::after_init_values_match(runtime_mem,initialized,runtime_values),"unreadable virtual token segment");
 initialized=img;
 const std::array invalid_value{pins::RuntimeU32Pin{0,0x3ab2}};
 check(!pins::after_init_values_match(runtime_mem,initialized,invalid_value),"null token address");
 const std::array outside_value{pins::RuntimeU32Pin{0x1000001fe,0x3ab2}};
 check(!pins::after_init_values_match(runtime_mem,initialized,outside_value),"DWORD straddles virtual range");
 initialized.slide=-0x1000;runtime_mem.base=0xfffff100;
 check(pins::after_init_values_match(runtime_mem,initialized,runtime_values),"initialized token negative slide");
 const pins::PointerPin pointer{0x100000080,0x100000040};
 mem.put(0x80,std::uintptr_t{0x100002040});check(pins::pointer_matches(mem,img,pointer),"slid pointer target");
 mem.put(0x80,std::uintptr_t{0x100000040});check(!pins::pointer_matches(mem,img,pointer),"unslid pointer rejected");
 mem.put(0x80,std::uintptr_t{0});check(!pins::pointer_matches(mem,img,pointer),"null pointer rejected");
 check(!pins::pointer_matches(mem,img,{pointer.address,0}),"null expected target rejected");
 check(!pins::all_match(mem,img,{},{},{}),"empty required set rejected");
 mem.put(0x80,std::uintptr_t{0x100002040});mem.put(0x10,std::int32_t{0x2c});
 const std::array byte_set{bytes};const std::array rel_set{forward};const std::array ptr_set{pointer};
 check(pins::all_match(mem,img,byte_set,rel_set,ptr_set),"all categories pass");
 mem.put(0x80,std::uintptr_t{0});check(!pins::all_match(mem,img,byte_set,rel_set,ptr_set),"last category failure closes whole set");
 img.segments[0].initprot=VM_PROT_READ;
 mem.put(0x80,std::uintptr_t{0x100002040});
 check(pins::pointer_matches(mem,img,pointer),"data vtable target allowed");
 check(!pins::pointer_matches(mem,img,{pointer.address,pointer.target,true}),"function pointer target must be executable");
 check(!pins::pointer_matches(mem,img,{0x1000000fc,pointer.target}),"pointer source straddles file bytes");
 check(!pins::bytes_match(mem,img,bytes),"code must be executable");
 check(pins::bytes_match(mem,img,{bytes.address,expected,false}),"explicit data bytes may be read only");
 check(!pins::relative_matches(mem,img,forward),"branch displacement must be executable");
 img.segments[0].initprot|=VM_PROT_EXECUTE;
 img.slide=-0x1000;mem.base=0xfffff000;
 check(pins::bytes_match(mem,img,bytes),"negative slide");
 img.slide=0;img.segments[0].address=1;mem.base=1;mem.put(0x10,std::numeric_limits<std::int32_t>::min());
 check(!pins::relative_matches(mem,img,{17,21,65}),"rel32 negative arithmetic underflow");
 img.segments[0].address=std::numeric_limits<std::uintptr_t>::max()-511;
 mem.base=img.segments[0].address;mem.put(0x10,std::numeric_limits<std::int32_t>::max());
 check(!pins::relative_matches(mem,img,{mem.base+16,mem.base+20,mem.base+64}),"rel32 positive arithmetic overflow");
 img.count=image::max_segments+1;check(!pins::bytes_match(mem,img,bytes),"invalid image count");
 std::printf("macOS pins: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
