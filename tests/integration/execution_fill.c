#include <cuda_runtime_api.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
/* Use the installed Runtime ABI with the existing controlled kernel registration. */
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;
typedef struct {int kind;unsigned int size;} ArgDesc;
extern cudaError_t corexRemoteModuleLoad(const char *,corexRemoteModule_t *);
extern cudaError_t corexRemoteModuleUnload(corexRemoteModule_t);
extern cudaError_t corexRemoteRegisterKernel(corexRemoteModule_t,const char *,const ArgDesc *,size_t,const void **);
#define CHECK(call) do { cudaError_t r=(call); printf("%s=%d\n",#call,r);if(r) return 1; } while(0)
#define EXPECT(call,want) do { cudaError_t r=(call); printf("%s=%d expected=%d\n",#call,r,(want));if(r!=(want)) return 1; } while(0)
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    void *ptr;cudaStream_t stream;unsigned char expected[384],out[384];
    CHECK(cudaMalloc(&ptr,384));CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    struct cudaPitchedPtr pitched={ptr,32,17,4};struct cudaExtent extent={17,2,3};
    for(int async=0;async<2;++async){
        memset(expected,0xcc,sizeof(expected));CHECK(cudaMemcpy(ptr,expected,384,cudaMemcpyHostToDevice));
        if(async) CHECK(cudaMemset3DAsync(pitched,-89,extent,stream));
        else CHECK(cudaMemset3D(pitched,0x1a7,extent));
        for(int z=0;z<3;++z)for(int y=0;y<2;++y)memset(expected+z*128+y*32,0xa7,17);
        if(async){
            /* Same-stream D2H stays deferred and retires the native fill. */
            memset(out,0x11,sizeof(out));CHECK(cudaMemcpyAsync(out,ptr,384,cudaMemcpyDeviceToHost,stream));
            for(size_t i=0;i<sizeof(out);++i)if(out[i]!=0x11)return 1;
            CHECK(cudaStreamSynchronize(stream));
        }else CHECK(cudaMemcpy(out,ptr,384,cudaMemcpyDeviceToHost));
        if(memcmp(expected,out,384))return 1;
        printf("FILL_DATA_AND_PADDING async=%d PASS\n",async);
    }
    struct cudaPitchedPtr interior={(unsigned char *)ptr+5,0,0,0};
    CHECK(cudaMemset3DAsync(interior,0x142,(struct cudaExtent){7,1,1},NULL));
    CHECK(cudaDeviceSynchronize());CHECK(cudaMemcpy(out,ptr,384,cudaMemcpyDeviceToHost));
    memset(expected+5,0x42,7);if(memcmp(expected,out,384))return 1;
    EXPECT(cudaMemset3D(pitched,1,(struct cudaExtent){33,2,3}),cudaErrorInvalidValue);
    EXPECT(cudaMemset3D(pitched,1,(struct cudaExtent){17,2,4}),cudaErrorInvalidValue);
    CHECK(cudaMemset3D((struct cudaPitchedPtr){0},0,(struct cudaExtent){0,0,0}));
    corexRemoteModule_t module;const void *func;ArgDesc arg={1,8};
    CHECK(corexRemoteModuleLoad(argv[1],&module));
    CHECK(corexRemoteRegisterKernel(module,"m1s2_lifecycle_kernel",&arg,1,&func));
    for(int c=0;c<3;++c)CHECK(cudaFuncSetSharedMemConfig(func,(enum cudaSharedMemConfig)c));
    int plain=-1,flagged=-1,grid=-1,block=-1;
    CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&plain,func,128,0));
    CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&flagged,func,128,0,0));
    if(plain!=flagged||flagged<=0)return 1;
    EXPECT(cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&flagged,func,128,0,1),cudaErrorNotSupported);
    EXPECT(cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&flagged,func,128,0,2),cudaErrorInvalidValue);
    CHECK(cudaOccupancyMaxPotentialBlockSize(&grid,&block,func,0,0));
    printf("OCCUPANCY_RESULT blocks=%d grid=%d block=%d\n",plain,grid,block);
    if(grid<=0||block<=0)return 1;
    CHECK(corexRemoteModuleUnload(module));
    EXPECT(cudaFuncSetSharedMemConfig(func,cudaSharedMemBankSizeDefault),cudaErrorInvalidResourceHandle);
    EXPECT(cudaOccupancyMaxPotentialBlockSize(&grid,&block,func,0,0),cudaErrorInvalidResourceHandle);
    CHECK(cudaStreamDestroy(stream));
    EXPECT(cudaMemset3DAsync(pitched,1,extent,stream),cudaErrorInvalidResourceHandle);
    CHECK(cudaFree(ptr));puts("EXECUTION_FILL_REMOTE=PASS");return 0;
}
