#ifndef COREX_GRAPH_OBJECTS_H
#define COREX_GRAPH_OBJECTS_H
#include "corex_graph_protocol.h"
typedef struct {
    int used;
    uint64_t id;
    CUgraph graph;
} GraphEntry;
typedef struct {
    int used;
    uint64_t id, graph_id, original_id;
    CUgraphNode node;
    uint32_t kind, length;
    unsigned char *params;
} GraphNodeEntry;
typedef struct {
    int used;
    uint64_t id;
    CUgraphExec exec;
    size_t count;
    /* Executable snapshots survive destruction and mutation of source graphs. */
    GraphNodeEntry nodes[CRX_GRAPH_MAX_NODES];
} GraphExecEntry;
#endif
