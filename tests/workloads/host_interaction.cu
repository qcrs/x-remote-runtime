#include "common.cuh"
struct HostState { const int *values; int *next; int expected; int calls; int pid; bool valid; };
static void consume(void *opaque) {
    HostState *state=(HostState *)opaque; state->pid=getpid(); state->valid=true;
    for(int i=0;i<N;++i) {
        if(state->values[i]!=state->expected) state->valid=false;
        state->next[i]=state->values[i]+1;
    }
    ++state->calls;
}
int main() {
    int *a=device(),*b=device(); std::vector<int> src(N,7),out(N,-1),next(N,-999); cudaStream_t s=stream();
    HostState state={out.data(),next.data(),23,0,0,false};
    CUDA(cudaMemcpyAsync(a,src.data(),N*sizeof(int),cudaMemcpyHostToDevice,s));
    affine<<<N/BLOCK,BLOCK,0,s>>>(a,b,N,3,2); CUDA(cudaGetLastError());
    CUDA(cudaMemcpyAsync(out.data(),b,N*sizeof(int),cudaMemcpyDeviceToHost,s)); CUDA(cudaLaunchHostFunc(s,consume,&state));
    CUDA(cudaMemcpyAsync(a,next.data(),N*sizeof(int),cudaMemcpyHostToDevice,s));
    affine<<<N/BLOCK,BLOCK,0,s>>>(a,b,N,2,1); CUDA(cudaGetLastError());
    CUDA(cudaStreamSynchronize(s)); REQUIRE(state.calls==1 && state.valid && state.pid==getpid());
    CUDA(cudaMemcpy(out.data(),b,N*sizeof(int),cudaMemcpyDeviceToHost)); for(int x:out) REQUIRE(x==49);
    cudaGraph_t graph; cudaGraphExec_t exec; cudaGraphNode_t kernel,tail;
    int n=N,scale=1,bias=0; void *args[]={&a,&b,&n,&scale,&bias}; cudaKernelNodeParams kp={};
    kp.func=(void *)affine; kp.gridDim=dim3(N/BLOCK); kp.blockDim=dim3(BLOCK); kp.kernelParams=args;
    CUDA(cudaGraphCreate(&graph,0)); CUDA(cudaGraphAddKernelNode(&kernel,graph,nullptr,0,&kp));
    state.expected=49; cudaHostNodeParams hp={consume,&state}; CUDA(cudaGraphAddHostNode(&tail,graph,&kernel,1,&hp));
    CUDA(cudaGraphInstantiate(&exec,graph,nullptr,nullptr,0));
    std::vector<int> graph_values(N);
    for(int r=0;r<3;++r) {
        CUDA(cudaGraphLaunch(exec,s)); CUDA(cudaStreamSynchronize(s)); REQUIRE(state.valid && state.calls==r+2 && state.pid==getpid());
        CUDA(cudaMemcpy(graph_values.data(),b,N*sizeof(int),cudaMemcpyDeviceToHost)); for(int x:graph_values) REQUIRE(x==24);
        for(int x:next) REQUIRE(x==50);
    }
    CUDA(cudaGraphExecDestroy(exec)); CUDA(cudaGraphDestroy(graph)); CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaStreamDestroy(s)); pass("host_interaction");
}
