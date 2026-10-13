#ifndef COREX_GRAPH_CLIENT_H
#define COREX_GRAPH_CLIENT_H
#include "corex_graph_protocol.h"
#include <pthread.h>
struct corexCudaGraphHandle {
    uint64_t id, generation;
    int live;
};
struct corexCudaGraphExecHandle {
    uint64_t id, generation;
    int live;
    uint64_t *frontier;
    uint64_t source_graph_id;
    size_t event_count;
    cudaEvent_t events[CRX_GRAPH_MAX_NODES];
    uint64_t event_nodes[CRX_GRAPH_MAX_NODES];
    uint32_t event_kinds[CRX_GRAPH_MAX_NODES];
};
struct corexCudaGraphNodeHandle {
    uint64_t id, generation, graph_id;
    int live;
    uint32_t kind;
    struct cudaMemsetParams fill;
    cudaMemcpy3DParms copy;
    struct cudaKernelNodeParams kernel;
    void *kernel_args[32];
    unsigned char *arg_storage;
    size_t arg_storage_bytes;
    cudaEvent_t event;
    uint64_t original_id;
};
typedef struct {
    struct corexCudaGraphHandle graphs[CRX_GRAPH_MAX_GRAPHS];
    struct corexCudaGraphExecHandle execs[CRX_GRAPH_MAX_EXECS];
    struct corexCudaGraphNodeHandle nodes[CRX_GRAPH_MAX_NODE_HANDLES];
    size_t graph_next, exec_next, node_next;
    struct {
        enum cudaStreamCaptureStatus status;
        cudaStream_t stream;
        cudaGraph_t graph;
        cudaGraphNode_t tail;
        cudaGraphNode_t dependencies[CRX_GRAPH_MAX_NODES];
        size_t dependency_count;
        int supplied_graph;
        uint64_t id;
        pthread_t owner;
    } capture;
    uint64_t capture_next;

} CorexClientGraphs;
#endif
