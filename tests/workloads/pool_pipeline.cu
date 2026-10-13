#include "common.cuh"
int main() {
    cudaMemPoolProps props={}; props.allocType=cudaMemAllocationTypePinned; props.location.type=cudaMemLocationTypeDevice;
    cudaMemPool_t pool; CUDA(cudaMemPoolCreate(&pool,&props)); cudaStream_t a=stream(),b=stream(); cudaEvent_t ready=event();
    for(int r=0;r<6;++r) {
        int *x,*y; CUDA(cudaMallocFromPoolAsync(&x,N*sizeof(int),pool,a)); CUDA(cudaMallocAsync(&y,N*sizeof(int),a));
        auto src=input(r); std::vector<int> out(N,-1);
        CUDA(cudaMemcpyAsync(x,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,a));
        affine<<<N/BLOCK,BLOCK,0,a>>>(x,y,N,5,r); CUDA(cudaGetLastError());
        CUDA(cudaEventRecord(ready,a)); CUDA(cudaStreamWaitEvent(b,ready,0));
        CUDA(cudaMemcpyAsync(out.data(),y,N*sizeof(int),cudaMemcpyDeviceToHost,b));
        CUDA(cudaFreeAsync(x,b)); CUDA(cudaFreeAsync(y,b)); CUDA(cudaStreamSynchronize(b));
        for(int i=0;i<N;++i) REQUIRE(out[i]==5*src[i]+r);
    }
    CUDA(cudaMemPoolTrimTo(pool,0)); CUDA(cudaMemPoolDestroy(pool)); CUDA(cudaEventDestroy(ready));
    CUDA(cudaStreamDestroy(a)); CUDA(cudaStreamDestroy(b)); pass("pool_pipeline");
}
