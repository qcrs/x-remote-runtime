#ifdef COREX_GRAPH_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(cudaGraphEdgeData)==8,"capture edge ABI");
_Static_assert(sizeof(cudaEvent_t)==8,"event token ABI");
#ifndef COREX_GRAPH_INTERNAL_ABI
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;
typedef struct {int kind;uint32_t size;} ArgDesc;
extern cudaError_t corexRemoteModuleLoad(const char *,corexRemoteModule_t *);
extern cudaError_t corexRemoteModuleUnload(corexRemoteModule_t);
extern cudaError_t corexRemoteRegisterKernel(corexRemoteModule_t,const char *,const ArgDesc *,size_t,const void **);
#define CHECK(call) do {cudaError_t r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
#define EXPECT(call,want) do {cudaError_t r=(call);printf("%s=%d want=%d\n",#call,r,want);fflush(stdout);if(r!=(want))return 1;}while(0)
#define VERIFY(expr) do {if(!(expr)){printf("FAILED: %s\n",#expr);return 1;}}while(0)
static void *wrong_thread(void *p) {
    return (void *)(uintptr_t)cudaStreamUpdateCaptureDependencies(p,NULL,0,cudaStreamSetCaptureDependencies);
}
static int child_family(void *ptr,cudaStream_t stream,const char *image) {
    void *output=NULL,*marker=NULL;CHECK(cudaMalloc(&output,4));CHECK(cudaMalloc(&marker,4));
    corexRemoteModule_t module;const void *func;ArgDesc desc={1,sizeof(void *)};
    CHECK(corexRemoteModuleLoad(image,&module));
    CHECK(corexRemoteRegisterKernel(module,"m1s2_lifecycle_kernel",&desc,1,&func));
    cudaGraph_t source,parent,borrowed,clone;cudaGraphNode_t fill,kernel,copy,child,mapped,root,marker_node;
    cudaGraphExec_t original,updated,subexec;
    CHECK(cudaGraphCreate(&source,0));
    struct cudaMemsetParams f={.dst=ptr,.value=10,.elementSize=4,.width=1,.height=1};
    CHECK(cudaGraphAddMemsetNode(&fill,source,NULL,0,&f));
    void *args[]={&ptr};struct cudaKernelNodeParams k={0};
    k.func=(void *)func;k.gridDim=(dim3){1,1,1};k.blockDim=(dim3){1,1,1};k.kernelParams=args;
    CHECK(cudaGraphAddKernelNode(&kernel,source,&fill,1,&k));
    struct cudaMemcpy3DParms cp={0};cp.srcPtr=(struct cudaPitchedPtr){ptr,4,4,1};
    cp.dstPtr=(struct cudaPitchedPtr){output,4,4,1};cp.extent=(struct cudaExtent){4,1,1};cp.kind=cudaMemcpyDeviceToDevice;
    CHECK(cudaGraphAddMemcpyNode(&copy,source,&kernel,1,&cp));
    CHECK(cudaGraphCreate(&parent,0));
    CHECK(cudaGraphAddEmptyNode(&root,parent,NULL,0));
    CHECK(cudaGraphAddChildGraphNode(&child,parent,&root,1,source));
    struct cudaMemsetParams mark={.dst=marker,.value=7,.elementSize=4,.width=1,.height=1};
    CHECK(cudaGraphAddMemsetNode(&marker_node,parent,&child,1,&mark));
    CHECK(cudaGraphChildGraphNodeGetGraph(child,&borrowed));VERIFY(borrowed!=source);
    size_t count=0;CHECK(cudaGraphGetNodes(borrowed,NULL,&count));VERIFY(count==3);
    CHECK(cudaGraphNodeFindInClone(&mapped,fill,borrowed));
    struct cudaMemsetParams got={0};CHECK(cudaGraphMemsetNodeGetParams(mapped,&got));VERIFY(got.value==10 && got.dst==ptr);
    EXPECT(cudaGraphMemsetNodeSetParams(mapped,&f),cudaErrorNotSupported);
    EXPECT(cudaGraphDestroy(borrowed),cudaErrorNotSupported);
    EXPECT(cudaGraphClone(&clone,parent),cudaErrorNotSupported);VERIFY(clone==NULL);
    EXPECT(cudaGraphAddChildGraphNode(&mapped,source,NULL,0,parent),cudaErrorNotSupported);
    CHECK(cudaGraphInstantiate(&original,parent,NULL,NULL,0));
    CHECK(cudaGraphInstantiate(&updated,parent,NULL,NULL,0));
    mark.value=15;CHECK(cudaGraphExecMemsetNodeSetParams(updated,marker_node,&mark));
    mark.value=23;CHECK(cudaGraphMemsetNodeSetParams(marker_node,&mark));
    CHECK(cudaGraphNodeSetEnabled(updated,marker_node,0));CHECK(cudaMemset(marker,0,4));
    EXPECT(cudaGraphInstantiate(&subexec,borrowed,NULL,NULL,0),cudaErrorNotSupported);VERIFY(subexec==NULL);
    f.value=20;CHECK(cudaGraphMemsetNodeSetParams(fill,&f));
    CHECK(cudaGraphExecChildGraphNodeSetParams(updated,child,source));
    int before_destroy=0;CHECK(cudaGraphLaunch(updated,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(&before_destroy,output,4,cudaMemcpyDeviceToHost));
    printf("CHILD_UPDATED_BEFORE_SOURCE_DESTROY=%d\n",before_destroy);VERIFY(before_destroy==21);
    int mark_value=0;CHECK(cudaMemcpy(&mark_value,marker,4,cudaMemcpyDeviceToHost));VERIFY(mark_value==0);
    CHECK(cudaGraphNodeSetEnabled(updated,marker_node,1));
    CHECK(cudaGraphLaunch(updated,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(&mark_value,marker,4,cudaMemcpyDeviceToHost));VERIFY(mark_value==15);
    mark.value=17;CHECK(cudaGraphExecMemsetNodeSetParams(updated,marker_node,&mark));
    f.value=30;CHECK(cudaGraphMemsetNodeSetParams(fill,&f));
    CHECK(cudaGraphExecChildGraphNodeSetParams(updated,child,source));
    int value=0;CHECK(cudaGraphLaunch(original,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(&value,output,4,cudaMemcpyDeviceToHost));VERIFY(value==11);
    CHECK(cudaMemcpy(&mark_value,marker,4,cudaMemcpyDeviceToHost));VERIFY(mark_value==7);
    CHECK(cudaGraphDestroy(source));
    CHECK(cudaGraphLaunch(updated,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(&value,output,4,cudaMemcpyDeviceToHost));printf("CHILD_UPDATED_AFTER_SOURCE_DESTROY=%d\n",value);VERIFY(value==31);
    CHECK(cudaMemcpy(&mark_value,marker,4,cudaMemcpyDeviceToHost));VERIFY(mark_value==17);
    CHECK(cudaGraphDestroyNode(child));
    EXPECT(cudaGraphGetNodes(borrowed,NULL,&count),cudaErrorInvalidResourceHandle);
    CHECK(cudaGraphDestroy(parent));
    CHECK(cudaGraphLaunch(original,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(&value,output,4,cudaMemcpyDeviceToHost));VERIFY(value==11);
    CHECK(corexRemoteModuleUnload(module));
    EXPECT(cudaGraphLaunch(updated,stream),cudaErrorInvalidResourceHandle);
    CHECK(cudaGraphExecDestroy(original));CHECK(cudaGraphExecDestroy(updated));
    CHECK(cudaFree(output));CHECK(cudaFree(marker));puts("CHILD_KERNEL_COPY_OWNERSHIP_EXEC_SNAPSHOTS=PASS");return 0;
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    struct {uint64_t before,value,after;} attribute={0x1234,0,0x5678};
    CHECK(cudaDeviceGetGraphMemAttribute(0,cudaGraphMemAttrUsedMemCurrent,&attribute.value));
    VERIFY(attribute.before==0x1234 && attribute.after==0x5678);
    uint64_t zero=0,one=1;
    CHECK(cudaDeviceSetGraphMemAttribute(0,cudaGraphMemAttrUsedMemHigh,&zero));
    CHECK(cudaDeviceSetGraphMemAttribute(0,cudaGraphMemAttrReservedMemHigh,&zero));
    CHECK(cudaDeviceGetGraphMemAttribute(0,cudaGraphMemAttrUsedMemHigh,&attribute.value));VERIFY(attribute.value==0);
    CHECK(cudaDeviceGetGraphMemAttribute(0,cudaGraphMemAttrReservedMemHigh,&attribute.value));VERIFY(attribute.value==0);
    CHECK(cudaDeviceGraphMemTrim(0));
    EXPECT(cudaDeviceGetGraphMemAttribute(1,cudaGraphMemAttrUsedMemCurrent,&attribute.value),cudaErrorInvalidDevice);
    EXPECT(cudaDeviceGetGraphMemAttribute(0,(enum cudaGraphMemAttributeType)4,&attribute.value),cudaErrorInvalidValue);
    EXPECT(cudaDeviceGetGraphMemAttribute(0,cudaGraphMemAttrUsedMemCurrent,NULL),cudaErrorInvalidValue);
    EXPECT(cudaDeviceSetGraphMemAttribute(0,cudaGraphMemAttrUsedMemCurrent,&zero),cudaErrorInvalidValue);
    EXPECT(cudaDeviceSetGraphMemAttribute(0,cudaGraphMemAttrUsedMemHigh,&one),cudaErrorInvalidValue);
    puts("GRAPH_MEMORY_CONTROL_DTO=PASS");
    void *ptr=NULL;cudaStream_t stream,other;cudaEvent_t a,b,c,got;
    CHECK(cudaMalloc(&ptr,32));
    CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    CHECK(cudaStreamCreateWithFlags(&other,cudaStreamNonBlocking));
    if(child_family(ptr,stream,argv[1]))return 1;
    CHECK(cudaEventCreateWithFlags(&a,cudaEventDisableTiming));
    CHECK(cudaEventCreateWithFlags(&b,cudaEventDisableTiming));
    CHECK(cudaEventCreateWithFlags(&c,cudaEventDisableTiming));
    cudaGraph_t graph;cudaGraphNode_t wait,fill,record;cudaGraphExec_t exec,old;
    CHECK(cudaGraphCreate(&graph,0));
    CHECK(cudaGraphAddEventWaitNode(&wait,graph,NULL,0,a));
    struct cudaMemsetParams p={.dst=ptr,.value=0x31,.elementSize=1,.width=32,.height=1};
    CHECK(cudaGraphAddMemsetNode(&fill,graph,&wait,1,&p));
    CHECK(cudaGraphAddEventRecordNode(&record,graph,&fill,1,a));
    CHECK(cudaGraphEventRecordNodeGetEvent(record,&got));VERIFY(got==a);
    CHECK(cudaGraphEventWaitNodeGetEvent(wait,&got));VERIFY(got==a);
    CHECK(cudaGraphInstantiate(&old,graph,NULL,NULL,0));
    CHECK(cudaGraphEventRecordNodeSetEvent(record,b));
    CHECK(cudaGraphEventWaitNodeSetEvent(wait,b));
    CHECK(cudaGraphEventRecordNodeGetEvent(record,&got));VERIFY(got==b);
    CHECK(cudaGraphEventWaitNodeGetEvent(wait,&got));VERIFY(got==b);
    CHECK(cudaGraphInstantiate(&exec,graph,NULL,NULL,0));
    CHECK(cudaGraphExecEventRecordNodeSetEvent(exec,record,c));
    CHECK(cudaGraphExecEventWaitNodeSetEvent(exec,wait,c));
    /* Exec setters do not mutate graph params. */
    CHECK(cudaGraphEventRecordNodeGetEvent(record,&got));VERIFY(got==b);
    EXPECT(cudaGraphEventWaitNodeGetEvent(record,&got),cudaErrorInvalidValue);
    EXPECT(cudaGraphEventRecordNodeGetEvent(record,NULL),cudaErrorInvalidValue);
    EXPECT(cudaGraphEventRecordNodeSetEvent(fill,c),cudaErrorInvalidValue);
    cudaGraph_t foreign;cudaGraphNode_t foreign_record;
    CHECK(cudaGraphCreate(&foreign,0));CHECK(cudaGraphAddEventRecordNode(&foreign_record,foreign,NULL,0,c));
    EXPECT(cudaGraphExecEventRecordNodeSetEvent(exec,foreign_record,c),cudaErrorInvalidValue);
    CHECK(cudaGraphDestroy(foreign));
    unsigned char source[32],host[32];memset(source,0x19,32);memset(host,0,32);
    CHECK(cudaMemcpyAsync(ptr,source,32,cudaMemcpyHostToDevice,other));
    CHECK(cudaMemcpyAsync(host,ptr,32,cudaMemcpyDeviceToHost,other));
    CHECK(cudaEventRecord(c,other));
    CHECK(cudaGraphLaunch(exec,stream));CHECK(cudaEventSynchronize(c));
    for(int i=0;i<32;++i)VERIFY(host[i]==0x19);
    CHECK(cudaMemcpy(host,ptr,32,cudaMemcpyDeviceToHost));
    for(int i=0;i<32;++i)VERIFY(host[i]==0x31);
    CHECK(cudaEventRecord(a,other));CHECK(cudaGraphLaunch(old,stream));CHECK(cudaStreamSynchronize(stream));
    /* Destroying graph owners leaves both executable snapshots usable. */
    CHECK(cudaGraphDestroy(graph));CHECK(cudaEventDestroy(b));
    CHECK(cudaGraphLaunch(exec,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaGraphExecDestroy(old));CHECK(cudaEventDestroy(a));
    CHECK(cudaEventDestroy(c));EXPECT(cudaGraphLaunch(exec,stream),cudaErrorInvalidResourceHandle);
    CHECK(cudaGraphExecDestroy(exec));
    puts("EVENT_MUTATION_SNAPSHOTS_FRONTIERS=PASS");

    CHECK(cudaGraphCreate(&graph,0));cudaGraphNode_t left,right;
    CHECK(cudaGraphAddEmptyNode(&left,graph,NULL,0));CHECK(cudaGraphAddEmptyNode(&right,graph,NULL,0));
    cudaGraphNode_t roots[2]={left,right};cudaGraphEdgeData edges[2]={{0},{0}};
    edges[0].type=1;
    EXPECT(cudaStreamBeginCaptureToGraph(stream,graph,roots,edges,2,cudaStreamCaptureModeThreadLocal),cudaErrorNotSupported);
    edges[0].type=0;
    CHECK(cudaStreamBeginCaptureToGraph(stream,graph,roots,edges,2,cudaStreamCaptureModeThreadLocal));
    const cudaGraphNode_t *deps=NULL;size_t count=0;cudaGraph_t returned=NULL;enum cudaStreamCaptureStatus status;
    CHECK(cudaStreamGetCaptureInfo_v2(stream,&status,NULL,&returned,&deps,&count));
    VERIFY(status==cudaStreamCaptureStatusActive && returned==graph && count==2 && deps[0]==left && deps[1]==right);
    CHECK(cudaMemsetAsync(ptr,0x52,32,stream));
    CHECK(cudaStreamGetCaptureInfo_v2(stream,&status,NULL,NULL,&deps,&count));VERIFY(count==1);
    cudaGraphNode_t first=deps[0],branch;
    CHECK(cudaGraphAddEmptyNode(&branch,graph,&first,1));
    CHECK(cudaStreamUpdateCaptureDependencies(stream,&branch,1,cudaStreamAddCaptureDependencies));
    CHECK(cudaStreamGetCaptureInfo_v2(stream,&status,NULL,NULL,&deps,&count));VERIFY(count==2);
    EXPECT(cudaStreamUpdateCaptureDependencies(stream,&branch,1,2),cudaErrorInvalidValue);
    CHECK(cudaStreamUpdateCaptureDependencies(stream,&branch,1,cudaStreamSetCaptureDependencies));
    CHECK(cudaStreamGetCaptureInfo_v2(stream,&status,NULL,NULL,&deps,&count));VERIFY(count==1 && deps[0]==branch);
    pthread_t thread;VERIFY(!pthread_create(&thread,NULL,wrong_thread,stream));void *result;
    VERIFY(!pthread_join(thread,&result));VERIFY((uintptr_t)result==cudaErrorStreamCaptureWrongThread);
    CHECK(cudaMemsetAsync(ptr,0x63,32,stream));CHECK(cudaStreamEndCapture(stream,&returned));VERIFY(returned==graph);
    CHECK(cudaGraphInstantiate(&exec,graph,NULL,NULL,0));CHECK(cudaGraphLaunch(exec,stream));CHECK(cudaStreamSynchronize(stream));
    CHECK(cudaMemcpy(host,ptr,32,cudaMemcpyDeviceToHost));for(int i=0;i<32;++i)VERIFY(host[i]==0x63);
    CHECK(cudaGraphExecDestroy(exec));CHECK(cudaGraphDestroy(graph));
    /* The same dependency API also extends ordinary capture. */
    CHECK(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));CHECK(cudaMemsetAsync(ptr,0x74,32,stream));
    CHECK(cudaStreamUpdateCaptureDependencies(stream,NULL,0,cudaStreamSetCaptureDependencies));
    CHECK(cudaStreamGetCaptureInfo_v2(stream,&status,NULL,NULL,&deps,&count));VERIFY(count==0 && deps==NULL);
    CHECK(cudaStreamEndCapture(stream,&graph));CHECK(cudaGraphDestroy(graph));
    CHECK(cudaGraphCreate(&graph,0));CHECK(cudaGraphAddEmptyNode(&left,graph,NULL,0));
    CHECK(cudaStreamBeginCaptureToGraph(stream,graph,&left,NULL,1,cudaStreamCaptureModeThreadLocal));
    EXPECT(cudaMemcpy(host,ptr,32,cudaMemcpyDeviceToHost),cudaErrorStreamCaptureUnsupported);
    EXPECT(cudaStreamEndCapture(stream,&returned),cudaErrorStreamCaptureInvalidated);VERIFY(returned==NULL);
    CHECK(cudaGraphGetNodes(graph,NULL,&count));VERIFY(count==1);CHECK(cudaGraphDestroy(graph));
    CHECK(cudaStreamDestroy(stream));CHECK(cudaStreamDestroy(other));CHECK(cudaFree(ptr));
    puts("CAPTURE_TO_GRAPH_DEPENDENCY_UPDATES=PASS");
    return 0;
}
#endif
