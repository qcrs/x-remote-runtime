#include <cuda.h>
#include <stdio.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;setvbuf(stdout,NULL,_IONBF,0);
 CUdevice device;CUcontext context;CUmodule module=NULL;CUdeviceptr pointer=0;size_t bytes=0;
 if(cuInit(0)||cuDeviceGet(&device,0)||cuCtxCreate(&context,0,device)||cuModuleLoad(&module,argv[1]))return 1;
 CUresult result=cuModuleGetGlobal(&pointer,&bytes,module,"runtime_symbol_probe");
 printf("cuModuleGetGlobal=%d bytes=%zu pointer_nonnull=%d\n",result,bytes,pointer!=0);
 if(result==CUDA_SUCCESS){int value=-1;result=cuMemcpyDtoH(&value,pointer,sizeof(value));printf("SYMBOL_READBACK result=%d value=%d\n",result,value);}
 printf("cuModuleUnload=%d\n",cuModuleUnload(module));cuCtxDestroy(context);return 0;
}
