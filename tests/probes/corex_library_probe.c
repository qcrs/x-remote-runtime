#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#define SHOW(call) do {CUresult r=(call); printf("%s=%d\n",#call,(int)r);}while(0)
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc<2)return 2;
 CUdevice device;CUcontext context;CUlibrary library=NULL;
 puts("ENTER_INIT");
 if(cuInit(0)||cuDeviceGet(&device,0)||cuCtxCreate(&context,0,device))return 1;
 puts("INIT_DONE");
 FILE *file=fopen(argv[1],"rb");if(!file)return 1;
 fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 void *image=malloc(size);if(!image||fread(image,1,size,file)!=(size_t)size)return 1;fclose(file);
 for(int from_data=0;from_data<2;++from_data){
  printf("ENTER_LIBRARY_LOAD from_data=%d\n",from_data);
  CUresult result=from_data?cuLibraryLoadData(&library,image,NULL,NULL,0,NULL,NULL,0):
                            cuLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0);
  printf("LIBRARY_LOAD from_data=%d result=%d\n",from_data,result);if(result)return 1;
  unsigned count=0;SHOW(cuLibraryGetKernelCount(&count,library));printf("KERNEL_COUNT=%u\n",count);
  CUkernel kernels[32]={0};SHOW(cuLibraryEnumerateKernels(kernels,32,library));
  for(unsigned i=0;i<count && i<32;++i){const char *name=NULL;if(kernels[i]){SHOW(cuKernelGetName(&name,kernels[i]));printf("KERNEL_NAME=%s\n",name?name:"NONE");}}
  CUmodule module=NULL;SHOW(cuLibraryGetModule(&module,library));
  CUkernel kernel=NULL;CUfunction function=NULL;
  SHOW(cuLibraryGetKernel(&kernel,library,"m1s2_lifecycle_kernel"));
  SHOW(cuKernelGetFunction(&function,kernel));
  SHOW(cuKernelSetAttribute(CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES,0,kernel,device));
  SHOW(cuKernelSetAttribute(CU_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT,0,kernel,device));
  if(argc>2){puts("ENTER_NATIVE_LIBRARY_UNLOAD");SHOW(cuLibraryUnload(library));}
  else puts("NATIVE_UNLOAD_SKIPPED=known non-returning multi-kernel destructor; context cleanup follows");
 }
 free(image);puts("LIBRARY_PROBE=COMPLETE; process exit releases native resources");_Exit(0);
}
