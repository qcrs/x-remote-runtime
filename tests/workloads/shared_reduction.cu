#include "common.cuh"
__global__ void reduce(const int *a,int *b,int n) {
    extern __shared__ int scratch[];
    int t=threadIdx.x,i=blockIdx.x*blockDim.x+t;
    scratch[t]=i<n?a[i]:0; __syncthreads();
    for(int stride=blockDim.x/2;stride;stride/=2) {
        if(t<stride) scratch[t]+=scratch[t+stride]; __syncthreads();
    }
    if(t==0) b[blockIdx.x]=scratch[0];
}
int main() {
    auto h=input(9); int expected=0; for(int x:h) expected+=x;
    int *a=device(),*partial=device(N/BLOCK),*sum=device(1);
    CUDA(cudaMemcpy(a,h.data(),N*sizeof(int),cudaMemcpyHostToDevice));
    reduce<<<N/BLOCK,BLOCK,BLOCK*sizeof(int)>>>(a,partial,N); CUDA(cudaGetLastError());
    reduce<<<1,BLOCK,BLOCK*sizeof(int)>>>(partial,sum,N/BLOCK); CUDA(cudaGetLastError());
    int got=0; CUDA(cudaMemcpy(&got,sum,sizeof(int),cudaMemcpyDeviceToHost)); REQUIRE(got==expected);
    CUDA(cudaFree(a)); CUDA(cudaFree(partial)); CUDA(cudaFree(sum)); pass("shared_reduction");
}
