#ifdef COREX_GRAPH_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifndef COREX_GRAPH_INTERNAL_ABI
#define CHECK(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
#define EXPECT(call,want) do{cudaError_t r=(call);printf("%s=%d want=%d\n",#call,r,want);fflush(stdout);if(r!=(want))return 1;}while(0)
#define VERIFY(expr) do{if(!(expr)){printf("FAILED %s\n",#expr);return 1;}}while(0)
typedef struct {void *ptr;cudaError_t result;int previous;} ThreadData;
static void *allocate_other_thread(void *opaque) {
    ThreadData *d=opaque;enum cudaStreamCaptureMode mode=cudaStreamCaptureModeThreadLocal;
    d->result=cudaThreadExchangeStreamCaptureMode(&mode);d->previous=mode;
    if(!d->result)d->result=cudaMalloc(&d->ptr,32);
    return NULL;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    cudaStream_t stream;CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    enum cudaStreamCaptureMode mode=cudaStreamCaptureModeRelaxed;
    CHECK(cudaThreadExchangeStreamCaptureMode(&mode));VERIFY(mode==cudaStreamCaptureModeGlobal);
    EXPECT(cudaThreadExchangeStreamCaptureMode(NULL),cudaErrorInvalidValue);
    enum cudaStreamCaptureMode invalid=(enum cudaStreamCaptureMode)3;
    EXPECT(cudaThreadExchangeStreamCaptureMode(&invalid),cudaErrorInvalidValue);
    CHECK(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));void *ptr=NULL;
    CHECK(cudaMalloc(&ptr,32));CHECK(cudaMemsetAsync(ptr,0x39,32,stream));
    ThreadData d={0};pthread_t thread;VERIFY(!pthread_create(&thread,NULL,allocate_other_thread,&d));
    VERIFY(!pthread_join(thread,NULL));VERIFY(d.result==cudaSuccess && d.previous==cudaStreamCaptureModeGlobal);
    enum cudaStreamCaptureStatus status;CHECK(cudaStreamIsCapturing(stream,&status));VERIFY(status==cudaStreamCaptureStatusActive);
    cudaGraph_t graph;CHECK(cudaStreamEndCapture(stream,&graph));
    mode=cudaStreamCaptureModeGlobal;CHECK(cudaThreadExchangeStreamCaptureMode(&mode));VERIFY(mode==cudaStreamCaptureModeRelaxed);
    CHECK(cudaGraphDebugDotPrint(graph,argv[1],0));
    FILE *file=fopen(argv[1],"r");VERIFY(file!=NULL);char text[2048];size_t len=fread(text,1,sizeof(text)-1,file);fclose(file);text[len]=0;
    VERIFY(strstr(text,"digraph cuda_graph") && strstr(text,"Memset"));
    EXPECT(cudaGraphDebugDotPrint(graph,argv[1],1),cudaErrorNotSupported);
    EXPECT(cudaGraphDebugDotPrint(graph,"/no-such-corex-directory/graph.dot",0),cudaErrorOperatingSystem);
    cudaGraphExec_t exec;CHECK(cudaGraphInstantiate(&exec,graph,NULL,NULL,0));CHECK(cudaGraphUpload(exec,stream));
    CHECK(cudaGraphLaunch(exec,stream));CHECK(cudaStreamSynchronize(stream));
    unsigned char host[32];CHECK(cudaMemcpy(host,ptr,32,cudaMemcpyDeviceToHost));for(int i=0;i<32;++i)VERIFY(host[i]==0x39);
    CHECK(cudaGraphExecDestroy(exec));CHECK(cudaGraphDestroy(graph));CHECK(cudaFree(ptr));CHECK(cudaFree(d.ptr));
    /* The owner's default interaction remains conservative after restoring TLS. */
    CHECK(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
    EXPECT(cudaMalloc(&ptr,32),cudaErrorStreamCaptureUnsupported);
    EXPECT(cudaStreamEndCapture(stream,&graph),cudaErrorStreamCaptureInvalidated);
    CHECK(cudaStreamDestroy(stream));puts("GRAPH_CONTROLS_TLS_CAPTURE_ALLOCATION_UPLOAD_CLIENT_DOT=PASS");return 0;
}
#endif
