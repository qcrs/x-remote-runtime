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
#define CHECK(call) do {cudaError_t r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
#define EXPECT(call,want) do {cudaError_t r=(call);printf("%s=%d want=%d\n",#call,r,want);fflush(stdout);if(r!=(want))return 1;}while(0)
#define VERIFY(expr) do {if(!(expr)){printf("FAILED: %s\n",#expr);return 1;}}while(0)
static void *wrong_thread(void *p) {
    return (void *)(uintptr_t)cudaStreamUpdateCaptureDependencies(p,NULL,0,cudaStreamSetCaptureDependencies);
}
int main(void) {
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
