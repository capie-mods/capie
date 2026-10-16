/* Read-only identity diagnostic for owned fixture; never writes executable memory. */
#include <mach-o/dyld.h>
#include <stdio.h>
__attribute__((constructor)) static void init(void){
 for(unsigned i=0;i<_dyld_image_count();++i){
  const struct mach_header* h=_dyld_get_image_header(i);
  if(i<3||h->filetype==MH_EXECUTE)printf("identity index=%u type=%u name=%s\n",i,h->filetype,_dyld_get_image_name(i));
 }
}
