#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
extern "C" {
__device__ int runtime_symbol_probe = 41;
__constant__ int runtime_constant_probe[2] = {17,19};
__global__ void runtime_symbol_probe_kernel(int *out)
{
    if (threadIdx.x == 0) *out = runtime_symbol_probe + runtime_constant_probe[0] + runtime_constant_probe[1];
}
void **symbol_test_registration(void);
void __cudaUnregisterFatBinary(void **);
}
#define CHECK(call) do { cudaError_t e=(call); if(e!=cudaSuccess){fprintf(stderr,"line %d %s = %d (%s)\n",__LINE__,#call,(int)e,cudaGetErrorString(e));return 1;} }while(0)
#define REQUIRE(test) do { if(!(test)){fprintf(stderr,"line %d failed: %s\n",__LINE__,#test);return 1;} }while(0)
#define ERROR(call,want) do { cudaError_t e=(call); REQUIRE(e==(want)); REQUIRE(cudaGetLastError()==(want)); }while(0)
int main(int argc, char **)
{
    void *global_address=NULL,*constant_address=NULL,*same=NULL;
    size_t bytes=0;
    CHECK(cudaGetSymbolAddress(&global_address,runtime_symbol_probe));
    CHECK(cudaGetSymbolAddress(&same,runtime_symbol_probe)); REQUIRE(same==global_address);
    CHECK(cudaGetSymbolAddress(&constant_address,runtime_constant_probe)); REQUIRE(constant_address!=global_address);
    CHECK(cudaGetSymbolSize(&bytes,runtime_symbol_probe)); REQUIRE(bytes==sizeof(int));
    CHECK(cudaGetSymbolSize(&bytes,runtime_constant_probe)); REQUIRE(bytes==2*sizeof(int));
    int value=0,constants[2]={0};
    CHECK(cudaMemcpyFromSymbol(&value,runtime_symbol_probe,sizeof(value))); REQUIRE(value==41);
    CHECK(cudaMemcpyFromSymbol(constants,runtime_constant_probe,sizeof(constants))); REQUIRE(constants[0]==17 && constants[1]==19);
    struct cudaPointerAttributes attributes;
    CHECK(cudaPointerGetAttributes(&attributes,global_address)); REQUIRE(attributes.type==cudaMemoryTypeDevice && attributes.devicePointer==global_address);
    value=5; CHECK(cudaMemcpyToSymbol(runtime_symbol_probe,&value,sizeof(value),0,cudaMemcpyDefault));
    value=23; CHECK(cudaMemcpyToSymbol(runtime_constant_probe,&value,sizeof(value),sizeof(int)));
    int *device=NULL; CHECK(cudaMalloc(&device,sizeof(int)));
    runtime_symbol_probe_kernel<<<1,1>>>(device); CHECK(cudaGetLastError());
    CHECK(cudaMemcpy(&value,device,sizeof(value),cudaMemcpyDeviceToHost)); REQUIRE(value==45);
    CHECK(cudaMemcpyToSymbol(runtime_symbol_probe,device,sizeof(int),0,cudaMemcpyDeviceToDevice));
    value=0; CHECK(cudaMemcpyFromSymbol(&value,runtime_symbol_probe,sizeof(value),0,cudaMemcpyDefault)); REQUIRE(value==45);
    CHECK(cudaMemcpyFromSymbol(device,runtime_constant_probe,sizeof(int),sizeof(int),cudaMemcpyDeviceToDevice));
    CHECK(cudaMemcpy(&value,device,sizeof(value),cudaMemcpyDeviceToHost)); REQUIRE(value==23);
    cudaStream_t a,b; cudaEvent_t event;
    CHECK(cudaStreamCreateWithFlags(&a,cudaStreamNonBlocking)); CHECK(cudaStreamCreateWithFlags(&b,cudaStreamNonBlocking));
    CHECK(cudaEventCreateWithFlags(&event,cudaEventDisableTiming));
    value=71; CHECK(cudaMemcpyToSymbolAsync(runtime_symbol_probe,&value,sizeof(value),0,cudaMemcpyHostToDevice,a)); value=999;
    CHECK(cudaEventRecord(event,a)); CHECK(cudaStreamWaitEvent(b,event,0));
    int readback=-1; CHECK(cudaMemcpyFromSymbolAsync(&readback,runtime_symbol_probe,sizeof(readback),0,cudaMemcpyDeviceToHost,b)); REQUIRE(readback==-1);
    CHECK(cudaEventRecord(event,b)); CHECK(cudaEventSynchronize(event)); REQUIRE(readback==71);
    CHECK(cudaMemcpyToSymbolAsync(runtime_constant_probe,device,sizeof(int),0,cudaMemcpyDeviceToDevice,a));
    CHECK(cudaMemcpyFromSymbolAsync(device,runtime_symbol_probe,sizeof(int),0,cudaMemcpyDefault,a));
    readback=-1; CHECK(cudaMemcpyAsync(&readback,device,sizeof(int),cudaMemcpyDeviceToHost,a));
    CHECK(cudaStreamSynchronize(a)); REQUIRE(readback==71);
    value=81; CHECK(cudaMemcpyToSymbolAsync(runtime_symbol_probe,&value,sizeof(value),0,cudaMemcpyDefault,0));
    readback=-1; CHECK(cudaMemcpyFromSymbolAsync(&readback,runtime_symbol_probe,sizeof(readback),0,cudaMemcpyDefault,0));
    CHECK(cudaStreamSynchronize(0)); REQUIRE(readback==81);
    ERROR(cudaFree(global_address),cudaErrorInvalidDevicePointer);
    ERROR(cudaMemcpyToSymbol(runtime_constant_probe,&value,sizeof(value),SIZE_MAX),cudaErrorInvalidValue);
    ERROR(cudaMemcpyFromSymbol(&value,runtime_symbol_probe,sizeof(value)+1),cudaErrorInvalidValue);
    ERROR(cudaMemcpyToSymbol(runtime_symbol_probe,&value,sizeof(value),0,cudaMemcpyDeviceToHost),cudaErrorInvalidMemcpyDirection);
    ERROR(cudaMemcpyFromSymbol(&value,runtime_symbol_probe,sizeof(value),0,cudaMemcpyHostToDevice),cudaErrorInvalidMemcpyDirection);
    int unknown=0;
    cudaError_t (*get_address)(void **,const void *)=cudaGetSymbolAddress;
    cudaError_t (*get_size)(size_t *,const void *)=cudaGetSymbolSize;
    ERROR(get_address(&same,&unknown),cudaErrorInvalidSymbol);
    ERROR(get_address(NULL,(const void*)&runtime_symbol_probe),cudaErrorInvalidValue);
    ERROR(get_size(NULL,(const void*)&runtime_symbol_probe),cudaErrorInvalidValue);
    ERROR(cudaMemcpyFromSymbolAsync(&value,runtime_symbol_probe,sizeof(int),0,cudaMemcpyDeviceToHost,(cudaStream_t)(uintptr_t)123),cudaErrorInvalidResourceHandle);
    printf("SYMBOL_COPIES=PASS global_constant offsets default H2D_D2H_D2D async event_frontier kernel_read\n");
    if(argc==1) {
        readback=-1; CHECK(cudaMemcpyFromSymbolAsync(&readback,runtime_symbol_probe,sizeof(int),0,cudaMemcpyDeviceToHost,a)); REQUIRE(readback==-1);
        __cudaUnregisterFatBinary(symbol_test_registration()); REQUIRE(readback==81);
        ERROR(cudaGetSymbolAddress(&same,runtime_symbol_probe),cudaErrorInvalidSymbol);
        ERROR(cudaMemcpy(&value,global_address,sizeof(value),cudaMemcpyDeviceToHost),cudaErrorInvalidDevicePointer);
        printf("SYMBOL_MODULE_UNLOAD=PASS pending_D2H_visible stale_symbol_and_address\n");
    } else {
        static int exit_readback=-1;
        CHECK(cudaMemcpyFromSymbolAsync(&exit_readback,runtime_symbol_probe,sizeof(int),0,cudaMemcpyDeviceToHost,a));
        REQUIRE(exit_readback==-1);
        printf("SYMBOL_EXIT_WITH_LIVE_MODULE=PASS pending_D2H_and_module_storage\n");
    }
    CHECK(cudaFree(device)); CHECK(cudaEventDestroy(event)); CHECK(cudaStreamDestroy(a)); CHECK(cudaStreamDestroy(b));
    printf("SYMBOL_EXPANSION=PASS\n");
    return 0;
}
