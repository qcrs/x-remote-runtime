#include <cuda_runtime_api.h>
#include <cuda.h>
#include <stdio.h>
#include <string.h>
#define CHECK(call) do { int r=(call); printf("%s=%d\n",#call,r); if(r)return 1; } while(0)
#define QUERY(name,variable) CHECK(cudaGetDriverEntryPoint(name,(void **)&variable,cudaEnableDefault))
int main(void)
{
    CHECK(cudaSetDevice(0));
    CUdeviceptr a=0,b=0; unsigned char source[32],host[32];
    CHECK(cudaMalloc((void **)&b,sizeof(source)));
    CUresult (CUDAAPI *alloc)(CUdeviceptr *,size_t);
    CUresult (CUDAAPI *release)(CUdeviceptr);
    CUresult (CUDAAPI *htod)(CUdeviceptr,const void *,size_t);
    CUresult (CUDAAPI *dtoh)(void *,CUdeviceptr,size_t);
    CUresult (CUDAAPI *dtod)(CUdeviceptr,CUdeviceptr,size_t);
    CUresult (CUDAAPI *sync)(void);
    QUERY("cuMemAlloc",alloc); QUERY("cuMemFree",release);
    QUERY("cuMemcpyHtoD",htod); QUERY("cuMemcpyDtoH",dtoh);
    QUERY("cuMemcpyDtoD",dtod); QUERY("cuCtxSynchronize",sync);
    memset(source,0x61,sizeof(source));
    CHECK(alloc(&a,sizeof(source)));
    CHECK(htod(a,source,sizeof(source))); CHECK(dtod(b,a,sizeof(source)));
    CHECK(dtoh(host,b,sizeof(host))); if(memcmp(host,source,sizeof(host)))return 1;
    CHECK(cudaMemset((void *)a,0x39,sizeof(source))); CHECK(dtoh(host,a,sizeof(host)));
    for(unsigned i=0;i<sizeof(host);++i)if(host[i]!=0x39)return 1;
    CHECK(sync()); CHECK(release(a)); CHECK(cudaFree((void *)b));
#ifndef COREX_ENTRY_NATIVE_PROBE
    if(release(a)!=CUDA_ERROR_INVALID_VALUE)return 1;
#endif
    void *entry=NULL;
    CHECK(cudaGetDriverEntryPoint("cuMemAlloc",&entry,cudaEnableLegacyStream));
    printf("DRIVER_ZERO_FREE=%d\n",release(0));
    printf("DRIVER_ZERO_ALLOC=%d\n",alloc(&a,0));
#ifndef COREX_ENTRY_NATIVE_PROBE
    if(release(0)!=CUDA_SUCCESS)return 1;
    if(alloc(NULL,32)!=CUDA_ERROR_INVALID_VALUE)return 1;
    if(cudaGetDriverEntryPoint("cuCtxCreate",&entry,0)!=cudaErrorNotSupported||entry)return 1;
    CHECK(cudaGetDriverEntryPoint("cuMemAlloc",&entry,cudaEnablePerThreadDefaultStream));
    if(!entry)return 1;
    if(cudaGetDriverEntryPoint("cuMemcpyHtoD",&entry,cudaEnablePerThreadDefaultStream)!=cudaErrorNotSupported||entry)return 1;
    if(cudaGetDriverEntryPoint("cuMemAlloc",&entry,3)!=cudaErrorInvalidValue)return 1;
    if(cudaGetDriverEntryPoint(NULL,&entry,0)!=cudaErrorInvalidValue)return 1;
    if(cudaGetDriverEntryPoint("cuMemAlloc",NULL,0)!=cudaErrorInvalidValue)return 1;
    puts("DRIVER_ENTRIES_REMOTE=PASS");
#else
    puts("DRIVER_ENTRIES_NATIVE=PASS");
#endif
    CHECK(cudaDeviceReset()); return 0;
}
