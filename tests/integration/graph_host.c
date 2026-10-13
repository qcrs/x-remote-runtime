#ifdef COREX_GRAPH_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
_Static_assert(sizeof(struct cudaHostNodeParams)==16,"host params ABI");
#ifndef COREX_GRAPH_INTERNAL_ABI
#define CHECK(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
#define EXPECT(call,want) do{cudaError_t r=(call);printf("%s=%d want=%d\n",#call,r,want);fflush(stdout);if(r!=(want))return 1;}while(0)
#define VERIFY(expr) do{if(!(expr)){printf("FAILED %s\n",#expr);return 1;}}while(0)
typedef struct {atomic_int calls,entered,release;int wait,pid,seen;unsigned char *host;} Data;
static void host_one(void *opaque) {
    Data *data=opaque;data->pid=getpid();data->seen=data->host ? data->host[0] : 0;
    atomic_store(&data->entered,1);
    struct timespec nap={0,1000000};while(data->wait && !atomic_load(&data->release))nanosleep(&nap,NULL);
    atomic_fetch_add(&data->calls,1);
}
static void host_two(void *opaque){host_one(opaque);atomic_fetch_add(&((Data *)opaque)->calls,9);}
static void host_three(void *opaque){host_one(opaque);atomic_fetch_add(&((Data *)opaque)->calls,99);}
typedef struct {void *ptr;cudaStream_t stream;atomic_int done;cudaError_t result;} Submit;
static void *next_gpu(void *opaque) {
    Submit *s=opaque;s->result=cudaMemsetAsync(s->ptr,0x66,32,s->stream);atomic_store(&s->done,1);return NULL;
}
int main(void) {
    void *ptr;cudaStream_t stream;CHECK(cudaMalloc(&ptr,32));CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    unsigned char source[32],visible[32];memset(source,0x19,32);memset(visible,0,32);
    Data one={.wait=1,.host=visible},two={0},three={0};
    cudaGraph_t graph,clone;cudaGraphNode_t fill,host;cudaGraphExec_t original,updated,copied;
    CHECK(cudaGraphCreate(&graph,0));struct cudaMemsetParams p={.dst=ptr,.value=0x55,.elementSize=1,.width=32,.height=1};
    CHECK(cudaGraphAddMemsetNode(&fill,graph,NULL,0,&p));
    struct cudaHostNodeParams hp={.fn=host_one,.userData=&one},got;
    CHECK(cudaGraphAddHostNode(&host,graph,&fill,1,&hp));CHECK(cudaGraphHostNodeGetParams(host,&got));VERIFY(got.fn==host_one && got.userData==&one);
    CHECK(cudaGraphInstantiate(&original,graph,NULL,NULL,0));
    hp.fn=host_two;hp.userData=&two;CHECK(cudaGraphHostNodeSetParams(host,&hp));
    CHECK(cudaGraphClone(&clone,graph));CHECK(cudaGraphInstantiate(&copied,clone,NULL,NULL,0));
    CHECK(cudaGraphInstantiate(&updated,graph,NULL,NULL,0));
    hp.fn=host_three;hp.userData=&three;CHECK(cudaGraphExecHostNodeSetParams(updated,host,&hp));
    CHECK(cudaGraphHostNodeGetParams(host,&got));VERIFY(got.fn==host_two && got.userData==&two);
    CHECK(cudaGraphDestroy(graph));CHECK(cudaGraphDestroy(clone));
    CHECK(cudaGraphUpload(original,stream));CHECK(cudaStreamSynchronize(stream));VERIFY(atomic_load(&one.calls)==0);
    CHECK(cudaMemcpyAsync(ptr,source,32,cudaMemcpyHostToDevice,stream));
    CHECK(cudaMemcpyAsync(visible,ptr,32,cudaMemcpyDeviceToHost,stream));
    CHECK(cudaGraphLaunch(original,stream)); /* Must return while callback is blocked. */
    struct timespec nap={0,1000000};for(int i=0;i<2000 && !atomic_load(&one.entered);++i)nanosleep(&nap,NULL);
    VERIFY(atomic_load(&one.entered)==1 && atomic_load(&one.calls)==0 && one.pid==getpid() && one.seen==0x19);
    Submit submit={.ptr=ptr,.stream=stream};pthread_t thread;VERIFY(!pthread_create(&thread,NULL,next_gpu,&submit));
    struct timespec delay={0,20000000};nanosleep(&delay,NULL);VERIFY(!atomic_load(&submit.done));
    atomic_store(&one.release,1);VERIFY(!pthread_join(thread,NULL));VERIFY(submit.result==cudaSuccess);
    CHECK(cudaStreamSynchronize(stream));VERIFY(atomic_load(&one.calls)==1);
    CHECK(cudaMemcpy(visible,ptr,32,cudaMemcpyDeviceToHost));VERIFY(visible[0]==0x66);
    CHECK(cudaGraphLaunch(updated,stream));CHECK(cudaStreamSynchronize(stream));VERIFY(atomic_load(&three.calls)==100);
    CHECK(cudaGraphLaunch(copied,stream));CHECK(cudaStreamSynchronize(stream));VERIFY(atomic_load(&two.calls)==10);
    CHECK(cudaGraphExecDestroy(original));CHECK(cudaGraphExecDestroy(updated));CHECK(cudaGraphExecDestroy(copied));
    /* A host node with a GPU successor requires a finer bridge, so reject it. */
    CHECK(cudaGraphCreate(&graph,0));hp.fn=host_one;hp.userData=&one;
    CHECK(cudaGraphAddHostNode(&host,graph,NULL,0,&hp));CHECK(cudaGraphAddMemsetNode(&fill,graph,&host,1,&p));
    EXPECT(cudaGraphInstantiate(&original,graph,NULL,NULL,0),cudaErrorNotSupported);CHECK(cudaGraphDestroy(graph));
    CHECK(cudaStreamDestroy(stream));CHECK(cudaFree(ptr));
    puts("GRAPH_HOST_CLIENT_PROCESS_ASYNC_FRONTIER_ORDER_SNAPSHOTS=PASS");return 0;
}
#endif
