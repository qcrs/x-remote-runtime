#include <cuda_runtime_api.h>
#include <stdio.h>
#define SHOW(call) do { cudaError_t r=(call); printf("%s=%d\n",#call,(int)r); fflush(stdout); } while(0)
int main(void) {
 cudaStream_t stream; cudaMemPool_t pool=NULL,current=NULL,custom=NULL;void *ptr=NULL;
 SHOW(cudaSetDevice(0));if(cudaStreamCreate(&stream))return 1;
 int supported=-1;SHOW(cudaDeviceGetAttribute(&supported,cudaDevAttrMemoryPoolsSupported,0));printf("MEMORY_POOLS_SUPPORTED=%d\n",supported);
 SHOW(cudaDeviceGetDefaultMemPool(&pool,0));SHOW(cudaDeviceGetMemPool(&current,0));
 struct cudaMemPoolProps props={0};props.allocType=cudaMemAllocationTypePinned;props.location.type=cudaMemLocationTypeDevice;
 SHOW(cudaMemPoolCreate(&custom,&props));printf("DEFAULT_POOL_PRESENT=%d CURRENT_POOL_PRESENT=%d CUSTOM_POOL_PRESENT=%d\n",pool!=NULL,current!=NULL,custom!=NULL);
 SHOW(cudaMallocAsync(&ptr,4096,stream));if(ptr)SHOW(cudaFreeAsync(ptr,stream));
 ptr=NULL;SHOW(cudaMallocAsync(&ptr,0,stream));printf("ZERO_ALLOC_NULL=%d\n",ptr==NULL);SHOW(cudaFreeAsync(NULL,stream));
 if(custom){SHOW(cudaMallocFromPoolAsync(&ptr,4096,custom,stream));if(ptr)SHOW(cudaFreeAsync(ptr,stream));SHOW(cudaStreamSynchronize(stream));SHOW(cudaMemPoolDestroy(custom));}
 SHOW(cudaStreamDestroy(stream));return 0;
}
