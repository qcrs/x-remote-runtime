#ifndef COREX_GRAPH_PROTOCOL_H
#define COREX_GRAPH_PROTOCOL_H
#include "corex_protocol.h"

/* Generated envelope codecs surround this handwritten graph command model.
 * Only IDs, bounded network-order fields and by-value kernel bytes cross CRX9. */
#define CRX_GRAPH_MAX_NODES 64u
#define CRX_GRAPH_MAX_GRAPHS 128u
#define CRX_GRAPH_MAX_EXECS 128u
#define CRX_GRAPH_MAX_NODE_HANDLES 4096u
#define CRX_GRAPH_PARAM_BYTES 4096u
enum CorexGraphAction {
    GRAPH_CREATE=1, GRAPH_DESTROY, GRAPH_ADD_NODE, GRAPH_SET_NODE,
    GRAPH_EXEC_SET_NODE, GRAPH_GET_PARAMS, GRAPH_DESTROY_NODE,
    GRAPH_INSTANTIATE, GRAPH_EXEC_DESTROY, GRAPH_LAUNCH,
    GRAPH_GET_NODES, GRAPH_GET_ROOTS, GRAPH_GET_EDGES,
    GRAPH_GET_DEPENDENCIES, GRAPH_GET_DEPENDENTS, GRAPH_GET_TYPE,
    GRAPH_ADD_DEPENDENCIES, GRAPH_REMOVE_DEPENDENCIES,
    GRAPH_SET_ENABLED, GRAPH_GET_ENABLED,
    GRAPH_CLONE, GRAPH_FIND_CLONE, GRAPH_EXEC_UPDATE,
    GRAPH_CAPTURE_BEGIN, GRAPH_CAPTURE_ADD, GRAPH_CAPTURE_END, GRAPH_CAPTURE_ABORT,
    GRAPH_CAPTURE_BEGIN_TO_GRAPH, GRAPH_CAPTURE_DEPENDENCIES,
};
/* Query/control RPCs outside this allowlist must not execute while a native
 * capture is active. This guard also protects against a direct CRX9 peer. */
static inline int corex_graph_capture_safe_opcode(uint32_t opcode)
{
    switch (opcode) {
    case OP_GRAPH: case OP_CLOSE: case OP_HELLO:
    case OP_GET_DEVICE_INFO: case OP_GET_DRIVER_VERSION: case OP_GET_RUNTIME_VERSION:
    case OP_DEVICE_GET_ATTRIBUTE: case OP_GET_DEVICE_FLAGS: case OP_GET_PRIORITY_RANGE:
    case OP_GET_LIMIT: case OP_GET_CACHE_CONFIG: case OP_GET_SHARED_MEM_CONFIG:
    case OP_DEVICE_GET_PCI_BUS_ID: case OP_DEVICE_GET_BY_PCI_BUS_ID:
    case OP_FUNCTION_ATTRIBUTES: case OP_OCCUPANCY: case OP_STREAM_GET_FLAGS:
    case OP_STREAM_GET_PRIORITY:
        return 1;
    default:
        return 0;
    }
}
/* Node params: memcpy = dst AllocationID/offset, src AllocationID/offset,
 * bytes (5 u64); memset = AllocationID/offset/pitch/width/height (5 u64),
 * value/elementSize (2 u32); kernel reuses OP_LAUNCH_GENERIC's checked DTO;
 * event = EventID (u64). ADD_NODE prefixes dependency count:u32 + NodeIDs.
 * Inspection returns bounded NodeID arrays or pairs of NodeID edge endpoints. */
#endif
