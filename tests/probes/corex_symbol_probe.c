#include <cuda.h>
#include <stdio.h>
#define CHECK(call) do { CUresult rc=(call); printf("%s=%d\n",#call,rc); if(rc!=CUDA_SUCCESS)return 1; }while(0)
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    CUdevice device;CUcontext context;CUmodule module;CUdeviceptr global,constant,buffer;size_t bytes;
    CHECK(cuInit(0));CHECK(cuDeviceGet(&device,0));CHECK(cuCtxCreate(&context,0,device));CHECK(cuModuleLoad(&module,argv[1]));
    CHECK(cuModuleGetGlobal(&global,&bytes,module,"runtime_symbol_probe"));if(bytes!=4)return 1;
    int value=-1;CHECK(cuMemcpyDtoH(&value,global,4));if(value!=41)return 1;
    CHECK(cuModuleGetGlobal(&constant,&bytes,module,"runtime_constant_probe"));if(bytes!=8)return 1;
    int values[2]={0};CHECK(cuMemcpyDtoH(values,constant,8));if(values[0]!=17||values[1]!=19)return 1;
    value=61;CHECK(cuMemcpyHtoD(constant+4,&value,4));CHECK(cuMemcpyDtoH(values,constant,8));if(values[0]!=17||values[1]!=61)return 1;
    CHECK(cuMemAlloc(&buffer,4));CHECK(cuMemcpyDtoD(global,constant+4,4));
    CUstream stream;CHECK(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));
    void *host;CHECK(cuMemAllocHost(&host,8));((int*)host)[0]=73;
    CHECK(cuMemcpyHtoDAsync(global,host,4,stream));CHECK(cuMemcpyDtoDAsync(buffer,global,4,stream));
    CHECK(cuMemcpyDtoHAsync((int*)host+1,buffer,4,stream));CHECK(cuStreamSynchronize(stream));if(((int*)host)[1]!=73)return 1;
    CHECK(cuMemFreeHost(host));CHECK(cuStreamDestroy(stream));CHECK(cuMemFree(buffer));
    CHECK(cuModuleUnload(module));CHECK(cuCtxDestroy(context));
    printf("SYMBOL_DRIVER_FAMILY=PASS initialized_global_constant offset_write H2D_D2H_D2D stream_order\n");return 0;
}
