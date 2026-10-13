#include <cuda_runtime_api.h>
#include <stdio.h>
#include <string.h>
#define CHECK(call) do {cudaError_t r=(call);printf("%s=%d\n",#call,r);if(r)return 1;} while(0)
int main(int argc,char **argv)
{
    int observe=argc==2 && !strcmp(argv[1],"--observe-baseline");
    cudaGraph_t graph; cudaGraphExec_t exec; unsigned flags=99;
    CHECK(cudaGraphCreate(&graph,0));
    cudaError_t result=cudaSetDeviceFlags(cudaDeviceScheduleSpin);
    printf("FLAGS_WITH_GRAPH=%d expected=%d\n",result,cudaErrorNotSupported);
    if(observe) {CHECK(cudaGetDeviceFlags(&flags));printf("FLAGS_AFTER=%u\n",flags);return 0;}
    if(result!=cudaErrorNotSupported)return 1;
    CHECK(cudaSetDeviceFlags(0)); CHECK(cudaGraphInstantiateWithFlags(&exec,graph,0));
    CHECK(cudaGraphDestroy(graph));
    result=cudaSetDeviceFlags(cudaDeviceScheduleSpin);
    printf("FLAGS_WITH_EXEC=%d expected=%d\n",result,cudaErrorNotSupported);
    if(result!=cudaErrorNotSupported)return 1;
    CHECK(cudaGraphLaunch(exec,NULL)); CHECK(cudaDeviceSynchronize()); CHECK(cudaGraphExecDestroy(exec));
    CHECK(cudaSetDeviceFlags(cudaDeviceScheduleSpin)); CHECK(cudaGetDeviceFlags(&flags));
    if(flags!=cudaDeviceScheduleSpin)return 1;
    CHECK(cudaDeviceReset()); puts("PARTIAL_CONTEXT_FLAGS=PASS");return 0;
}
