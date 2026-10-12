#include <cuda_runtime_api.h>
#include <stdio.h>
int main(void){
 int count=-1; cudaError_t r=cudaGetDeviceCount(&count);printf("cudaGetDeviceCount=%d count=%d\n",r,count);if(r)return 1;
 r=cudaCtxResetPersistingL2Cache();printf("cudaCtxResetPersistingL2Cache=%d\n",r);
 return 0;
}
