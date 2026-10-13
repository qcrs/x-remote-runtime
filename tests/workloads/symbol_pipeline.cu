#include "common.cuh"
__device__ int generation=1;
__constant__ int coefficients[2]={2,3};
__global__ void symbol_transform(const int *a,int *b,int n) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) b[i]=a[i]*coefficients[0]+coefficients[1]+generation;
}
int main() {
    int *a=device(),*b=device(); auto src=input(); std::vector<int> out(N); cudaStream_t s=stream();
    CUDA(cudaMemcpyAsync(a,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,s));
    for(int r=0;r<5;++r) {
        int bias=r+4,gen=r+10;
        CUDA(cudaMemcpyToSymbolAsync(generation,&gen,sizeof(gen),0,cudaMemcpyHostToDevice,s));
        CUDA(cudaMemcpyToSymbolAsync(coefficients,&bias,sizeof(bias),sizeof(int),cudaMemcpyHostToDevice,s));
        symbol_transform<<<N/BLOCK,BLOCK,0,s>>>(a,b,N); CUDA(cudaGetLastError());
        CUDA(cudaMemcpyAsync(out.data(),b,N*sizeof(int),cudaMemcpyDeviceToHost,s)); CUDA(cudaStreamSynchronize(s));
        for(int i=0;i<N;++i) REQUIRE(out[i]==2*src[i]+bias+gen);
        int got=0; CUDA(cudaMemcpyFromSymbol(&got,generation,sizeof(got))); REQUIRE(got==gen);
    }
    CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaStreamDestroy(s)); pass("symbol_pipeline");
}
