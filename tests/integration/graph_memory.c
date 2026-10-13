#ifdef COREX_GRAPH_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(struct cudaMemAllocNodeParams)==120,"graph allocation ABI");
_Static_assert(offsetof(struct cudaMemAllocNodeParams,dptr)==112,"graph pointer ABI");
#ifndef COREX_GRAPH_INTERNAL_ABI
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;
typedef struct {int kind;uint32_t size;} ArgDesc;
extern cudaError_t corexRemoteModuleLoad(const char *,corexRemoteModule_t *);
extern cudaError_t corexRemoteModuleUnload(corexRemoteModule_t);
extern cudaError_t corexRemoteRegisterKernel(corexRemoteModule_t,const char *,const ArgDesc *,size_t,const void **);
#define CHECK(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
#define EXPECT(call,want) do{cudaError_t r=(call);printf("%s=%d want=%d\n",#call,r,want);fflush(stdout);if(r!=(want))return 1;}while(0)
#define VERIFY(expr) do{if(!(expr)){printf("FAILED %s\n",#expr);return 1;}}while(0)
static struct cudaMemAllocNodeParams params(void) {
    struct cudaMemAllocNodeParams p={0};p.poolProps.allocType=cudaMemAllocationTypePinned;
    p.poolProps.location.type=cudaMemLocationTypeDevice;p.bytesize=32;return p;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    cudaStream_t stream,other;CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    CHECK(cudaStreamCreateWithFlags(&other,cudaStreamNonBlocking));
    corexRemoteModule_t module;const void *func;ArgDesc desc={1,sizeof(void *)};
    CHECK(corexRemoteModuleLoad(argv[1],&module));CHECK(corexRemoteRegisterKernel(module,"m1s2_lifecycle_kernel",&desc,1,&func));
    void *output=NULL;CHECK(cudaMallocAsync(&output,32,stream));CHECK(cudaStreamSynchronize(stream));
    cudaGraph_t graph,clone;cudaGraphNode_t alloc,fill,kernel,copy,fre,extra;cudaGraphExec_t exec,duplicate;
    CHECK(cudaGraphCreate(&graph,0));struct cudaMemAllocNodeParams p=params(),got={0};
    CHECK(cudaGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));VERIFY(p.dptr!=NULL);
    CHECK(cudaGraphMemAllocNodeGetParams(alloc,&got));VERIFY(got.bytesize==32 && got.dptr==p.dptr && got.accessDescCount==0);
    EXPECT(cudaMemset(p.dptr,0,4),cudaErrorNotSupported);
    EXPECT(cudaFreeAsync(p.dptr,stream),cudaErrorNotSupported);
    struct cudaMemsetParams f={.dst=p.dptr,.value=41,.elementSize=4,.width=8,.height=1};
    CHECK(cudaGraphAddMemsetNode(&fill,graph,&alloc,1,&f));
    void *args[]={&p.dptr};struct cudaKernelNodeParams k={0};k.func=(void *)func;
    k.gridDim=k.blockDim=(dim3){1,1,1};k.kernelParams=args;
    CHECK(cudaGraphAddKernelNode(&kernel,graph,&fill,1,&k));
    struct cudaMemcpy3DParms cp={0};cp.srcPtr=(struct cudaPitchedPtr){p.dptr,32,32,1};
    cp.dstPtr=(struct cudaPitchedPtr){output,32,32,1};cp.extent=(struct cudaExtent){32,1,1};cp.kind=cudaMemcpyDeviceToDevice;
    CHECK(cudaGraphAddMemcpyNode(&copy,graph,&kernel,1,&cp));
    CHECK(cudaGraphAddMemFreeNode(&fre,graph,&copy,1,p.dptr));void *free_ptr=NULL;
    CHECK(cudaGraphMemFreeNodeGetParams(fre,&free_ptr));VERIFY(free_ptr==p.dptr);
    EXPECT(cudaGraphAddMemFreeNode(&extra,graph,&copy,1,p.dptr),cudaErrorInvalidValue);
    EXPECT(cudaGraphDestroyNode(fill),cudaErrorInvalidValue);
    EXPECT(cudaGraphRemoveDependencies(graph,&alloc,&fill,1),cudaErrorInvalidValue);
    EXPECT(cudaGraphClone(&clone,graph),cudaErrorNotSupported);
    CHECK(cudaGraphInstantiate(&exec,graph,NULL,NULL,0));
    EXPECT(cudaGraphUpload(exec,stream),cudaErrorNotSupported);
    EXPECT(cudaGraphInstantiate(&duplicate,graph,NULL,NULL,0),cudaErrorNotSupported);
    CHECK(cudaGraphDestroy(graph));
    int values[8]={0};
    for(int launch=0;launch<2;++launch) {
        CHECK(cudaGraphLaunch(exec,launch ? other : stream));
        CHECK(cudaStreamSynchronize(launch ? other : stream));
        CHECK(cudaMemcpy(values,output,32,cudaMemcpyDeviceToHost));
        VERIFY(values[0]==42);for(int i=1;i<8;++i)VERIFY(values[i]==41);
    }
    CHECK(cudaGraphExecDestroy(exec));CHECK(cudaDeviceSynchronize());
    EXPECT(cudaMemcpy(values,p.dptr,4,cudaMemcpyDeviceToHost),cudaErrorInvalidDevicePointer);
    CHECK(cudaDeviceGraphMemTrim(0));puts("GRAPH_ALLOC_KERNEL_COPY_FREE_SNAPSHOT_REPLAY=PASS");

    /* Unpaired allocations use normal Memory Pool free/frontier machinery. */
    CHECK(cudaGraphCreate(&graph,0));p=params();CHECK(cudaGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));
    void *identity=p.dptr;f.dst=p.dptr;f.value=73;CHECK(cudaGraphAddMemsetNode(&fill,graph,&alloc,1,&f));
    CHECK(cudaGraphInstantiate(&exec,graph,NULL,NULL,0));CHECK(cudaGraphLaunch(exec,stream));
    EXPECT(cudaGraphLaunch(exec,stream),cudaErrorInvalidValue);
    CHECK(cudaMemcpyAsync(values,p.dptr,32,cudaMemcpyDeviceToHost,stream));
    CHECK(cudaFreeAsync(p.dptr,stream));
    EXPECT(cudaMemset(p.dptr,0,4),cudaErrorNotSupported);
    CHECK(cudaStreamSynchronize(stream));for(int i=0;i<8;++i)VERIFY(values[i]==73);
    CHECK(cudaGraphLaunch(exec,stream));CHECK(cudaStreamSynchronize(stream));VERIFY(p.dptr==identity);
    CHECK(cudaGraphDestroy(graph));CHECK(cudaGraphExecDestroy(exec));
    /* Logical exec destruction must keep the unpaired native mapping alive. */
    CHECK(cudaMemcpy(values,p.dptr,32,cudaMemcpyDeviceToHost));for(int i=0;i<8;++i)VERIFY(values[i]==73);
    CHECK(cudaFreeAsync(p.dptr,other));CHECK(cudaStreamSynchronize(other));CHECK(cudaDeviceGraphMemTrim(0));
    EXPECT(cudaMemcpy(values,p.dptr,4,cudaMemcpyDeviceToHost),cudaErrorInvalidDevicePointer);
    CHECK(cudaDeviceSynchronize());puts("GRAPH_UNPAIRED_ASYNC_FREE_REPLAY_RETIRED_OWNER=PASS");

    /* Reject an unordered lifetime before submitting any native graph work. */
    CHECK(cudaGraphCreate(&graph,0));p=params();CHECK(cudaGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));
    f.dst=p.dptr;CHECK(cudaGraphAddMemsetNode(&fill,graph,NULL,0,&f));
    EXPECT(cudaGraphInstantiate(&exec,graph,NULL,NULL,0),cudaErrorInvalidValue);
    CHECK(cudaGraphDestroy(graph));
    CHECK(cudaFreeAsync(output,stream));CHECK(cudaStreamSynchronize(stream));CHECK(corexRemoteModuleUnload(module));
    CHECK(cudaStreamDestroy(stream));CHECK(cudaStreamDestroy(other));
    puts("GRAPH_MEMORY_PUBLIC_FAMILY=PASS");return 0;
}
#endif
