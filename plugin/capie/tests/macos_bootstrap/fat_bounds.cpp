/** Owned parser bounds checks; no process reads or code mutation. */
#include "../../src/bootstrap/macos_bootstrap.cpp"
#include <cstdio>
static void put(std::array<unsigned char, 256>& data, size_t at, uint64_t value, unsigned width=4) {
    for (unsigned i=0;i<width;++i) { data[at+width-1-i]=static_cast<unsigned char>(value);value>>=8; }
}
int main() {
    std::array<unsigned char,256> data{};
    unsigned checks=0;
    const auto check=[&](bool ok){++checks;return ok;};
    if(!check(x86_slice({data.data(),31}).empty()))return 1;
    put(data,0,0xcafebabe);put(data,4,1);put(data,8,CPU_TYPE_X86_64);put(data,16,64);put(data,20,64);
    if(!check(x86_slice(data).data()==data.data()+64 && x86_slice(data).size()==64))return 2;
    put(data,16,16);if(!check(x86_slice(data).empty()))return 3;
    put(data,16,240);if(!check(x86_slice(data).empty()))return 4;
    put(data,16,64);put(data,8,CPU_TYPE_ARM64);if(!check(x86_slice(data).empty()))return 5;
    put(data,8,CPU_TYPE_X86_64);put(data,4,2);put(data,28,CPU_TYPE_X86_64);put(data,36,128);put(data,40,64);
    if(!check(x86_slice(data).empty()))return 6;
    put(data,4,65);if(!check(x86_slice(data).empty()))return 7;
    data.fill(0);put(data,0,0xcafebabf);put(data,4,1);put(data,8,CPU_TYPE_X86_64);put(data,16,64,8);put(data,24,64,8);
    if(!check(x86_slice(data).data()==data.data()+64 && x86_slice(data).size()==64))return 8;
    put(data,16,UINT64_MAX,8);if(!check(x86_slice(data).empty()))return 9;
    put(data,16,64,8);put(data,24,UINT64_MAX,8);if(!check(x86_slice(data).empty()))return 10;
    std::printf("fat_bounds_checks=%u\n",checks);
}
