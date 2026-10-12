#include <cuda_runtime_api.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define SHOW(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);}while(0)
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);
 int initial_device[1]={0};SHOW(cudaSetValidDevices(initial_device,1));
 struct cudaDeviceProp prop;memset(&prop,0,sizeof(prop));int dev=-1;
 SHOW(cudaChooseDevice(&dev,&prop));printf("CHOOSE_DEVICE_EMPTY=%d\n",dev);
 prop.major=999;prop.totalGlobalMem=SIZE_MAX;dev=-1;
 SHOW(cudaChooseDevice(&dev,&prop));printf("CHOOSE_DEVICE_OVERSIZE=%d\n",dev);
 SHOW(cudaSetValidDevices(initial_device,1));
 int invalid_device[1]={1};SHOW(cudaSetValidDevices(invalid_device,1));
 int devices[2]={0,0};SHOW(cudaSetValidDevices(devices,2));
 enum cudaFuncCache cache=cudaFuncCachePreferNone;size_t limit=0;
 SHOW(cudaThreadGetCacheConfig(&cache));SHOW(cudaThreadSetCacheConfig(cudaFuncCachePreferShared));
 SHOW(cudaThreadGetCacheConfig(&cache));printf("THREAD_CACHE=%d\n",cache);
 SHOW(cudaThreadGetLimit(&limit,cudaLimitStackSize));printf("THREAD_STACK=%zu\n",limit);
 SHOW(cudaThreadSetLimit(cudaLimitStackSize,limit));SHOW(cudaThreadSynchronize());
 double value=1.25;SHOW(cudaSetDoubleForDevice(&value));SHOW(cudaSetDoubleForHost(&value));printf("DOUBLE_UNCHANGED=%d\n",value==1.25);
 return 0;
}
