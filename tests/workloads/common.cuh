#pragma once
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>
#define CUDA(call) do { cudaError_t workload_error=(call); if(workload_error!=cudaSuccess) { fprintf(stderr,"%s:%d %s: %d %s\n",__FILE__,__LINE__,#call,(int)workload_error,cudaGetErrorString(workload_error)); exit(1); } } while(0)
#define REQUIRE(expr) do { if(!(expr)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#expr); exit(1); } } while(0)
static const int N=4096, BLOCK=128;
__global__ void affine(const int *a,int *b,int n,int scale,int bias) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) b[i]=a[i]*scale+bias;
}
__global__ void combine(const int *a,const int *b,int *out,int n,int bias) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<n) out[i]=a[i]+b[i]+bias;
}
static std::vector<int> input(int seed=0) {
    std::vector<int> v(N); for(int i=0;i<N;++i) v[i]=(i%101)-50+seed; return v;
}
static cudaStream_t stream() { cudaStream_t s; CUDA(cudaStreamCreateWithFlags(&s,cudaStreamNonBlocking)); return s; }
static cudaEvent_t event() { cudaEvent_t e; CUDA(cudaEventCreateWithFlags(&e,cudaEventDisableTiming)); return e; }
static int *device(int n=N) { int *p; CUDA(cudaMalloc(&p,n*sizeof(int))); return p; }
static void pass(const char *name) {
    char netns[128]={}; ssize_t size=readlink("/proc/self/ns/net",netns,sizeof(netns)-1);
    REQUIRE(size>0);
    printf("CLIENT_PID=%d CLIENT_NETNS=%s ENDPOINT=%s:%s\n",getpid(),netns,getenv("COREX_REMOTE_HOST"),getenv("COREX_REMOTE_PORT"));
    printf("%s=PASS\n",name);
}
