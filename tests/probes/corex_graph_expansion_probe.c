#define _GNU_SOURCE
#include <cuda.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <dlfcn.h>

#define OBS(call) observe(#call, (call))
static CUresult observe(const char *name, CUresult r) {
    printf("%s=%d\n", name, (int)r); fflush(stdout); return r;
}
static atomic_int callbacks;
static void CUDA_CB callback(void *p) { atomic_fetch_add(&callbacks, *(int *)p); }
int main(void) {
    CUdevice dev; CUcontext ctx; CUstream stream; CUdeviceptr ptr;
    CUevent a, b; CUgraph graph, child; CUgraphNode fill, cn, rec, wait;
    CUgraphExec exec = NULL; unsigned char data[32];
    if (cuInit(0) || cuDeviceGet(&dev, 0) || cuCtxCreate(&ctx, 0, dev) ||
        cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING) || cuMemAlloc(&ptr, 32) ||
        cuEventCreate(&a, CU_EVENT_DISABLE_TIMING) || cuEventCreate(&b, CU_EVENT_DISABLE_TIMING) ||
        cuGraphCreate(&child, 0) || cuGraphCreate(&graph, 0)) return 1;
    CUDA_MEMSET_NODE_PARAMS f = {.dst=ptr, .value=0x35, .elementSize=1, .width=32, .height=1};
    OBS(cuGraphAddMemsetNode(&fill, child, NULL, 0, &f, ctx));
    CUresult cr = OBS(cuGraphAddChildGraphNode(&cn, graph, NULL, 0, child));
    if (!cr) {
        CUgraph got = NULL; OBS(cuGraphChildGraphNodeGetGraph(cn, &got));
        size_t n=0; if (got) OBS(cuGraphGetNodes(got, NULL, &n));
        printf("CHILD_COPY=%d NODES=%zu\n",got != child, n);
        OBS(cuGraphInstantiate(&exec, graph, NULL, NULL, 0));
        if (exec) {
            CUresult (*upload)(CUgraphExec,CUstream)=dlsym(RTLD_DEFAULT,"cuGraphUpload");
            if(upload) OBS(upload(exec, stream)); else puts("cuGraphUpload=SYMBOL_ABSENT");
            f.value=0x57; OBS(cuGraphMemsetNodeSetParams(fill, &f));
            OBS(cuGraphLaunch(exec,stream)); OBS(cuStreamSynchronize(stream));
            OBS(cuMemcpyDtoH(data,ptr,32)); printf("CHILD_EXEC_SNAPSHOT=%d\n",data[0]==0x35);
            OBS(cuGraphExecChildGraphNodeSetParams(exec,cn,child));
            OBS(cuGraphDestroy(child)); child=NULL;
            OBS(cuGraphLaunch(exec,stream)); OBS(cuStreamSynchronize(stream));
            OBS(cuMemcpyDtoH(data,ptr,32)); printf("CHILD_EXEC_UPDATE_AFTER_SOURCE_DESTROY=%d\n",data[0]==0x57);
            OBS(cuGraphExecDestroy(exec));exec=NULL;
        }
    }
    OBS(cuGraphAddEventRecordNode(&rec,graph,NULL,0,a));
    OBS(cuGraphAddEventWaitNode(&wait,graph,&rec,1,a));
    CUevent got=NULL;
    OBS(cuGraphEventRecordNodeGetEvent(rec,&got)); printf("RECORD_READBACK=%d\n",got==a);
    OBS(cuGraphEventWaitNodeGetEvent(wait,&got)); printf("WAIT_READBACK=%d\n",got==a);
    OBS(cuGraphEventRecordNodeSetEvent(rec,b)); OBS(cuGraphEventWaitNodeSetEvent(wait,b));
    OBS(cuGraphInstantiate(&exec,graph,NULL,NULL,0));
    if(exec) {
        OBS(cuGraphExecEventRecordNodeSetEvent(exec,rec,a));
        OBS(cuGraphExecEventWaitNodeSetEvent(exec,wait,a));
        OBS(cuGraphLaunch(exec,stream));OBS(cuStreamSynchronize(stream));
        OBS(cuEventQuery(a)); OBS(cuGraphExecDestroy(exec));exec=NULL;
    }
    OBS(cuGraphDestroy(graph));if(child)OBS(cuGraphDestroy(child));
    OBS(cuGraphCreate(&graph,0));
    CUDA_MEM_ALLOC_NODE_PARAMS mp={0};
    mp.poolProps.allocType=CU_MEM_ALLOCATION_TYPE_PINNED;
    mp.poolProps.location.type=CU_MEM_LOCATION_TYPE_DEVICE; mp.poolProps.location.id=0;
    mp.bytesize=4096;CUgraphNode alloc=NULL, free_node=NULL;
    CUresult mr=OBS(cuGraphAddMemAllocNode(&alloc,graph,NULL,0,&mp));
    printf("GRAPH_MEM_PTR=%llu\n",(unsigned long long)mp.dptr);
    if(!mr) {
        CUDA_MEM_ALLOC_NODE_PARAMS mg={0}; OBS(cuGraphMemAllocNodeGetParams(alloc,&mg));
        printf("GRAPH_MEM_READBACK=%d\n",mg.dptr==mp.dptr && mg.bytesize==mp.bytesize);
        OBS(cuGraphAddMemFreeNode(&free_node,graph,&alloc,1,mp.dptr));
        if(free_node){CUdeviceptr fp=0;OBS(cuGraphMemFreeNodeGetParams(free_node,&fp));printf("GRAPH_FREE_READBACK=%d\n",fp==mp.dptr);}
        OBS(cuGraphInstantiate(&exec,graph,NULL,NULL,0));
        if(exec){OBS(cuGraphLaunch(exec,stream));OBS(cuStreamSynchronize(stream));OBS(cuGraphLaunch(exec,stream));OBS(cuStreamSynchronize(stream));OBS(cuGraphExecDestroy(exec));exec=NULL;}
    }
    for(int attr=0;attr<4;++attr){cuuint64_t value=0;OBS(cuDeviceGetGraphMemAttribute(dev,(CUgraphMem_attribute)attr,&value));printf("GRAPH_MEM_ATTR_%d=%llu\n",attr,(unsigned long long)value);if(attr==1||attr==3){value=0;OBS(cuDeviceSetGraphMemAttribute(dev,(CUgraphMem_attribute)attr,&value));}}
    OBS(cuDeviceGraphMemTrim(dev));OBS(cuGraphDestroy(graph));
    OBS(cuGraphCreate(&graph,0));int one=1;CUDA_HOST_NODE_PARAMS hp={.fn=callback,.userData=&one};CUgraphNode host=NULL;
    CUresult hr=OBS(cuGraphAddHostNode(&host,graph,NULL,0,&hp));
    if(!hr){CUDA_HOST_NODE_PARAMS hg={0};OBS(cuGraphHostNodeGetParams(host,&hg));OBS(cuGraphHostNodeSetParams(host,&hp));OBS(cuGraphInstantiate(&exec,graph,NULL,NULL,0));if(exec){OBS(cuGraphExecHostNodeSetParams(exec,host,&hp));OBS(cuGraphLaunch(exec,stream));OBS(cuStreamSynchronize(stream));printf("HOST_CALLBACKS=%d\n",atomic_load(&callbacks));OBS(cuGraphExecDestroy(exec));exec=NULL;}}
    /* CoreX declares the API but omits the Driver flag enum; CUDA flag is 1. */
    CUUserObject u=NULL;CUresult ur=OBS(cuUserObjectCreate(&u,&one,callback,1,1));
    if(!ur){OBS(cuGraphRetainUserObject(graph,u,1,0));OBS(cuUserObjectRelease(u,1));}
    OBS(cuGraphDestroy(graph));
    OBS(cuGraphCreate(&graph,0));CUgraphNode root=NULL;OBS(cuGraphAddEmptyNode(&root,graph,NULL,0));
    CUresult br=OBS(cuStreamBeginCaptureToGraph(stream,graph,&root,NULL,1,CU_STREAM_CAPTURE_MODE_THREAD_LOCAL));
    if(!br){OBS(cuMemsetD8Async(ptr,0x71,32,stream));CUstreamCaptureStatus status;cuuint64_t id;CUgraph cap;const CUgraphNode *deps=NULL;size_t n=0;OBS(cuStreamGetCaptureInfo_v2(stream,&status,&id,&cap,&deps,&n));printf("CAPTURE_TO_GRAPH_IDENTITY=%d DEPS=%zu\n",cap==graph,n);OBS(cuStreamUpdateCaptureDependencies(stream,&root,1,CU_STREAM_SET_CAPTURE_DEPENDENCIES));OBS(cuMemsetD8Async(ptr,0x72,32,stream));OBS(cuStreamEndCapture(stream,&cap));printf("END_TO_GRAPH_IDENTITY=%d\n",cap==graph);}
    else {OBS(cuStreamBeginCapture(stream,CU_STREAM_CAPTURE_MODE_THREAD_LOCAL));OBS(cuMemsetD8Async(ptr,0x71,32,stream));CUstreamCaptureStatus status;cuuint64_t id;CUgraph cap;const CUgraphNode *deps=NULL;size_t n=0;OBS(cuStreamGetCaptureInfo_v2(stream,&status,&id,&cap,&deps,&n));if(n)OBS(cuStreamUpdateCaptureDependencies(stream,(CUgraphNode*)deps,n,CU_STREAM_SET_CAPTURE_DEPENDENCIES));OBS(cuStreamEndCapture(stream,&cap));if(cap)OBS(cuGraphDestroy(cap));}
    CUstreamCaptureMode mode=CU_STREAM_CAPTURE_MODE_RELAXED;OBS(cuThreadExchangeStreamCaptureMode(&mode));printf("PREVIOUS_CAPTURE_MODE=%d\n",mode);OBS(cuThreadExchangeStreamCaptureMode(&mode));
    OBS(cuGraphDebugDotPrint(graph,"/tmp/corex-graph-expansion.dot",0));
    OBS(cuGraphDestroy(graph));OBS(cuMemFree(ptr));OBS(cuEventDestroy(a));OBS(cuEventDestroy(b));OBS(cuStreamDestroy(stream));OBS(cuCtxDestroy(ctx));
    return 0;
}
