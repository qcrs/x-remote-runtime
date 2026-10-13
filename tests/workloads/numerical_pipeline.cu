#include "common.cuh"
int main() {
    auto h=input(); std::vector<int> out(N);
    int *a=device(),*b=device(),*c=device(); cudaStream_t s=stream();
    CUDA(cudaMemcpyAsync(a,h.data(),N*sizeof(int),cudaMemcpyHostToDevice,s));
    affine<<<N/BLOCK,BLOCK,0,s>>>(a,b,N,3,7); CUDA(cudaGetLastError());
    combine<<<N/BLOCK,BLOCK,0,s>>>(a,b,c,N,-2); CUDA(cudaGetLastError());
    CUDA(cudaMemcpyAsync(out.data(),c,N*sizeof(int),cudaMemcpyDeviceToHost,s));
    CUDA(cudaStreamSynchronize(s));
    for(int i=0;i<N;++i) REQUIRE(out[i]==4*h[i]+5);
    CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaFree(c)); CUDA(cudaStreamDestroy(s));
    pass("numerical_pipeline");
}
