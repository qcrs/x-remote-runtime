#include <cuda_runtime_api.h>
#include <stdio.h>
#define OBS(call) do {cudaError_t r=(call);printf("%s=%d\n",#call,(int)r);fflush(stdout);}while(0)
int main(void) {
    cudaGraph_t graph=NULL;cudaGraphNode_t node=NULL;cudaGraphExec_t exec=NULL;cudaStream_t stream=NULL;
    if(cudaGraphCreate(&graph,0)||cudaGraphAddEmptyNode(&node,graph,NULL,0)||
       cudaGraphInstantiate(&exec,graph,NULL,NULL,0)||cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking))return 1;
    OBS(cudaGraphUpload(exec,stream));OBS(cudaStreamSynchronize(stream));
    OBS(cudaGraphLaunch(exec,stream));OBS(cudaStreamSynchronize(stream));
    OBS(cudaGraphExecDestroy(exec));OBS(cudaGraphDestroy(graph));OBS(cudaStreamDestroy(stream));return 0;
}
