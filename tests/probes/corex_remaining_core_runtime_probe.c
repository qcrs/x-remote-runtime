#include <cuda_runtime_api.h>
#include <stdio.h>
#define SHOW(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,(int)r);fflush(stdout);}while(0)
static void host(void *p){*(volatile int *)p=1;}
static void callback(cudaStream_t s,cudaError_t r,void *p){(void)s;*(volatile int *)p=r==cudaSuccess;}
int main(void){
 SHOW(cudaSetDeviceFlags(cudaDeviceScheduleSpin));unsigned flags=99;SHOW(cudaGetDeviceFlags(&flags));printf("FLAGS_BEFORE=%u\n",flags);
 cudaStream_t stream;if(cudaStreamCreate(&stream))return 1;
 SHOW(cudaSetDeviceFlags(cudaDeviceScheduleYield));SHOW(cudaGetDeviceFlags(&flags));printf("FLAGS_AFTER=%u\n",flags);
 volatile int h=0,c=0;SHOW(cudaLaunchHostFunc(stream,host,(void*)&h));SHOW(cudaStreamAddCallback(stream,callback,(void*)&c,0));SHOW(cudaStreamSynchronize(stream));printf("HOST_FUNC=%d CALLBACK=%d\n",h,c);
 SHOW(cudaConfigureCall((dim3){1,1,1},(dim3){1,1,1},0,stream));
 void *entry=NULL;SHOW(cudaGetDriverEntryPoint("cuMemAlloc",&entry,0));printf("ENTRY_PRESENT=%d\n",entry!=NULL);
 SHOW(cudaDeviceReset());SHOW(cudaGetDeviceFlags(&flags));printf("FLAGS_AFTER_RESET=%u\n",flags);SHOW(cudaThreadExit());return 0;
}
