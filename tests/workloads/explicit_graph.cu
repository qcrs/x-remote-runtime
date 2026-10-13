#include "common.cuh"
int main() {
    int *a=device(),*b=device(),*c=device(); cudaStream_t s=stream(); cudaEvent_t done=event();
    cudaGraph_t graph; cudaGraphExec_t exec; cudaGraphNode_t fill,kernel,copy,record;
    CUDA(cudaGraphCreate(&graph,0)); cudaMemsetParams mp={}; mp.dst=a; mp.value=7; mp.elementSize=4; mp.width=N; mp.height=1;
    CUDA(cudaGraphAddMemsetNode(&fill,graph,nullptr,0,&mp));
    int n=N,scale=3,bias=2; void *args[]={&a,&b,&n,&scale,&bias};
    cudaKernelNodeParams kp={}; kp.func=(void *)affine; kp.gridDim=dim3(N/BLOCK); kp.blockDim=dim3(BLOCK); kp.kernelParams=args;
    CUDA(cudaGraphAddKernelNode(&kernel,graph,&fill,1,&kp));
    cudaMemcpy3DParms cp={}; cp.srcPtr=make_cudaPitchedPtr(b,N*sizeof(int),N*sizeof(int),1); cp.dstPtr=make_cudaPitchedPtr(c,N*sizeof(int),N*sizeof(int),1);
    cp.extent=make_cudaExtent(N*sizeof(int),1,1); cp.kind=cudaMemcpyDeviceToDevice;
    CUDA(cudaGraphAddMemcpyNode(&copy,graph,&kernel,1,&cp)); CUDA(cudaGraphAddEventRecordNode(&record,graph,&copy,1,done));
    CUDA(cudaGraphInstantiate(&exec,graph,nullptr,nullptr,0)); std::vector<int> out(N);
    for(int r=0;r<5;++r) {
        bias=r+2; CUDA(cudaGraphExecKernelNodeSetParams(exec,kernel,&kp));
        CUDA(cudaGraphLaunch(exec,s)); CUDA(cudaEventSynchronize(done));
        CUDA(cudaMemcpy(out.data(),c,N*sizeof(int),cudaMemcpyDeviceToHost));
        for(int x:out) REQUIRE(x==21+bias);
    }
    CUDA(cudaGraphDestroy(graph)); CUDA(cudaGraphExecDestroy(exec)); CUDA(cudaEventDestroy(done)); CUDA(cudaStreamDestroy(s));
    CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaFree(c)); pass("explicit_graph");
}
