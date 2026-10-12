#include <cuda.h>
#include <stdio.h>
#include <string.h>
#define CHECK(call) do { CUresult r=(call); printf("%s=%d\n",#call,(int)r); if(r) return 1; } while(0)
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    CUdevice dev;CUcontext ctx;CUstream stream;CUdeviceptr ptr;CUmodule module;CUfunction func;
    CHECK(cuInit(0));CHECK(cuDeviceGet(&dev,0));CHECK(cuCtxCreate(&ctx,0,dev));
    CHECK(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));CHECK(cuMemAlloc(&ptr,384));
    unsigned char expected[384],output[384];
    for(int async=0;async<2;++async){
        memset(expected,0xcc,sizeof(expected));CHECK(cuMemcpyHtoD(ptr,expected,sizeof(expected)));
        for(int z=0;z<3;++z){
            CUresult r=async?cuMemsetD2D8Async(ptr+z*128,32,0xa7,17,2,stream):cuMemsetD2D8(ptr+z*128,32,0xa7,17,2);
            printf("slice-fill async=%d z=%d result=%d\n",async,z,(int)r);if(r)return 1;
            for(int y=0;y<2;++y)memset(expected+z*128+y*32,0xa7,17);
        }
        CHECK(cuStreamSynchronize(stream));CHECK(cuMemcpyDtoH(output,ptr,sizeof(output)));
        if(memcmp(expected,output,sizeof(output)))return 1;
        printf("FILL_DATA_AND_PADDING async=%d PASS\n",async);
    }
    CHECK(cuModuleLoad(&module,argv[1]));CHECK(cuModuleGetFunction(&func,module,"m1s2_lifecycle_kernel"));
    for(int config=0;config<3;++config){CUresult r=cuFuncSetSharedMemConfig(func,(CUsharedconfig)config);printf("FUNCTION_SHARED_CONFIG config=%d result=%d\n",config,r);}
    for(unsigned flags=0;flags<3;++flags){int blocks=-1;CUresult r=cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&blocks,func,128,0,flags);printf("OCCUPANCY_FLAGS flags=%u result=%d blocks=%d\n",flags,r,blocks);}
    int grid=-1,block=-1;CUresult r=cuOccupancyMaxPotentialBlockSize(&grid,&block,func,NULL,0,0);
    printf("OCCUPANCY_POTENTIAL result=%d grid=%d block=%d\n",r,grid,block);
    CHECK(cuModuleUnload(module));CHECK(cuMemFree(ptr));CHECK(cuStreamDestroy(stream));CHECK(cuCtxDestroy(ctx));
    puts("EXECUTION_FILL_PROBE=COMPLETE");return 0;
}
