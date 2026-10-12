#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define CHECK(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);if(r)return 1;}while(0)
#define EXPECT(call,want) do{cudaError_t r=(call);printf("%s=%d expected=%d\n",#call,r,(want));if(r!=(want))return 1;}while(0)
int main(void){
 int device[1]={0},chosen=-1;struct cudaDeviceProp prop;memset(&prop,0,sizeof(prop));
 CHECK(cudaSetValidDevices(device,1));CHECK(cudaChooseDevice(&chosen,&prop));if(chosen!=0)return 1;
 prop.major=999;prop.totalGlobalMem=SIZE_MAX;CHECK(cudaChooseDevice(&chosen,&prop));if(chosen!=0)return 1;
 CHECK(cudaSetValidDevices(NULL,0));int bad[1]={1};
 EXPECT(cudaSetValidDevices(bad,1),cudaErrorInvalidDevice);
 EXPECT(cudaSetValidDevices(device,2),cudaErrorInvalidValue);
 EXPECT(cudaChooseDevice(NULL,&prop),cudaErrorInvalidValue);
 size_t stack=0,modern=0;CHECK(cudaThreadGetLimit(&stack,cudaLimitPrintfFifoSize));
 CHECK(cudaThreadSetLimit(cudaLimitPrintfFifoSize,stack));CHECK(cudaDeviceGetLimit(&modern,cudaLimitPrintfFifoSize));if(stack!=modern)return 1;
 EXPECT(cudaThreadGetLimit(&stack,cudaLimitStackSize),cudaErrorNotSupported);
 enum cudaFuncCache cache,readback;CHECK(cudaThreadGetCacheConfig(&cache));
 CHECK(cudaThreadSetCacheConfig(cudaFuncCachePreferShared));CHECK(cudaDeviceGetCacheConfig(&readback));if(readback!=cudaFuncCachePreferShared)return 1;
 CHECK(cudaThreadGetCacheConfig(&readback));if(readback!=cudaFuncCachePreferShared)return 1;
 CHECK(cudaThreadSetCacheConfig(cache));
 unsigned char *ptr=NULL,host[16]={0};cudaStream_t stream;
 CHECK(cudaMalloc((void **)&ptr,16));CHECK(cudaStreamCreate(&stream));
 CHECK(cudaMemsetAsync(ptr,0xa7,16,stream));CHECK(cudaMemcpyAsync(host,ptr,16,cudaMemcpyDeviceToHost,stream));
 for(int i=0;i<16;++i)if(host[i])return 1;
 CHECK(cudaThreadSynchronize());for(int i=0;i<16;++i)if(host[i]!=0xa7)return 1;
 CHECK(cudaStreamDestroy(stream));CHECK(cudaFree(ptr));puts("LEGACY_CONTROL_REMOTE=PASS");return 0;
}
