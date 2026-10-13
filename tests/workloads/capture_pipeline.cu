#include "common.cuh"
int main() {
    int *a=device(),*b=device(),*c=device(); cudaStream_t s=stream();
    auto src=input(); CUDA(cudaMemcpy(a,src.data(),N*sizeof(int),cudaMemcpyHostToDevice));
    cudaGraph_t graph; cudaGraphExec_t exec;
    CUDA(cudaStreamBeginCapture(s,cudaStreamCaptureModeThreadLocal));
    CUDA(cudaMemsetAsync(b,0,N*sizeof(int),s));
    affine<<<N/BLOCK,BLOCK,0,s>>>(a,b,N,2,3); CUDA(cudaGetLastError());
    CUDA(cudaMemcpyAsync(c,b,N*sizeof(int),cudaMemcpyDeviceToDevice,s));
    combine<<<N/BLOCK,BLOCK,0,s>>>(a,c,b,N,7); CUDA(cudaGetLastError());
    CUDA(cudaStreamEndCapture(s,&graph)); CUDA(cudaGraphInstantiate(&exec,graph,nullptr,nullptr,0));
    std::vector<int> out(N);
    for(int r=0;r<5;++r) {
        src=input(r); CUDA(cudaMemcpyAsync(a,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,s));
        CUDA(cudaGraphLaunch(exec,s)); CUDA(cudaMemcpyAsync(out.data(),b,N*sizeof(int),cudaMemcpyDeviceToHost,s)); CUDA(cudaStreamSynchronize(s));
        for(int i=0;i<N;++i) REQUIRE(out[i]==3*src[i]+10);
    }
    CUDA(cudaGraphExecDestroy(exec)); CUDA(cudaGraphDestroy(graph)); CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaFree(c)); CUDA(cudaStreamDestroy(s)); pass("capture_pipeline");
}
