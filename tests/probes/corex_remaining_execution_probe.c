#define _GNU_SOURCE
#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#define SHOW(call) do{ CUresult r=(call);printf("%s=%d\n",#call,(int)r);}while(0)
int main(int argc,char **argv){
 if(argc!=2)return 2;
 CUdevice dev;CUcontext ctx;CUmodule module;CUfunction func;CUlibrary lib=NULL;CUkernel kernel=NULL;
 if(cuInit(0)||cuDeviceGet(&dev,0)||cuCtxCreate(&ctx,0,dev))return 1;
 CUresult (*reset)(void)=dlsym(RTLD_DEFAULT,"cuCtxResetPersistingL2Cache");
 if(reset){SHOW(reset());}else puts("cuCtxResetPersistingL2Cache=SYMBOL_ABSENT");
 SHOW(cuLibraryLoadFromFile(&lib,argv[1],NULL,NULL,0,NULL,NULL,0));
 if(lib){SHOW(cuLibraryGetKernel(&kernel,lib,"m1s2_lifecycle_kernel"));
  if(kernel){CUfunction f=NULL;SHOW(cuKernelGetFunction(&f,kernel));}
  SHOW(cuLibraryUnload(lib));}
 if(cuModuleLoad(&module,argv[1])||cuModuleGetFunction(&func,module,"m1s2_lifecycle_kernel"))return 1;
 CUdeviceptr ptr;int v=41,out=0;cuMemAlloc(&ptr,4);cuMemcpyHtoD(ptr,&v,4);void *args[]={&ptr};
 SHOW(cuLaunchCooperativeKernel(func,1,1,1,1,1,1,0,NULL,args));
 SHOW(cuCtxSynchronize());SHOW(cuMemcpyDtoH(&out,ptr,4));printf("COOPERATIVE_DATA=%d\n",out);
 int supported=-1;SHOW(cuDeviceGetAttribute(&supported,CU_DEVICE_ATTRIBUTE_COOPERATIVE_LAUNCH,dev));printf("COOPERATIVE_ATTRIBUTE=%d\n",supported);
 cuMemFree(ptr);cuModuleUnload(module);cuCtxDestroy(ctx);return 0;
}
