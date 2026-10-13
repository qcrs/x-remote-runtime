#include "common.cuh"
__constant__ int iteration_bias;
__global__ void final_stage(int *a,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) a[i]+=iteration_bias;
}
int main() {
    for(int r=0;r<12;++r) {
        cudaStream_t a=stream(),b=stream(); cudaEvent_t ready=event();
        cudaMemPoolProps props={}; props.allocType=cudaMemAllocationTypePinned; props.location.type=cudaMemLocationTypeDevice;
        cudaMemPool_t pool; CUDA(cudaMemPoolCreate(&pool,&props));
        int *x=device(),*y,*z=device(); CUDA(cudaMallocFromPoolAsync(&y,N*sizeof(int),pool,a));
        auto src=input(r); std::vector<int> out(N,-888);
        CUDA(cudaMemcpyToSymbol(iteration_bias,&r,sizeof(r)));
        CUDA(cudaMemcpyAsync(x,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,a));
        affine<<<N/BLOCK,BLOCK,0,a>>>(x,y,N,2,3); CUDA(cudaGetLastError());
        CUDA(cudaEventRecord(ready,a)); CUDA(cudaStreamWaitEvent(b,ready,0));
        combine<<<N/BLOCK,BLOCK,0,b>>>(x,y,z,N,4); CUDA(cudaGetLastError()); CUDA(cudaStreamSynchronize(b));
        cudaGraph_t graph; cudaGraphExec_t exec; CUDA(cudaStreamBeginCapture(b,cudaStreamCaptureModeThreadLocal));
        affine<<<N/BLOCK,BLOCK,0,b>>>(z,z,N,2,1); CUDA(cudaGetLastError());
        final_stage<<<N/BLOCK,BLOCK,0,b>>>(z,N); CUDA(cudaGetLastError());
        CUDA(cudaStreamEndCapture(b,&graph)); CUDA(cudaGraphInstantiate(&exec,graph,nullptr,nullptr,0));
        for(int replay=0;replay<2;++replay) {
            CUDA(cudaGraphLaunch(exec,b)); CUDA(cudaMemcpyAsync(out.data(),z,N*sizeof(int),cudaMemcpyDeviceToHost,b));
            REQUIRE(out[0]==(replay==0?-888:6*src[0]+15+r)); CUDA(cudaStreamSynchronize(b));
            for(int i=0;i<N;++i) REQUIRE(out[i]==(replay==0?6*src[i]+15+r:12*src[i]+31+3*r));
        }
        CUDA(cudaFreeAsync(y,b)); CUDA(cudaStreamSynchronize(b)); CUDA(cudaGraphExecDestroy(exec)); CUDA(cudaGraphDestroy(graph));
        CUDA(cudaMemPoolDestroy(pool)); CUDA(cudaFree(x)); CUDA(cudaFree(z)); CUDA(cudaEventDestroy(ready)); CUDA(cudaStreamDestroy(a)); CUDA(cudaStreamDestroy(b));
    }
    CUDA(cudaDeviceSynchronize()); pass("mixed_iterations");
}
