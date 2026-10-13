#include <cuda.h>
#include <stdio.h>
#define CHECK(call) do{CUresult r=(call);printf("%s=%d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
static int launch(CUgraphExec exec,CUstream stream,CUdeviceptr out,const char *label) {
    int value=0;CHECK(cuGraphLaunch(exec,stream));CHECK(cuStreamSynchronize(stream));
    CHECK(cuMemcpyDtoH(&value,out,4));printf("%s=%d\n",label,value);fflush(stdout);return 0;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    CUdevice dev;CUcontext ctx;CUstream stream;CUmodule module;CUfunction fn;CUdeviceptr ptr,out;
    CHECK(cuInit(0));CHECK(cuDeviceGet(&dev,0));CHECK(cuCtxCreate(&ctx,0,dev));
    CHECK(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));CHECK(cuMemAlloc(&ptr,4));CHECK(cuMemAlloc(&out,4));
    CHECK(cuModuleLoad(&module,argv[1]));CHECK(cuModuleGetFunction(&fn,module,"m1s2_lifecycle_kernel"));
    CUgraph source,parent,retained=NULL;CUgraphNode fill,kernel,copy,child;CUgraphExec first,second,third,fourth,fifth;
    CHECK(cuGraphCreate(&source,0));CHECK(cuGraphCreate(&parent,0));
    CUDA_MEMSET_NODE_PARAMS f={.dst=ptr,.value=10,.elementSize=4,.width=1,.height=1};
    CHECK(cuGraphAddMemsetNode(&fill,source,NULL,0,&f,ctx));
    void *args[]={&ptr};CUDA_KERNEL_NODE_PARAMS k={.func=fn,.gridDimX=1,.gridDimY=1,.gridDimZ=1,
        .blockDimX=1,.blockDimY=1,.blockDimZ=1,.kernelParams=args};
    CHECK(cuGraphAddKernelNode(&kernel,source,&fill,1,&k));
    CUDA_MEMCPY3D cp={0};cp.srcMemoryType=cp.dstMemoryType=CU_MEMORYTYPE_DEVICE;
    cp.srcDevice=ptr;cp.dstDevice=out;cp.srcPitch=cp.dstPitch=4;cp.srcHeight=cp.dstHeight=1;
    cp.WidthInBytes=4;cp.Height=cp.Depth=1;
    CHECK(cuGraphAddMemcpyNode(&copy,source,&kernel,1,&cp,ctx));
    CHECK(cuGraphAddChildGraphNode(&child,parent,NULL,0,source));
    CHECK(cuGraphInstantiate(&first,parent,NULL,NULL,0));CHECK(cuGraphInstantiate(&second,parent,NULL,NULL,0));
    CHECK(cuGraphInstantiate(&third,parent,NULL,NULL,0));
    CHECK(cuGraphInstantiate(&fourth,parent,NULL,NULL,0));CHECK(cuGraphInstantiate(&fifth,parent,NULL,NULL,0));
    f.value=20;CHECK(cuGraphMemsetNodeSetParams(fill,&f));
    CHECK(cuGraphExecChildGraphNodeSetParams(fourth,child,source));CHECK(cuGraphExecChildGraphNodeSetParams(fourth,child,source));
    if(launch(fourth,stream,out,"FIRST_DOUBLE_DIRECT_UPDATE"))return 1;
    CHECK(cuGraphClone(&retained,source));CHECK(cuGraphExecChildGraphNodeSetParams(fifth,child,retained));
    CHECK(cuGraphExecChildGraphNodeSetParams(fifth,child,retained));CHECK(cuGraphDestroy(retained));retained=NULL;
    if(launch(fifth,stream,out,"FIRST_DOUBLE_CLONE_UPDATE"))return 1;
    CHECK(cuGraphClone(&retained,source));CHECK(cuGraphExecChildGraphNodeSetParams(third,child,retained));
    CHECK(cuGraphDestroy(retained));retained=NULL;
    if(launch(third,stream,out,"FIRST_UPDATE_FROM_FRESH_CLONE"))return 1;
    CHECK(cuGraphExecChildGraphNodeSetParams(second,child,source));
    if(launch(second,stream,out,"DIRECT_UPDATE_BEFORE_DESTROY") || launch(first,stream,out,"ORIGINAL_EXEC_SNAPSHOT"))return 1;
    CHECK(cuGraphClone(&retained,source));CHECK(cuGraphExecChildGraphNodeSetParams(second,child,retained));
    CHECK(cuGraphDestroy(retained));retained=NULL;
    if(launch(second,stream,out,"FRESH_CLONE_UPDATE_AFTER_CLONE_DESTROY"))return 1;
    CHECK(cuGraphDestroy(source));
    if(launch(second,stream,out,"DIRECT_UPDATE_AFTER_DESTROY"))return 1;
    CHECK(cuGraphCreate(&source,0));f.value=30;CHECK(cuGraphAddMemsetNode(&fill,source,NULL,0,&f,ctx));
    CHECK(cuGraphAddKernelNode(&kernel,source,&fill,1,&k));CHECK(cuGraphAddMemcpyNode(&copy,source,&kernel,1,&cp,ctx));
    CHECK(cuGraphClone(&retained,source));CHECK(cuGraphExecChildGraphNodeSetParams(second,child,retained));
    CHECK(cuGraphDestroy(retained));retained=NULL;
    CHECK(cuGraphDestroy(source));
    if(launch(second,stream,out,"RETAINED_CLONE_UPDATE_AFTER_DESTROY"))return 1;
    CHECK(cuGraphDestroy(parent));
    if(launch(first,stream,out,"ORIGINAL_AFTER_PARENT_DESTROY") || launch(second,stream,out,"RETAINED_AFTER_PARENT_DESTROY"))return 1;
    /* Replacement materialization never launches the previous definition. */
    CHECK(cuGraphCreate(&source,0));f.value=20;CHECK(cuGraphAddMemsetNode(&fill,source,NULL,0,&f,ctx));
    CHECK(cuGraphAddKernelNode(&kernel,source,&fill,1,&k));CHECK(cuGraphAddMemcpyNode(&copy,source,&kernel,1,&cp,ctx));
    CHECK(cuGraphCreate(&parent,0));CHECK(cuGraphAddChildGraphNode(&child,parent,NULL,0,source));
    CUgraphExec rebuilt;CHECK(cuGraphInstantiate(&rebuilt,parent,NULL,NULL,0));
    CHECK(cuGraphDestroy(source));CHECK(cuGraphDestroy(parent));
    if(launch(rebuilt,stream,out,"REBUILT_EXEC_FIRST_LAUNCH"))return 1;
    CHECK(cuGraphExecDestroy(rebuilt));
    CHECK(cuGraphExecDestroy(first));CHECK(cuGraphExecDestroy(second));CHECK(cuGraphExecDestroy(third));
    CHECK(cuGraphExecDestroy(fourth));CHECK(cuGraphExecDestroy(fifth));
    CHECK(cuModuleUnload(module));CHECK(cuMemFree(ptr));CHECK(cuMemFree(out));CHECK(cuStreamDestroy(stream));CHECK(cuCtxDestroy(ctx));return 0;
}
