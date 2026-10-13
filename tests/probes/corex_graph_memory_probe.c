#include <cuda.h>
#include <stdio.h>
#include <string.h>
#define CHECK(call) do {CUresult r=(call);printf("%s=%d\n",#call,(int)r);fflush(stdout);if(r)return 1;}while(0)
static int accounting(CUdevice dev,const char *phase) {
    for(int attr=0;attr<4;++attr) {
        cuuint64_t value=0;CHECK(cuDeviceGetGraphMemAttribute(dev,(CUgraphMem_attribute)attr,&value));
        printf("%s_GRAPH_ATTR_%d=%llu\n",phase,attr,(unsigned long long)value);
    }
    CUmemoryPool pool;CHECK(cuDeviceGetDefaultMemPool(&pool,dev));
    for(int attr=5;attr<=8;++attr) {
        cuuint64_t value=0;CHECK(cuMemPoolGetAttribute(pool,(CUmemPool_attribute)attr,&value));
        printf("%s_POOL_ATTR_%d=%llu\n",phase,attr,(unsigned long long)value);
    }
    return 0;
}
int main(int argc,char **argv) {
    CUdevice dev;CUcontext ctx;CUstream stream;CUgraph graph;CUgraphNode alloc,fill,copy,fre;
    CUgraphExec exec;CUdeviceptr dst;unsigned char host[32];
    if(cuInit(0)||cuDeviceGet(&dev,0)||cuCtxCreate(&ctx,0,dev)||cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING))return 1;
    CUmodule module=NULL;CUfunction fn=NULL;
    if(argc==2){CHECK(cuModuleLoad(&module,argv[1]));CHECK(cuModuleGetFunction(&fn,module,"m1s2_lifecycle_kernel"));}
    if(accounting(dev,"INITIAL"))return 1;
    CHECK(cuGraphCreate(&graph,0));
    CUDA_MEM_ALLOC_NODE_PARAMS p={0},got={0};
    p.poolProps.allocType=CU_MEM_ALLOCATION_TYPE_PINNED;p.poolProps.location.type=CU_MEM_LOCATION_TYPE_DEVICE;
    p.poolProps.location.id=dev;p.bytesize=32;
    CHECK(cuGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));
    CHECK(cuGraphMemAllocNodeGetParams(alloc,&got));
    printf("MEM_READBACK_BYTES=%zu REQUESTED=%zu PTR_EQUAL=%d ALLOCTYPE=%d LOCATION=%d DEVICE=%d\n",got.bytesize,p.bytesize,got.dptr==p.dptr,got.poolProps.allocType,got.poolProps.location.type,got.poolProps.location.id);
    if(accounting(dev,"ADDED"))return 1;
    CHECK(cuMemAlloc(&dst,32));
    CUDA_MEMSET_NODE_PARAMS f={.dst=p.dptr,.value=0x45,.elementSize=1,.width=32,.height=1};
    if(fn){f.value=41;f.elementSize=4;f.width=8;}
    CHECK(cuGraphAddMemsetNode(&fill,graph,&alloc,1,&f,ctx));
    CUgraphNode after_fill=fill,kernel;void *args[]={&p.dptr};
    CUDA_KERNEL_NODE_PARAMS kp={.func=fn,.gridDimX=1,.gridDimY=1,.gridDimZ=1,
        .blockDimX=1,.blockDimY=1,.blockDimZ=1,.kernelParams=args};
    if(fn){CHECK(cuGraphAddKernelNode(&kernel,graph,&fill,1,&kp));after_fill=kernel;}
    CUDA_MEMCPY3D cp={0};cp.srcMemoryType=cp.dstMemoryType=CU_MEMORYTYPE_DEVICE;
    cp.srcDevice=p.dptr;cp.dstDevice=dst;cp.WidthInBytes=32;cp.Height=cp.Depth=1;
    CHECK(cuGraphAddMemcpyNode(&copy,graph,&after_fill,1,&cp,ctx));
    CHECK(cuGraphAddMemFreeNode(&fre,graph,&copy,1,p.dptr));
    CHECK(cuGraphInstantiate(&exec,graph,NULL,NULL,0));
    CHECK(cuGraphDestroy(graph));graph=NULL;
    for(int launch=0;launch<2;++launch) {
        CHECK(cuGraphLaunch(exec,stream));CHECK(cuStreamSynchronize(stream));CHECK(cuMemcpyDtoH(host,dst,32));
        for(int i=0;i<32;++i)if(host[i]!=(fn ? (i%4 ? 0 : (i==0 ? 42 : 41)) : 0x45))return 2;
        printf("MEM_ALLOC_FILL_COPY_FREE_REPLAY_%d=PASS\n",launch);
    }
    if(accounting(dev,"REPLAYED"))return 1;
    cuuint64_t zero=0;CHECK(cuDeviceSetGraphMemAttribute(dev,CU_GRAPH_MEM_ATTR_USED_MEM_HIGH,&zero));
    CHECK(cuDeviceSetGraphMemAttribute(dev,CU_GRAPH_MEM_ATTR_RESERVED_MEM_HIGH,&zero));
    if(accounting(dev,"RESET"))return 1;
    CHECK(cuGraphExecDestroy(exec));if(graph)CHECK(cuGraphDestroy(graph));CHECK(cuDeviceGraphMemTrim(dev));
    if(accounting(dev,"DESTROYED_TRIMMED"))return 1;
    /* Unpaired graph allocation integrates with ordinary stream-ordered free. */
    CHECK(cuGraphCreate(&graph,0));p.dptr=0;CHECK(cuGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));
    f.dst=p.dptr;f.value=0x67;f.elementSize=1;f.width=32;CHECK(cuGraphAddMemsetNode(&fill,graph,&alloc,1,&f,ctx));
    CHECK(cuGraphInstantiate(&exec,graph,NULL,NULL,0));CHECK(cuGraphLaunch(exec,stream));
    CHECK(cuStreamSynchronize(stream));CHECK(cuMemcpyDtoH(host,p.dptr,32));
    for(int i=0;i<32;++i)if(host[i]!=0x67)return 3;
    CHECK(cuMemFreeAsync(p.dptr,stream));CHECK(cuStreamSynchronize(stream));
    CHECK(cuGraphDestroy(graph));graph=NULL;
    CHECK(cuGraphLaunch(exec,stream));CHECK(cuStreamSynchronize(stream));CHECK(cuMemcpyDtoH(host,p.dptr,32));
    for(int i=0;i<32;++i)if(host[i]!=0x67)return 4;
    /* Holding the native exec preserves the mapping until ordinary free. */
    CHECK(cuMemcpyDtoH(host,p.dptr,32));for(int i=0;i<32;++i)if(host[i]!=0x67)return 5;
    CHECK(cuMemFree(p.dptr));CHECK(cuGraphExecDestroy(exec));if(graph)CHECK(cuGraphDestroy(graph));
    CHECK(cuMemFree(dst));CHECK(cuDeviceGraphMemTrim(dev));
    if(accounting(dev,"FINAL"))return 1;
    CHECK(cuDeviceSetGraphMemAttribute(dev,CU_GRAPH_MEM_ATTR_USED_MEM_HIGH,&zero));
    CHECK(cuDeviceSetGraphMemAttribute(dev,CU_GRAPH_MEM_ATTR_RESERVED_MEM_HIGH,&zero));
    if(accounting(dev,"FINAL_HIGH_RESET"))return 1;
    /* Measure the CoreX lifetime defect separately from the positive path. */
    CHECK(cuGraphCreate(&graph,0));p.dptr=0;CHECK(cuGraphAddMemAllocNode(&alloc,graph,NULL,0,&p));
    CHECK(cuGraphInstantiate(&exec,graph,NULL,NULL,0));CHECK(cuGraphLaunch(exec,stream));CHECK(cuStreamSynchronize(stream));
    CHECK(cuGraphDestroy(graph));CHECK(cuGraphExecDestroy(exec));
    CUresult destroyed=cuMemcpyDtoH(host,p.dptr,32);
    printf("EARLY_EXEC_DESTROY_POINTER_RC=%d\n",destroyed);if(destroyed!=CUDA_ERROR_INVALID_VALUE)return 6;
    if(module)CHECK(cuModuleUnload(module));
    CHECK(cuStreamDestroy(stream));CHECK(cuCtxDestroy(ctx));puts("NATIVE_GRAPH_MEMORY_DATA_AND_ORDERING=PASS");return 0;
}
