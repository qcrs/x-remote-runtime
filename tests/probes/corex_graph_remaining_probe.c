#define _GNU_SOURCE
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <dlfcn.h>
#include <string.h>
#define OBS(call) do{int r=(call);printf("%s=%d\n",#call,r);fflush(stdout);}while(0)
static atomic_int destroyed;
static atomic_int destructor_entered,destructor_release,destructor_finished;
static CUcontext destructor_context;
static CUUserObject blocked_object;
static void CUDA_CB destroy(void *p){atomic_fetch_add((atomic_int *)p,1);}
static void CUDA_CB blocked_destroy(void *p) {
    (void)p;atomic_store(&destructor_entered,1);struct timespec nap={0,1000000};
    while(!atomic_load(&destructor_release))nanosleep(&nap,NULL);
    atomic_store(&destructor_finished,1);
}
static void *release_object(void *p) {
    (void)p;OBS(cuCtxSetCurrent(destructor_context));OBS(cuUserObjectRelease(blocked_object,1));return NULL;
}
static void *mode_thread(void *opaque) {
    (void)opaque;
    enum cudaStreamCaptureMode mode=cudaStreamCaptureModeRelaxed;
    OBS(cudaThreadExchangeStreamCaptureMode(&mode));printf("OTHER_THREAD_PREVIOUS_MODE=%d\n",mode);
    OBS(cudaThreadExchangeStreamCaptureMode(&mode));return NULL;
}
int main(void) {
    CUdevice device;CUcontext ctx;
    if(cuInit(0)||cuDeviceGet(&device,0)||cuCtxCreate(&ctx,0,device))return 1;
    void *fn=NULL;
    OBS(cuGetProcAddress("cuGraphUpload",&fn,11010,0));printf("UPLOAD_ENTRY_PRESENT=%d\n",fn!=NULL);
    printf("UPLOAD_PTSZ_SYMBOL_PRESENT=%d\n",dlsym(RTLD_DEFAULT,"cuGraphUpload_ptsz")!=NULL);
    CUgraph graph,clone;CUgraphNode empty;CUgraphExec exec;CUUserObject object=NULL;
    if(cuGraphCreate(&graph,0)||cuGraphAddEmptyNode(&empty,graph,NULL,0))return 1;
    OBS(cuUserObjectCreate(&object,&destroyed,destroy,1,1));
    if(object) {
        OBS(cuGraphRetainUserObject(graph,object,1,0));OBS(cuUserObjectRelease(object,1));
        if(cuGraphClone(&clone,graph)||cuGraphInstantiate(&exec,graph,NULL,NULL,0))return 1;
        CUresult (*upload)(CUgraphExec,CUstream)=(CUresult (*)(CUgraphExec,CUstream))fn;
        if(upload)OBS(upload(exec,0));
        OBS(cuGraphLaunch(exec,0));OBS(cuCtxSynchronize());OBS(cuGraphDestroy(graph));
        printf("USER_DESTRUCTOR_AFTER_SOURCE_DESTROY=%d\n",atomic_load(&destroyed));
        OBS(cuGraphExecDestroy(exec));printf("USER_DESTRUCTOR_WITH_CLONE=%d\n",atomic_load(&destroyed));
        OBS(cuGraphDestroy(clone));
        struct timespec nap={0,1000000};for(int i=0;i<1000 && !atomic_load(&destroyed);++i)nanosleep(&nap,NULL);
        printf("USER_DESTRUCTOR_AFTER_LAST_OWNER=%d\n",atomic_load(&destroyed));
    } else OBS(cuGraphDestroy(graph));
    if(fn) {
        CUdeviceptr ptr;CUgraphNode fill;unsigned char host[32];
        if(cuMemAlloc(&ptr,32)||cuMemsetD8(ptr,0x11,32)||cuGraphCreate(&graph,0))return 1;
        CUDA_MEMSET_NODE_PARAMS p={.dst=ptr,.value=0x55,.elementSize=1,.width=32,.height=1};
        if(cuGraphAddMemsetNode(&fill,graph,NULL,0,&p,ctx)||cuGraphInstantiate(&exec,graph,NULL,NULL,0))return 1;
        CUresult (*upload)(CUgraphExec,CUstream)=(CUresult (*)(CUgraphExec,CUstream))fn;
        OBS(upload(exec,0));if(cuCtxSynchronize()||cuMemcpyDtoH(host,ptr,32))return 1;
        for(int i=0;i<32;++i)if(host[i]!=0x11)return 2;
        OBS(cuGraphLaunch(exec,0));if(cuCtxSynchronize()||cuMemcpyDtoH(host,ptr,32))return 1;
        for(int i=0;i<32;++i)if(host[i]!=0x55)return 3;
        puts("DRIVER_UPLOAD_PREPARES_WITHOUT_EXECUTION=PASS");
        OBS(cuGraphExecDestroy(exec));OBS(cuGraphDestroy(graph));OBS(cuMemFree(ptr));
        CUDA_MEM_ALLOC_NODE_PARAMS mem={0};CUgraphNode allocation;
        mem.poolProps.allocType=CU_MEM_ALLOCATION_TYPE_PINNED;mem.poolProps.location.type=CU_MEM_LOCATION_TYPE_DEVICE;mem.bytesize=32;
        if(cuGraphCreate(&graph,0)||cuGraphAddMemAllocNode(&allocation,graph,NULL,0,&mem)||cuGraphInstantiate(&exec,graph,NULL,NULL,0))return 1;
        OBS(upload(exec,0));OBS(cuGraphLaunch(exec,0));OBS(cuCtxSynchronize());
        OBS(cuMemFree(mem.dptr));
        OBS(upload(exec,0));OBS(cuMemFree(mem.dptr));OBS(cuGraphLaunch(exec,0));OBS(cuCtxSynchronize());
        OBS(cuMemFree(mem.dptr));OBS(cuGraphExecDestroy(exec));OBS(cuGraphDestroy(graph));
        cuuint64_t used=0;
        OBS(cuDeviceGetGraphMemAttribute(device,CU_GRAPH_MEM_ATTR_USED_MEM_CURRENT,&used));
        printf("UNPAIRED_UPLOAD_USED_AFTER_FREE_DESTROY=%llu\n",(unsigned long long)used);
        OBS(cuDeviceGraphMemTrim(device));
        OBS(cuDeviceGetGraphMemAttribute(device,CU_GRAPH_MEM_ATTR_USED_MEM_CURRENT,&used));
        printf("UNPAIRED_UPLOAD_USED_AFTER_FREE_DESTROY_TRIM=%llu\n",(unsigned long long)used);
        CUgraphNode free_node;mem.dptr=0;
        if(cuGraphCreate(&graph,0)||cuGraphAddMemAllocNode(&allocation,graph,NULL,0,&mem)||
           cuGraphAddMemFreeNode(&free_node,graph,&allocation,1,mem.dptr)||cuGraphInstantiate(&exec,graph,NULL,NULL,0))return 1;
        puts("PAIRED_MEMORY_UPLOAD_BEGIN");OBS(upload(exec,0));OBS(cuMemFree(mem.dptr));
        OBS(cuGraphExecDestroy(exec));OBS(cuGraphDestroy(graph));
    }
    destructor_context=ctx;
    OBS(cuUserObjectCreate(&blocked_object,&destroyed,blocked_destroy,1,1));
    if(blocked_object) {
        pthread_t release_thread;if(pthread_create(&release_thread,NULL,release_object,NULL))return 1;
        struct timespec nap={0,1000000};for(int i=0;i<1000 && !atomic_load(&destructor_entered);++i)nanosleep(&nap,NULL);
        if(!atomic_load(&destructor_entered))return 4;
        OBS(cuCtxSynchronize());
        printf("USER_DESTRUCTOR_NO_SYNC=%d\n",!atomic_load(&destructor_finished));
        atomic_store(&destructor_release,1);if(pthread_join(release_thread,NULL))return 1;
        for(int i=0;i<1000 && !atomic_load(&destructor_finished);++i)nanosleep(&nap,NULL);
        if(!atomic_load(&destructor_finished))return 5;
    }
    OBS(cuCtxDestroy(ctx));
    cudaStream_t stream=NULL;if(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking))return 1;
    enum cudaStreamCaptureMode mode=cudaStreamCaptureModeRelaxed;
    OBS(cudaThreadExchangeStreamCaptureMode(&mode));printf("MAIN_PREVIOUS_MODE=%d\n",mode);
    pthread_t thread;if(pthread_create(&thread,NULL,mode_thread,NULL)||pthread_join(thread,NULL))return 1;
    mode=cudaStreamCaptureModeGlobal;OBS(cudaThreadExchangeStreamCaptureMode(&mode));printf("MAIN_AFTER_OTHER_THREAD=%d\n",mode);
    for(int interaction=0;interaction<3;++interaction) {
        mode=(enum cudaStreamCaptureMode)interaction;OBS(cudaThreadExchangeStreamCaptureMode(&mode));
        OBS(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal));
        void *p=NULL;int result=cudaMalloc(&p,32);
        printf("INTERACTION_%d_MALLOC=%d\n",interaction,result);
        enum cudaStreamCaptureStatus status;OBS(cudaStreamIsCapturing(stream,&status));printf("INTERACTION_%d_STATUS=%d\n",interaction,status);
        cudaGraph_t capture=NULL;OBS(cudaStreamEndCapture(stream,&capture));if(capture)OBS(cudaGraphDestroy(capture));
        if(p)OBS(cudaFree(p));
    }
    OBS(cudaStreamDestroy(stream));return 0;
}
