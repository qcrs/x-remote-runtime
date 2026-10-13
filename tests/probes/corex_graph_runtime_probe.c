#include <cuda_runtime_api.h>
#include <stdio.h>
static int failures;
#define SHOW(call)                                                                                 \
    do {                                                                                           \
        cudaError_t r = (call);                                                                    \
        if (r != cudaSuccess)                                                                      \
            ++failures;                                                                            \
        printf("%s=%d\n", #call, (int)r);                                                          \
        fflush(stdout);                                                                            \
    } while (0)
int main(void) {
    cudaGraph_t graph = NULL, captured = NULL;
    cudaGraphNode_t node = NULL;
    cudaGraphExec_t exec = NULL;
    cudaStream_t stream = NULL;
    SHOW(cudaGraphCreate(&graph, 0));
    printf("GRAPH_PRESENT=%d\n", graph != NULL);
    if (graph) {
        SHOW(cudaGraphAddEmptyNode(&node, graph, NULL, 0));
        size_t count = 0;
        SHOW(cudaGraphGetNodes(graph, NULL, &count));
        printf("NODE_COUNT=%zu\n", count);
        SHOW(cudaGraphInstantiate(&exec, graph, NULL, NULL, 0));
        printf("EXEC_PRESENT=%d\n", exec != NULL);
        if (exec) {
            SHOW(cudaGraphLaunch(exec, 0));
            SHOW(cudaDeviceSynchronize());
            SHOW(cudaGraphExecDestroy(exec));
        }
        SHOW(cudaGraphDestroy(graph));
    }
    SHOW(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    if (stream) {
        cudaError_t begin = cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal);
        printf("cudaStreamBeginCapture=%d\n", begin);
        enum cudaStreamCaptureStatus status = 99;
        SHOW(cudaStreamIsCapturing(stream, &status));
        printf("CAPTURE_STATUS=%d\n", status);
        if (begin == cudaSuccess) {
            SHOW(cudaStreamEndCapture(stream, &captured));
            if (captured)
                SHOW(cudaGraphDestroy(captured));
        }
        SHOW(cudaStreamDestroy(stream));
    }
    return failures ? 1 : 0;
}
