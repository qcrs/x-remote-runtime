/* Bounded single-stream native capture, sharing the explicit graph registry. */
static int capture_active_locked(void) {
    return CLIENT_GRAPHS.capture.status != cudaStreamCaptureStatusNone;
}
static cudaError_t capture_reject_locked(void) {
    if (CLIENT_GRAPHS.capture.status == cudaStreamCaptureStatusInvalidated)
        return cudaErrorStreamCaptureInvalidated;
    CLIENT_GRAPHS.capture.status = cudaStreamCaptureStatusInvalidated;
    return cudaErrorStreamCaptureUnsupported;
}
static cudaError_t capture_submit_locked(cudaStream_t stream, uint32_t kind, const void *params) {
    if (CLIENT_GRAPHS.capture.status == cudaStreamCaptureStatusInvalidated)
        return cudaErrorStreamCaptureInvalidated;
    if (!pthread_equal(CLIENT_GRAPHS.capture.owner, pthread_self()))
        return cudaErrorStreamCaptureWrongThread;
    if (stream != CLIENT_GRAPHS.capture.stream)
        return capture_reject_locked();
    struct corexCudaGraphHandle *g = resolve_graph_handle(CLIENT_GRAPHS.capture.graph);
    G6EStreamView sv;
    if (!g || get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    if (CLIENT_GRAPHS.node_next == CRX_GRAPH_MAX_NODE_HANDLES)
        return capture_reject_locked();
    ClientGraphParams p;
    cudaError_t r = graph_params_locked(kind, params, &p);
    if (r == cudaErrorNotSupported)
        return capture_reject_locked();
    if (r) {
        CLIENT_GRAPHS.capture.status = cudaStreamCaptureStatusInvalidated;
        return r;
    }
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_CAPTURE_ADD,
                                              .graph = g->id,
                                              .stream = sv.stream_id,
                                              .kind = kind,
                                              .data_length = p.length};
    CorexGeneratedcudaGraphCreateResponse reply;
    memcpy(q.data, p.wire, p.length);
    r = graph_rpc(&q, &reply);
    if (r) {
        free(p.cache.arg_storage);
        if (CLIENT_GRAPHS.capture.graph)
            CLIENT_GRAPHS.capture.status = cudaStreamCaptureStatusInvalidated;
        return r;
    }
    struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[CLIENT_GRAPHS.node_next++];
    n->live = 1;
    n->generation = g_active_session_generation;
    n->graph_id = g->id;
    n->id = reply.object;
    graph_commit_params(n, &p);
    CLIENT_GRAPHS.capture.tail = n;
    CLIENT_GRAPHS.capture.dependencies[0] = n;
    CLIENT_GRAPHS.capture.dependency_count = 1;
    return cudaSuccess;
}
static cudaError_t capture_begin_locked(cudaStream_t stream, enum cudaStreamCaptureMode mode) {
    if (mode < cudaStreamCaptureModeGlobal || mode > cudaStreamCaptureModeRelaxed)
        return cudaErrorInvalidValue;
    if (mode != cudaStreamCaptureModeThreadLocal)
        return cudaErrorNotSupported;
    if (capture_active_locked())
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    G6EStreamView sv;
    if (!stream)
        return cudaErrorStreamCaptureUnsupported;
    if (get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    if (CLIENT_GRAPHS.graph_next == CRX_GRAPH_MAX_GRAPHS)
        return cudaErrorMemoryAllocation;
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = GRAPH_CAPTURE_BEGIN, .stream = sv.stream_id, .value = mode};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    struct corexCudaGraphHandle *g = &CLIENT_GRAPHS.graphs[CLIENT_GRAPHS.graph_next++];
    *g = (struct corexCudaGraphHandle){
        .id = reply.object, .generation = g_active_session_generation, .live = 1};
    CLIENT_GRAPHS.capture.status = cudaStreamCaptureStatusActive;
    CLIENT_GRAPHS.capture.stream = stream;
    CLIENT_GRAPHS.capture.graph = g;
    CLIENT_GRAPHS.capture.id = ++CLIENT_GRAPHS.capture_next;
    CLIENT_GRAPHS.capture.owner = pthread_self();
    return cudaSuccess;
}
static cudaError_t capture_dependencies_locked(cudaStream_t stream, cudaGraphNode_t *deps,
                                                size_t count, unsigned flags) {
    if (count > CRX_GRAPH_MAX_NODES || (count && !deps) || flags > 1)
        return cudaErrorInvalidValue;
    if (!capture_active_locked() || stream != CLIENT_GRAPHS.capture.stream)
        return cudaErrorInvalidValue;
    if (CLIENT_GRAPHS.capture.status == cudaStreamCaptureStatusInvalidated)
        return cudaErrorStreamCaptureInvalidated;
    if (!pthread_equal(CLIENT_GRAPHS.capture.owner, pthread_self()))
        return cudaErrorStreamCaptureWrongThread;
    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action=GRAPH_CAPTURE_DEPENDENCIES,
        .graph=CLIENT_GRAPHS.capture.graph->id, .stream=sv.stream_id, .value=flags};
    size_t pos=0;
    for (size_t i=0; i<count; ++i) {
        struct corexCudaGraphNodeHandle *n=resolve_graph_node_handle(deps[i]);
        if (!n) return cudaErrorInvalidResourceHandle;
        if (n->graph_id != q.graph) return cudaErrorInvalidValue;
        corex_protocol_write_u64(q.data, &pos, n->id);
    }
    q.data_length=(uint32_t)pos;
    CorexGeneratedcudaGraphCreateResponse reply;
    cudaError_t r=graph_rpc(&q, &reply);
    if (r) return r;
    if (reply.value > CRX_GRAPH_MAX_NODES || reply.data_length != 8*reply.value) {
        mark_session_failed_locked("INVALID_CAPTURE_DEPENDENCIES");
        return cudaErrorUnknown;
    }
    cudaGraphNode_t updated[CRX_GRAPH_MAX_NODES];
    for (size_t i=0; i<reply.value; ++i) {
        updated[i]=graph_node_from_id(corex_protocol_read_u64(reply.data+8*i));
        if (!updated[i] || updated[i]->graph_id != q.graph) {
            mark_session_failed_locked("UNKNOWN_CAPTURE_DEPENDENCY");
            return cudaErrorUnknown;
        }
    }
    memcpy(CLIENT_GRAPHS.capture.dependencies, updated, reply.value*sizeof(*updated));
    CLIENT_GRAPHS.capture.dependency_count=reply.value;
    return cudaSuccess;
}
static cudaError_t capture_begin_to_graph_locked(cudaStream_t stream, cudaGraph_t graph,
        const cudaGraphNode_t *deps, const cudaGraphEdgeData *edge_data, size_t count,
        enum cudaStreamCaptureMode mode) {
    if (mode < cudaStreamCaptureModeGlobal || mode > cudaStreamCaptureModeRelaxed ||
        count > CRX_GRAPH_MAX_NODES || (count && !deps)) return cudaErrorInvalidValue;
    if (mode != cudaStreamCaptureModeThreadLocal) return cudaErrorNotSupported;
    if (capture_active_locked()) return cudaErrorInvalidValue;
    if (edge_data) {
        const unsigned char *bytes=(const unsigned char *)edge_data;
        for (size_t i=0; i<count*sizeof(*edge_data); ++i)
            if (bytes[i]) return cudaErrorNotSupported;
    }
    cudaError_t r=graph_runtime_locked();
    if (r) return r;
    struct corexCudaGraphHandle *g=resolve_graph_handle(graph);
    if (!g) return cudaErrorInvalidResourceHandle;
    G6EStreamView sv;
    if (!stream) return cudaErrorStreamCaptureUnsupported;
    if (get_stream_view(stream, &sv) != 0) return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q={.action=GRAPH_CAPTURE_BEGIN_TO_GRAPH,
        .graph=g->id, .stream=sv.stream_id, .value=mode};
    size_t pos=0;
    for (size_t i=0; i<count; ++i) {
        struct corexCudaGraphNodeHandle *n=resolve_graph_node_handle(deps[i]);
        if (!n) return cudaErrorInvalidResourceHandle;
        if (n->graph_id != g->id) return cudaErrorInvalidValue;
        for (size_t j=0; j<i; ++j) if (deps[i]==deps[j]) return cudaErrorInvalidValue;
        corex_protocol_write_u64(q.data, &pos, n->id);
    }
    q.data_length=(uint32_t)pos;
    CorexGeneratedcudaGraphCreateResponse reply;
    r=graph_rpc(&q,&reply);
    if (r) return r;
    CLIENT_GRAPHS.capture.status=cudaStreamCaptureStatusActive;
    CLIENT_GRAPHS.capture.stream=stream;
    CLIENT_GRAPHS.capture.graph=g;
    CLIENT_GRAPHS.capture.id=++CLIENT_GRAPHS.capture_next;
    CLIENT_GRAPHS.capture.owner=pthread_self();
    CLIENT_GRAPHS.capture.supplied_graph=1;
    CLIENT_GRAPHS.capture.dependency_count=count;
    if (count) memcpy(CLIENT_GRAPHS.capture.dependencies,deps,count*sizeof(*deps));
    return cudaSuccess;
}
static cudaError_t capture_end_locked(cudaStream_t stream, cudaGraph_t *out) {
    if (!out)
        return cudaErrorInvalidValue;
    *out = NULL;
    if (!capture_active_locked() || stream != CLIENT_GRAPHS.capture.stream)
        return cudaErrorInvalidValue;
    if (!pthread_equal(CLIENT_GRAPHS.capture.owner, pthread_self()))
        return cudaErrorStreamCaptureWrongThread;
    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    cudaGraph_t graph = CLIENT_GRAPHS.capture.graph;
    int invalid = CLIENT_GRAPHS.capture.status == cudaStreamCaptureStatusInvalidated;
    int supplied = CLIENT_GRAPHS.capture.supplied_graph;
    CorexGeneratedcudaGraphCreateRequest q = {.action =
                                                  invalid ? GRAPH_CAPTURE_ABORT : GRAPH_CAPTURE_END,
                                              .graph = graph->id,
                                              .stream = sv.stream_id};
    CorexGeneratedcudaGraphCreateResponse reply;
    cudaError_t r = graph_rpc(&q, &reply);
    memset(&CLIENT_GRAPHS.capture, 0, sizeof(CLIENT_GRAPHS.capture));
    if (r || invalid) {
        if (supplied && !r)
            return cudaErrorStreamCaptureInvalidated;
        graph->live = 0;
        for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
            struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
            if (n->generation == g_active_session_generation && n->graph_id == graph->id) {
                n->live = 0;
                free(n->arg_storage);
                n->arg_storage = NULL;
            }
        }
        return r ? r : cudaErrorStreamCaptureInvalidated;
    }
    *out = graph;
    return cudaSuccess;
}
static cudaError_t capture_info_locked(cudaStream_t stream, enum cudaStreamCaptureStatus *status,
                                       unsigned long long *id, cudaGraph_t *graph,
                                       const cudaGraphNode_t **dependencies, size_t *count) {
    if (!status)
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    *status = stream == CLIENT_GRAPHS.capture.stream ? CLIENT_GRAPHS.capture.status
                                                     : cudaStreamCaptureStatusNone;
    if (id)
        *id = *status == cudaStreamCaptureStatusNone ? 0 : CLIENT_GRAPHS.capture.id;
    if (graph)
        *graph = *status == cudaStreamCaptureStatusActive ? CLIENT_GRAPHS.capture.graph : NULL;
    if (dependencies)
        *dependencies = *status == cudaStreamCaptureStatusActive && CLIENT_GRAPHS.capture.dependency_count
                            ? CLIENT_GRAPHS.capture.dependencies
                            : NULL;
    if (count)
        *count = *status == cudaStreamCaptureStatusActive ? CLIENT_GRAPHS.capture.dependency_count : 0;
    return cudaSuccess;
}
static cudaError_t capture_guard_graph_api(const char *name) {
    if (!capture_active_locked())
        return cudaSuccess;
    if (strstr(name, "Get") || strcmp(name, "cudaStreamIsCapturing") == 0 ||
        strcmp(name, "cudaStreamEndCapture") == 0 || strcmp(name, "cudaStreamBeginCapture") == 0 ||
        strcmp(name, "cudaStreamBeginCaptureToGraph") == 0 ||
        strcmp(name, "cudaStreamUpdateCaptureDependencies") == 0 || strstr(name, "cudaGraphAdd"))
        return cudaSuccess;
    return capture_reject_locked();
}
