#include "common.cuh"
int main() {
    cudaStream_t a=stream(),b=stream(); cudaEvent_t ready=event(),done=event();
    int *in=device(),*mid=device(),*out=device();
    for(int round=0;round<6;++round) {
        auto src=input(round); std::vector<int> got(N,-777);
        CUDA(cudaMemcpyAsync(in,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,a));
        affine<<<N/BLOCK,BLOCK,0,a>>>(in,mid,N,2,round); CUDA(cudaGetLastError());
        CUDA(cudaEventRecord(ready,a)); CUDA(cudaStreamWaitEvent(b,ready,0));
        combine<<<N/BLOCK,BLOCK,0,b>>>(in,mid,out,N,5); CUDA(cudaGetLastError());
        CUDA(cudaMemcpyAsync(got.data(),out,N*sizeof(int),cudaMemcpyDeviceToHost,b));
        REQUIRE(got[0]==-777 && got[N-1]==-777);
        CUDA(cudaEventRecord(done,b)); CUDA(cudaEventSynchronize(done));
        for(int i=0;i<N;++i) REQUIRE(got[i]==3*src[i]+round+5);
        CUDA(cudaStreamWaitEvent(a,done,0));
    }
    CUDA(cudaFree(in)); CUDA(cudaFree(mid)); CUDA(cudaFree(out));
    CUDA(cudaEventDestroy(ready)); CUDA(cudaEventDestroy(done)); CUDA(cudaStreamDestroy(a)); CUDA(cudaStreamDestroy(b)); pass("stream_pipeline");
}
