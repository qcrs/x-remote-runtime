#include <cuda.h>
#include <stdio.h>
#define SHOW(call) do{CUresult r=(call);printf("%s=%d\n",#call,(int)r);fflush(stdout);}while(0)
static void host(void *p){*(volatile int *)p=1;}
static void callback(CUstream s,CUresult r,void *p){(void)s;*(volatile int *)p=r==CUDA_SUCCESS;}
int main(int argc,char **argv){
 CUdevice dev;CUcontext ctx;CUstream stream;CUmodule module;CUfunction kernel;
 if(argc!=2||cuInit(0)||cuDeviceGet(&dev,0)||cuCtxCreate(&ctx,0,dev)||cuStreamCreate(&stream,0))return 1;
 volatile int h=0,c=0;SHOW(cuLaunchHostFunc(stream,host,(void *)&h));SHOW(cuStreamAddCallback(stream,callback,(void *)&c,0));SHOW(cuStreamSynchronize(stream));printf("HOST_FUNC=%d CALLBACK=%d\n",h,c);
 if(cuModuleLoad(&module,argv[1])||cuModuleGetFunction(&kernel,module,"m1s2_lifecycle_kernel"))return 1;
 CUdeviceptr ptr;int input=42,output=0;cuMemAlloc(&ptr,4);cuMemcpyHtoD(ptr,&input,4);void *args[]={&ptr};
 CUlaunchConfig config={0};config.gridDimX=config.gridDimY=config.gridDimZ=1;config.blockDimX=config.blockDimY=config.blockDimZ=1;config.hStream=stream;
 SHOW(cuLaunchKernelEx(&config,kernel,args,NULL));SHOW(cuStreamSynchronize(stream));SHOW(cuMemcpyDtoH(&output,ptr,4));printf("LAUNCH_EX_DATA=%d\n",output);
 CUlaunchAttribute attr={0};attr.id=CU_LAUNCH_ATTRIBUTE_IGNORE;config.attrs=&attr;config.numAttrs=1;SHOW(cuLaunchKernelEx(&config,kernel,args,NULL));
 SHOW(cuDevicePrimaryCtxSetFlags(dev,CU_CTX_SCHED_SPIN));
 cuMemFree(ptr);cuModuleUnload(module);cuStreamDestroy(stream);cuCtxDestroy(ctx);
 unsigned options[]={0,1,2,4,8,16,9,3,32};
 for(unsigned i=0;i<sizeof(options)/sizeof(options[0]);++i){
  CUresult r=cuCtxCreate(&ctx,options[i],dev);printf("CONTEXT_CREATE flags=%u rc=%d\n",options[i],r);
  if(r==CUDA_SUCCESS){unsigned flags=99;SHOW(cuCtxGetFlags(&flags));printf("CONTEXT_FLAGS=%u\n",flags);cuCtxDestroy(ctx);}
 }
 int pageable=-1;SHOW(cuDeviceGetAttribute(&pageable,CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS,dev));printf("PAGEABLE_MEMORY_ACCESS=%d\n",pageable);return 0;
}
