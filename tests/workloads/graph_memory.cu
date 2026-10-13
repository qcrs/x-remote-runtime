#include "common.cuh"
int main() {
    int *out=device(); cudaStream_t s=stream(); cudaGraph_t graph; cudaGraphExec_t exec;
    cudaGraphNode_t alloc,fill,kernel,copy,release; cudaMemAllocNodeParams ap={};
    ap.poolProps.allocType=cudaMemAllocationTypePinned; ap.poolProps.location.type=cudaMemLocationTypeDevice; ap.bytesize=N*sizeof(int);
    CUDA(cudaGraphCreate(&graph,0)); CUDA(cudaGraphAddMemAllocNode(&alloc,graph,nullptr,0,&ap));
    cudaMemsetParams mp={}; mp.dst=ap.dptr; mp.value=11; mp.elementSize=4; mp.width=N; mp.height=1;
    CUDA(cudaGraphAddMemsetNode(&fill,graph,&alloc,1,&mp));
    int *tmp=(int *)ap.dptr,n=N,scale=4,bias=3; void *args[]={&tmp,&tmp,&n,&scale,&bias};
    cudaKernelNodeParams kp={}; kp.func=(void *)affine; kp.gridDim=dim3(N/BLOCK); kp.blockDim=dim3(BLOCK); kp.kernelParams=args;
    CUDA(cudaGraphAddKernelNode(&kernel,graph,&fill,1,&kp));
    cudaMemcpy3DParms cp={}; cp.srcPtr=make_cudaPitchedPtr(tmp,N*sizeof(int),N*sizeof(int),1); cp.dstPtr=make_cudaPitchedPtr(out,N*sizeof(int),N*sizeof(int),1);
    cp.extent=make_cudaExtent(N*sizeof(int),1,1); cp.kind=cudaMemcpyDeviceToDevice;
    CUDA(cudaGraphAddMemcpyNode(&copy,graph,&kernel,1,&cp)); CUDA(cudaGraphAddMemFreeNode(&release,graph,&copy,1,tmp));
    CUDA(cudaGraphInstantiate(&exec,graph,nullptr,nullptr,0)); CUDA(cudaGraphDestroy(graph));
    std::vector<int> values(N);
    for(int r=0;r<4;++r) { CUDA(cudaGraphLaunch(exec,s)); CUDA(cudaMemcpyAsync(values.data(),out,N*sizeof(int),cudaMemcpyDeviceToHost,s)); CUDA(cudaStreamSynchronize(s)); for(int x:values) REQUIRE(x==47); }
    CUDA(cudaGraphExecDestroy(exec)); CUDA(cudaDeviceSynchronize());
    REQUIRE(cudaMemcpy(values.data(),tmp,sizeof(int),cudaMemcpyDeviceToHost)==cudaErrorInvalidDevicePointer);
    REQUIRE(cudaGetLastError()==cudaErrorInvalidDevicePointer);
    CUDA(cudaDeviceGraphMemTrim(0)); CUDA(cudaFree(out)); CUDA(cudaStreamDestroy(s)); pass("graph_memory");
}
