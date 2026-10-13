#ifndef COREX_GRAPH_OBJECTS_H
#define COREX_GRAPH_OBJECTS_H
#include "corex_graph_protocol.h"
typedef struct {
    int used;
    uint64_t id;
    CUgraph graph;
    uint64_t owner_node_id; /* borrowed native child; parent node owns destruction */
} GraphEntry;
typedef struct {
    int used;
    uint64_t id, graph_id, original_id;
    CUgraphNode node;
    uint32_t kind, length;
    unsigned enabled;
    unsigned char *params;
} GraphNodeEntry;
typedef struct {
    int used;
    uint64_t id;
    CUgraphExec exec;
    CUgraph definition; /* owned native definition for rebuilt executable nodes */
    uint64_t last_stream;
    int launched;
    size_t count;
    /* Executable snapshots survive destruction and mutation of source graphs. */
    GraphNodeEntry nodes[CRX_GRAPH_MAX_NODES];
    unsigned char edges[CRX_GRAPH_MAX_NODES][CRX_GRAPH_MAX_NODES];
} GraphExecEntry;
#endif
