/* Graph API transactions share the RuntimeContext mutex and the existing
 * allocation, kernel and stream identities. Native handles remain server-side. */
#define CLIENT_GRAPHS (corex_runtime_context_get()->graphs)
static struct corexCudaGraphHandle *resolve_graph_handle(cudaGraph_t h) {
    for (size_t i = 0; i < CLIENT_GRAPHS.graph_next; ++i) {
        struct corexCudaGraphHandle *v = &CLIENT_GRAPHS.graphs[i];
        if (v == h && v->live && v->generation == g_active_session_generation)
            return v;
    }
    return NULL;
}
static struct corexCudaGraphNodeHandle *resolve_graph_node_handle(cudaGraphNode_t h) {
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *v = &CLIENT_GRAPHS.nodes[i];
        if (v == h && v->live && v->generation == g_active_session_generation)
            return v;
    }
    return NULL;
}
static struct corexCudaGraphExecHandle *resolve_graph_exec_handle(cudaGraphExec_t h) {
    for (size_t i = 0; i < CLIENT_GRAPHS.exec_next; ++i) {
        struct corexCudaGraphExecHandle *v = &CLIENT_GRAPHS.execs[i];
        if (v == h && v->live && v->generation == g_active_session_generation)
            return v;
    }
    return NULL;
}
static void invalidate_client_graphs(void) {
    memset(&CLIENT_GRAPHS.capture, 0, sizeof(CLIENT_GRAPHS.capture));
    for (size_t i = 0; i < CLIENT_GRAPHS.graph_next; ++i)
        CLIENT_GRAPHS.graphs[i].live = 0;
    for (size_t i = 0; i < CLIENT_GRAPHS.exec_next; ++i) {
        CLIENT_GRAPHS.execs[i].live = 0;
        free(CLIENT_GRAPHS.execs[i].frontier);
        CLIENT_GRAPHS.execs[i].frontier = NULL;
    }
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        CLIENT_GRAPHS.nodes[i].live = 0;
        free(CLIENT_GRAPHS.nodes[i].arg_storage);
        CLIENT_GRAPHS.nodes[i].arg_storage = NULL;
    }
}
static cudaError_t graph_runtime_locked(void) {
    cudaError_t r = ensure_runtime();
    if (r != cudaSuccess)
        return r;
    if (g_hello_state == COREX_HELLO_NEGOTIATED)
        for (uint32_t i = 0; i < g_hello.capability_count; ++i)
            if (g_hello.capabilities[i] == CRX_CAP_GRAPHS)
                return cudaSuccess;
    return cudaErrorNotSupported;
}
static cudaError_t graph_rpc(CorexGeneratedcudaGraphCreateRequest *q,
                             CorexGeneratedcudaGraphCreateResponse *r) {
    if (corex_generated_call_cudaGraphCreate(generated_rpc_call, q, r) != 0) {
        if (!g_last_rpc_transport_error && g_last_remote_status == ST_OK)
            mark_session_failed_locked("MALFORMED_GRAPH_RESPONSE");
        return map_last_rpc_error(cudaErrorUnknown);
    }
    if (((q->action == GRAPH_CREATE || q->action == GRAPH_ADD_NODE ||
          q->action == GRAPH_INSTANTIATE || q->action == GRAPH_CLONE ||
          q->action == GRAPH_FIND_CLONE || q->action == GRAPH_CAPTURE_BEGIN ||
          q->action == GRAPH_CAPTURE_ADD) &&
         !r->object) ||
        (q->action == GRAPH_GET_ENABLED && r->value > 1) ||
        (q->action == GRAPH_EXEC_UPDATE && r->value > cudaGraphExecUpdateErrorNotSupported)) {
        mark_session_failed_locked("INVALID_GRAPH_RESPONSE_ID_OR_ENUM");
        return cudaErrorUnknown;
    }
    return cudaSuccess;
}
static cudaError_t graph_create_locked(cudaGraph_t *out, unsigned int flags) {
    if (!out || flags)
        return cudaErrorInvalidValue;
    *out = NULL;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    if (CLIENT_GRAPHS.graph_next == CRX_GRAPH_MAX_GRAPHS)
        return cudaErrorMemoryAllocation;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_CREATE};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    struct corexCudaGraphHandle *h = &CLIENT_GRAPHS.graphs[CLIENT_GRAPHS.graph_next++];
    *h = (struct corexCudaGraphHandle){
        .id = reply.object, .generation = g_active_session_generation, .live = 1};
    *out = h;
    return cudaSuccess;
}
static cudaError_t graph_destroy_locked(cudaGraph_t h) {
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(h);
    if (!g)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_DESTROY, .graph = g->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    g->live = 0;
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
        if (n->generation == g_active_session_generation && n->graph_id == g->id) {
            n->live = 0;
            free(n->arg_storage);
            n->arg_storage = NULL;
        }
    }
    return cudaSuccess;
}
typedef struct {
    unsigned char wire[CRX_GRAPH_PARAM_BYTES];
    uint32_t length;
    struct corexCudaGraphNodeHandle cache;
} ClientGraphParams;
static cudaError_t graph_params_locked(uint32_t kind, const void *params, ClientGraphParams *out) {
    memset(out, 0, sizeof(*out));
    out->cache.kind = kind;
    if (kind == cudaGraphNodeTypeEmpty)
        return cudaSuccess;
    if (!params)
        return cudaErrorInvalidValue;
    if (kind == cudaGraphNodeTypeEventRecord || kind == cudaGraphNodeTypeWaitEvent) {
        cudaEvent_t event = *(const cudaEvent_t *)params;
        struct corexCudaEventHandle *e = resolve_event_handle(event);
        if (!e)
            return cudaErrorInvalidResourceHandle;
        size_t pos = 0;
        corex_protocol_write_u64(out->wire, &pos, e->event_id);
        out->length = (uint32_t)pos;
        out->cache.event = event;
        return cudaSuccess;
    }
    if (kind == cudaGraphNodeTypeMemset) {
        const struct cudaMemsetParams *p = params;
        if ((p->elementSize != 1 && p->elementSize != 2 && p->elementSize != 4) || !p->width ||
            !p->height || p->width > SIZE_MAX / p->elementSize)
            return cudaErrorInvalidValue;
        size_t row = p->width * p->elementSize, pitch = p->height == 1 ? row : p->pitch;
        if (pitch < row || p->height - 1 > (SIZE_MAX - row) / pitch)
            return cudaErrorInvalidValue;
        ResolvedRemotePtr dst;
        ResolveResult rr = resolve_remote_ptr(p->dst, (p->height - 1) * pitch + row, &dst);
        if (rr != RESOLVE_OK)
            return map_resolve_error(rr);
        if ((uintptr_t)p->dst % p->elementSize)
            return cudaErrorInvalidValue;
        size_t pos = 0;
        corex_protocol_write_u64(out->wire, &pos, dst.allocation_id);
        corex_protocol_write_u64(out->wire, &pos, dst.byte_offset);
        corex_protocol_write_u64(out->wire, &pos, p->pitch);
        corex_protocol_write_u64(out->wire, &pos, p->width);
        corex_protocol_write_u64(out->wire, &pos, p->height);
        corex_protocol_write_u32(out->wire, &pos, p->value);
        corex_protocol_write_u32(out->wire, &pos, p->elementSize);
        out->length = (uint32_t)pos;
        out->cache.fill = *p;
        return cudaSuccess;
    }
    if (kind == cudaGraphNodeTypeMemcpy) {
        const cudaMemcpy3DParms *p = params;
        if (p->srcArray || p->dstArray)
            return cudaErrorNotSupported;
        if (p->kind != cudaMemcpyDeviceToDevice && p->kind != cudaMemcpyDefault)
            return cudaErrorNotSupported;
        if (p->kind == cudaMemcpyDefault &&
            (!pointer_in_remote_arena(p->srcPtr.ptr) || !pointer_in_remote_arena(p->dstPtr.ptr)))
            return cudaErrorNotSupported;
        if (!p->srcPtr.pitch || !p->dstPtr.pitch || !p->extent.width || !p->extent.height ||
            !p->extent.depth || p->srcPtr.ysize > SIZE_MAX / p->srcPtr.pitch ||
            p->dstPtr.ysize > SIZE_MAX / p->dstPtr.pitch)
            return cudaErrorInvalidValue;
        size_t ss = p->srcPtr.pitch * p->srcPtr.ysize, ds = p->dstPtr.pitch * p->dstPtr.ysize;
        ResolvedRemotePtr dst, src;
        ResolveResult rr = resolve_remote_3d_region(p->dstPtr.ptr, p->dstPos, p->dstPtr.pitch, ds,
                                                    p->extent, &dst);
        if (rr != RESOLVE_OK)
            return map_resolve_error(rr);
        rr = resolve_remote_3d_region(p->srcPtr.ptr, p->srcPos, p->srcPtr.pitch, ss, p->extent,
                                      &src);
        if (rr != RESOLVE_OK)
            return map_resolve_error(rr);
        uint64_t v[] = {dst.allocation_id, dst.byte_offset,  p->dstPtr.pitch, ds,
                        src.allocation_id, src.byte_offset,  p->srcPtr.pitch, ss,
                        p->extent.width,   p->extent.height, p->extent.depth};
        size_t pos = 0;
        for (size_t i = 0; i < 11; ++i)
            corex_protocol_write_u64(out->wire, &pos, v[i]);
        out->length = (uint32_t)pos;
        out->cache.copy = *p;
        return cudaSuccess;
    }
    if (kind == cudaGraphNodeTypeKernel) {
        const struct cudaKernelNodeParams *p = params;
        if (p->extra)
            return cudaErrorNotSupported;
        unsigned char *wire = NULL;
        uint32_t length = 0;
        cudaError_t r = serialize_kernel_launch(p->func, p->gridDim, p->blockDim, p->kernelParams,
                                                p->sharedMemBytes, 0, &wire, &length);
        if (r)
            return r;
        if (length > sizeof(out->wire)) {
            free(wire);
            return cudaErrorNotSupported;
        }
        memcpy(out->wire, wire, length);
        free(wire);
        out->length = length;
        corexRemoteKernelHandle *k = resolve_kernel_reference(p->func);
        size_t bytes = 0;
        for (size_t i = 0; i < k->argc; ++i)
            bytes += (k->args[i].size + 15u) & ~15u;
        out->cache.arg_storage = malloc(bytes ? bytes : 1);
        if (!out->cache.arg_storage)
            return cudaErrorMemoryAllocation;
        out->cache.kernel = *p;
        out->cache.arg_storage_bytes = bytes;
        size_t offset = 0;
        for (size_t i = 0; i < k->argc; ++i) {
            out->cache.kernel_args[i] = out->cache.arg_storage + offset;
            memcpy(out->cache.kernel_args[i], p->kernelParams[i], k->args[i].size);
            offset += (k->args[i].size + 15u) & ~15u;
        }
        return cudaSuccess;
    }
    return cudaErrorNotSupported;
}
static void graph_commit_params(struct corexCudaGraphNodeHandle *n, ClientGraphParams *p) {
    free(n->arg_storage);
    n->arg_storage = p->cache.arg_storage;
    n->arg_storage_bytes = p->cache.arg_storage_bytes;
    n->event = p->cache.event;
    n->kind = p->cache.kind;
    n->fill = p->cache.fill;
    n->copy = p->cache.copy;
    n->kernel = p->cache.kernel;
    memcpy(n->kernel_args, p->cache.kernel_args, sizeof(n->kernel_args));
    if (n->kind == cudaGraphNodeTypeKernel)
        n->kernel.kernelParams = n->kernel_args;
    p->cache.arg_storage = NULL;
}
static cudaError_t graph_add_node_locked(cudaGraphNode_t *out, cudaGraph_t h,
                                         const cudaGraphNode_t *deps, size_t count, uint32_t kind,
                                         const void *params) {
    if (!out || count > CRX_GRAPH_MAX_NODES || (count && !deps))
        return cudaErrorInvalidValue;
    *out = NULL;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(h);
    if (!g)
        return cudaErrorInvalidResourceHandle;
    if (CLIENT_GRAPHS.node_next == CRX_GRAPH_MAX_NODE_HANDLES)
        return cudaErrorMemoryAllocation;
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = GRAPH_ADD_NODE, .graph = g->id, .kind = kind};
    CorexGeneratedcudaGraphCreateResponse reply;
    size_t pos = 0;
    corex_protocol_write_u32(q.data, &pos, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) {
        struct corexCudaGraphNodeHandle *d = resolve_graph_node_handle(deps[i]);
        if (!d)
            return cudaErrorInvalidResourceHandle;
        if (d->graph_id != g->id)
            return cudaErrorInvalidValue;
        for (size_t j = 0; j < i; ++j)
            if (deps[j] == deps[i])
                return cudaErrorInvalidValue;
        corex_protocol_write_u64(q.data, &pos, d->id);
    }
    ClientGraphParams p;
    r = graph_params_locked(kind, params, &p);
    if (r)
        return r;
    memcpy(q.data + pos, p.wire, p.length);
    q.data_length = (uint32_t)pos + p.length;
    r = graph_rpc(&q, &reply);
    if (r) {
        free(p.cache.arg_storage);
        return r;
    }
    struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[CLIENT_GRAPHS.node_next++];
    n->live = 1;
    n->generation = g_active_session_generation;
    n->graph_id = g->id;
    n->id = reply.object;
    graph_commit_params(n, &p);
    *out = n;
    return cudaSuccess;
}
static cudaError_t graph_set_node_locked(cudaGraphExec_t exec, cudaGraphNode_t node, uint32_t kind,
                                         const void *params) {
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(node);
    if (!n)
        return cudaErrorInvalidResourceHandle;
    if (n->kind != kind)
        return cudaErrorInvalidValue;
    struct corexCudaGraphExecHandle *e = exec ? resolve_graph_exec_handle(exec) : NULL;
    if (exec && !e)
        return cudaErrorInvalidResourceHandle;
    size_t event_index = 0;
    if (e && (kind == cudaGraphNodeTypeEventRecord || kind == cudaGraphNodeTypeWaitEvent)) {
        for (; event_index < e->event_count; ++event_index)
            if (e->event_nodes[event_index] == n->id)
                break;
        if (event_index == e->event_count)
            return cudaErrorInvalidValue;
    }
    ClientGraphParams p;
    r = graph_params_locked(kind, params, &p);
    if (r)
        return r;
    CorexGeneratedcudaGraphCreateRequest q = {.action = exec ? GRAPH_EXEC_SET_NODE : GRAPH_SET_NODE,
                                              .node = n->id,
                                              .exec = e ? e->id : 0,
                                              .kind = kind,
                                              .data_length = p.length};
    CorexGeneratedcudaGraphCreateResponse reply;
    memcpy(q.data, p.wire, p.length);
    r = graph_rpc(&q, &reply);
    if (!r && !exec)
        graph_commit_params(n, &p);
    if (!r && e && (kind == cudaGraphNodeTypeEventRecord || kind == cudaGraphNodeTypeWaitEvent))
        e->events[event_index] = p.cache.event;
    free(p.cache.arg_storage);
    return r;
}
static cudaError_t graph_get_params_locked(cudaGraphNode_t node, uint32_t kind, void *params) {
    if (!params)
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(node);
    if (!n)
        return cudaErrorInvalidResourceHandle;
    if (n->kind != kind)
        return cudaErrorInvalidValue;
    if (kind == cudaGraphNodeTypeMemset)
        *(struct cudaMemsetParams *)params = n->fill;
    else if (kind == cudaGraphNodeTypeMemcpy)
        *(cudaMemcpy3DParms *)params = n->copy;
    else if (kind == cudaGraphNodeTypeKernel)
        *(struct cudaKernelNodeParams *)params = n->kernel;
    else if (kind == cudaGraphNodeTypeEventRecord || kind == cudaGraphNodeTypeWaitEvent)
        *(cudaEvent_t *)params = n->event;
    return cudaSuccess;
}
static cudaError_t graph_instantiate_locked(cudaGraphExec_t *out, cudaGraph_t h,
                                            unsigned long long flags, cudaGraphNode_t *error,
                                            char *log, size_t logsize) {
    if (!out || (logsize && !log))
        return cudaErrorInvalidValue;
    *out = NULL;
    if (error)
        *error = NULL;
    if (logsize)
        *log = 0;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(h);
    if (!g)
        return cudaErrorInvalidResourceHandle;
    if (flags)
        return cudaErrorNotSupported;
    if (CLIENT_GRAPHS.exec_next == CRX_GRAPH_MAX_EXECS)
        return cudaErrorMemoryAllocation;
    uint64_t *frontier = calloc(G6E_FRONTIER_SLOTS, sizeof(uint64_t));
    if (!frontier)
        return cudaErrorMemoryAllocation;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_INSTANTIATE, .graph = g->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r) {
        free(frontier);
        return r;
    }
    struct corexCudaGraphExecHandle *e = &CLIENT_GRAPHS.execs[CLIENT_GRAPHS.exec_next++];
    *e = (struct corexCudaGraphExecHandle){.id = reply.object,
                                           .generation = g_active_session_generation,
                                           .live = 1,
                                           .frontier = frontier};
    e->source_graph_id = g->id;
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
        if (n->live && n->generation == g_active_session_generation && n->graph_id == g->id &&
            (n->kind == cudaGraphNodeTypeEventRecord || n->kind == cudaGraphNodeTypeWaitEvent)) {
            e->events[e->event_count] = n->event;
            e->event_nodes[e->event_count] = n->id;
            e->event_kinds[e->event_count++] = n->kind;
        }
    }
    *out = e;
    return cudaSuccess;
}
static cudaError_t graph_exec_destroy_locked(cudaGraphExec_t h) {
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphExecHandle *e = resolve_graph_exec_handle(h);
    if (!e)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_EXEC_DESTROY, .exec = e->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (!r) {
        e->live = 0;
        free(e->frontier);
        e->frontier = NULL;
    }
    return r;
}
static cudaError_t graph_launch_locked(cudaGraphExec_t h, cudaStream_t stream) {
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphExecHandle *e = resolve_graph_exec_handle(h);
    if (!e)
        return cudaErrorInvalidResourceHandle;
    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        return cudaErrorInvalidResourceHandle;
    for (size_t i = 0; i < e->event_count; ++i)
        if (!resolve_event_handle(e->events[i]))
            return cudaErrorInvalidResourceHandle;
    apply_legacy_default_ordering(&sv);
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = GRAPH_LAUNCH, .exec = e->id, .stream = sv.stream_id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    /* Native serializes launches of the same executable, even across streams.
     * Propagate that ordering to existing hidden-transfer host retirement. */
    for (size_t i = 0; i < G6E_FRONTIER_SLOTS; ++i)
        if (sv.frontier[i] < e->frontier[i])
            sv.frontier[i] = e->frontier[i];
    for (size_t i = 0; i < e->event_count; ++i) {
        struct corexCudaEventHandle *v = resolve_event_handle(e->events[i]);
        if (e->event_kinds[i] == cudaGraphNodeTypeWaitEvent && v->recorded)
            frontier_merge(sv.frontier, v->frontier);
    }
    sv.frontier[sv.slot_index] = ++(*sv.next_transfer_seq);
    memcpy(e->frontier, sv.frontier, G6E_FRONTIER_SLOTS * sizeof(uint64_t));
    for (size_t i = 0; i < e->event_count; ++i) {
        struct corexCudaEventHandle *v = resolve_event_handle(e->events[i]);
        if (e->event_kinds[i] == cudaGraphNodeTypeEventRecord) {
            v->recorded = 1;
            memcpy(v->frontier, sv.frontier, G6E_FRONTIER_SLOTS * sizeof(uint64_t));
        }
    }
    return cudaSuccess;
}
static cudaGraphNode_t graph_node_from_id(uint64_t id) {
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
        if (n->live && n->generation == g_active_session_generation && n->id == id)
            return n;
    }
    return NULL;
}
static cudaError_t graph_inspect_locked(uint32_t action, cudaGraph_t graph, cudaGraphNode_t node,
                                        cudaGraphNode_t *first, cudaGraphNode_t *second,
                                        size_t *count) {
    if (!count || (action == GRAPH_GET_EDGES && ((first == NULL) != (second == NULL))))
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = graph ? resolve_graph_handle(graph) : NULL;
    struct corexCudaGraphNodeHandle *n = node ? resolve_graph_node_handle(node) : NULL;
    if ((graph && !g) || (node && !n) || (!g && !n))
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = action, .graph = g ? g->id : 0, .node = n ? n->id : 0};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    size_t stride = action == GRAPH_GET_EDGES ? 16 : 8;
    if (reply.data_length != reply.value * stride)
        return cudaErrorUnknown;
    size_t size = *count, actual = reply.value, copied = size < actual ? size : actual;
    if (first) {
        for (size_t i = 0; i < copied; ++i) {
            first[i] = graph_node_from_id(corex_protocol_read_u64(reply.data + stride * i));
            if (!first[i])
                return cudaErrorUnknown;
            if (second) {
                second[i] =
                    graph_node_from_id(corex_protocol_read_u64(reply.data + stride * i + 8));
                if (!second[i])
                    return cudaErrorUnknown;
            }
        }
        for (size_t i = copied; i < size; ++i) {
            first[i] = NULL;
            if (second)
                second[i] = NULL;
        }
    }
    *count = actual;
    return cudaSuccess;
}
static cudaError_t graph_dependencies_locked(uint32_t action, cudaGraph_t h,
                                             const cudaGraphNode_t *from, const cudaGraphNode_t *to,
                                             size_t count) {
    if (count > CRX_GRAPH_MAX_NODES || (count && (!from || !to)))
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(h);
    if (!g)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action = action, .graph = g->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        struct corexCudaGraphNodeHandle *f = resolve_graph_node_handle(from[i]),
                                        *t = resolve_graph_node_handle(to[i]);
        if (!f || !t)
            return cudaErrorInvalidResourceHandle;
        if (f->graph_id != g->id || t->graph_id != g->id)
            return cudaErrorInvalidValue;
        corex_protocol_write_u64(q.data, &pos, f->id);
        corex_protocol_write_u64(q.data, &pos, t->id);
    }
    q.data_length = (uint32_t)pos;
    return graph_rpc(&q, &reply);
}
static cudaError_t graph_destroy_node_locked(cudaGraphNode_t node) {
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(node);
    if (!n)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_DESTROY_NODE, .node = n->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (!r) {
        n->live = 0;
        free(n->arg_storage);
        n->arg_storage = NULL;
    }
    return r;
}
static cudaError_t graph_node_type_locked(cudaGraphNode_t node, enum cudaGraphNodeType *type) {
    if (!type)
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(node);
    if (!n)
        return cudaErrorInvalidResourceHandle;
    *type = (enum cudaGraphNodeType)n->kind;
    return cudaSuccess;
}
static cudaError_t graph_node_enabled_locked(cudaGraphExec_t exec, cudaGraphNode_t node,
                                             unsigned *enabled, int set) {
    if (!enabled || (set && *enabled > 1))
        return cudaErrorInvalidValue;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(node);
    struct corexCudaGraphExecHandle *e = resolve_graph_exec_handle(exec);
    if (!n || !e)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {.action = set ? GRAPH_SET_ENABLED : GRAPH_GET_ENABLED,
                                              .node = n->id,
                                              .exec = e->id,
                                              .value = set ? *enabled : 0};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (!r && !set)
        *enabled = reply.value;
    return r;
}

#include "corex_capture_runtime.inc"

static cudaError_t graph_clone_locked(cudaGraph_t *out, cudaGraph_t original) {
    if (!out)
        return cudaErrorInvalidValue;
    *out = NULL;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(original);
    if (!g)
        return cudaErrorInvalidResourceHandle;
    size_t count = 0;
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
        if (n->live && n->generation == g_active_session_generation && n->graph_id == g->id)
            ++count;
    }
    if (CLIENT_GRAPHS.graph_next == CRX_GRAPH_MAX_GRAPHS ||
        count > CRX_GRAPH_MAX_NODE_HANDLES - CLIENT_GRAPHS.node_next)
        return cudaErrorMemoryAllocation;
    CorexGeneratedcudaGraphCreateRequest q = {.action = GRAPH_CLONE, .graph = g->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    if (reply.value != count || reply.data_length != 16 * count)
        return cudaErrorUnknown;
    struct corexCudaGraphHandle *clone = &CLIENT_GRAPHS.graphs[CLIENT_GRAPHS.graph_next++];
    *clone = (struct corexCudaGraphHandle){
        .id = reply.object, .generation = g_active_session_generation, .live = 1};
    for (size_t i = 0; i < count; ++i) {
        struct corexCudaGraphNodeHandle *src =
            graph_node_from_id(corex_protocol_read_u64(reply.data + 16 * i));
        if (!src) {
            graph_destroy_locked(clone);
            return cudaErrorUnknown;
        }
        unsigned char *storage = NULL;
        if (src->arg_storage) {
            storage = malloc(src->arg_storage_bytes ? src->arg_storage_bytes : 1);
            if (!storage) {
                graph_destroy_locked(clone);
                return cudaErrorMemoryAllocation;
            }
            memcpy(storage, src->arg_storage, src->arg_storage_bytes);
        }
        struct corexCudaGraphNodeHandle *dst = &CLIENT_GRAPHS.nodes[CLIENT_GRAPHS.node_next++];
        *dst = *src;
        dst->id = corex_protocol_read_u64(reply.data + 16 * i + 8);
        dst->graph_id = clone->id;
        dst->original_id = src->id;
        dst->arg_storage = storage;
        if (src->kind == cudaGraphNodeTypeKernel) {
            for (size_t j = 0; j < 32; ++j)
                if (src->kernel_args[j])
                    dst->kernel_args[j] =
                        storage + ((unsigned char *)src->kernel_args[j] - src->arg_storage);
            dst->kernel.kernelParams = dst->kernel_args;
        }
    }
    *out = clone;
    return cudaSuccess;
}
static cudaError_t graph_find_clone_locked(cudaGraphNode_t *out, cudaGraphNode_t original,
                                           cudaGraph_t clone) {
    if (!out)
        return cudaErrorInvalidValue;
    *out = NULL;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(clone);
    struct corexCudaGraphNodeHandle *n = resolve_graph_node_handle(original);
    if (!g || !n)
        return cudaErrorInvalidResourceHandle;
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = GRAPH_FIND_CLONE, .graph = g->id, .node = n->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    *out = graph_node_from_id(reply.object);
    return *out ? cudaSuccess : cudaErrorUnknown;
}
static cudaError_t graph_exec_update_locked(cudaGraphExec_t exec, cudaGraph_t graph,
                                            cudaGraphNode_t *error,
                                            enum cudaGraphExecUpdateResult *result) {
    if (!result)
        return cudaErrorInvalidValue;
    *result = cudaGraphExecUpdateError;
    if (error)
        *error = NULL;
    cudaError_t r = graph_runtime_locked();
    if (r)
        return r;
    struct corexCudaGraphHandle *g = resolve_graph_handle(graph);
    struct corexCudaGraphExecHandle *e = resolve_graph_exec_handle(exec);
    if (!g || !e)
        return cudaErrorInvalidResourceHandle;
    if (g->id != e->source_graph_id) {
        *result = cudaGraphExecUpdateErrorNotSupported;
        return cudaErrorNotSupported;
    }
    CorexGeneratedcudaGraphCreateRequest q = {
        .action = GRAPH_EXEC_UPDATE, .graph = g->id, .exec = e->id};
    CorexGeneratedcudaGraphCreateResponse reply;
    r = graph_rpc(&q, &reply);
    if (r)
        return r;
    *result = (enum cudaGraphExecUpdateResult)reply.value;
    if (error && reply.object)
        *error = graph_node_from_id(reply.object);
    if (reply.value != cudaGraphExecUpdateSuccess)
        return cudaErrorGraphExecUpdateFailure;
    e->event_count = 0;
    for (size_t i = 0; i < CLIENT_GRAPHS.node_next; ++i) {
        struct corexCudaGraphNodeHandle *n = &CLIENT_GRAPHS.nodes[i];
        if (n->live && n->generation == g_active_session_generation && n->graph_id == g->id &&
            (n->kind == cudaGraphNodeTypeEventRecord || n->kind == cudaGraphNodeTypeWaitEvent)) {
            e->events[e->event_count] = n->event;
            e->event_nodes[e->event_count] = n->id;
            e->event_kinds[e->event_count++] = n->kind;
        }
    }
    return cudaSuccess;
}

/* Repetitive exported mutex boundaries, with semantic bodies above. */
static cudaError_t graph_mem_attribute_locked(int device, enum cudaGraphMemAttributeType attr,
                                              void *value, int set) {
    if (!value || attr<0 || attr>3) return cudaErrorInvalidValue;
    if (device != 0) return cudaErrorInvalidDevice;
    uint64_t scalar=0;
    if (set) {
        memcpy(&scalar,value,sizeof(scalar));
        if ((attr!=1 && attr!=3) || scalar) return cudaErrorInvalidValue;
    }
    cudaError_t r=graph_runtime_locked();
    if(r) return r;
    if(set) {
        CorexGeneratedcudaDeviceSetGraphMemAttributeRequest q={.device=device,.attribute=attr,.value=scalar};
        CorexGeneratedcudaDeviceSetGraphMemAttributeResponse reply;
        if(corex_generated_call_cudaDeviceSetGraphMemAttribute(generated_rpc_call,&q,&reply))
            return map_last_rpc_error(cudaErrorUnknown);
    } else {
        CorexGeneratedcudaDeviceGetGraphMemAttributeRequest q={.device=device,.attribute=attr};
        CorexGeneratedcudaDeviceGetGraphMemAttributeResponse reply;
        if(corex_generated_call_cudaDeviceGetGraphMemAttribute(generated_rpc_call,&q,&reply))
            return map_last_rpc_error(cudaErrorUnknown);
        memcpy(value,&reply.value,sizeof(reply.value));
    }
    return cudaSuccess;
}
static cudaError_t graph_mem_trim_locked(int device) {
    if(device != 0) return cudaErrorInvalidDevice;
    cudaError_t r=graph_runtime_locked();
    if(r) return r;
    CorexGeneratedcudaDeviceGraphMemTrimRequest q={.device=device};
    CorexGeneratedcudaDeviceGraphMemTrimResponse reply;
    if(corex_generated_call_cudaDeviceGraphMemTrim(generated_rpc_call,&q,&reply))
        return map_last_rpc_error(cudaErrorUnknown);
    return cudaSuccess;
}
#define GRAPH_API(name, signature, body)                                                           \
    cudaError_t name signature {                                                                   \
        CorexRuntimeContext *context = corex_runtime_context_get();                                \
        corex_runtime_context_lock(context);                                                       \
        cudaError_t graph_api_error = capture_guard_graph_api(#name);                              \
        if (graph_api_error == cudaSuccess)                                                        \
            graph_api_error = (body);                                                              \
        corex_runtime_context_unlock(context);                                                     \
        return record_error(graph_api_error);                                                      \
    }
GRAPH_API(cudaGraphCreate, (cudaGraph_t * out, unsigned flags), graph_create_locked(out, flags))
GRAPH_API(cudaDeviceGetGraphMemAttribute, (int device, enum cudaGraphMemAttributeType attr, void *value),
          graph_mem_attribute_locked(device,attr,value,0))
GRAPH_API(cudaDeviceSetGraphMemAttribute, (int device, enum cudaGraphMemAttributeType attr, void *value),
          graph_mem_attribute_locked(device,attr,value,1))
GRAPH_API(cudaDeviceGraphMemTrim, (int device), graph_mem_trim_locked(device))
GRAPH_API(cudaGraphDestroy, (cudaGraph_t graph), graph_destroy_locked(graph))
GRAPH_API(cudaGraphAddEmptyNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeEmpty, NULL))
GRAPH_API(cudaGraphAddKernelNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count,
           const struct cudaKernelNodeParams *params),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeKernel, params))
GRAPH_API(cudaGraphAddMemcpyNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count,
           const cudaMemcpy3DParms *params),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeMemcpy, params))
GRAPH_API(cudaGraphAddMemsetNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count,
           const struct cudaMemsetParams *params),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeMemset, params))
GRAPH_API(cudaGraphKernelNodeSetParams,
          (cudaGraphNode_t node, const struct cudaKernelNodeParams *params),
          graph_set_node_locked(NULL, node, cudaGraphNodeTypeKernel, params))
GRAPH_API(cudaGraphMemcpyNodeSetParams, (cudaGraphNode_t node, const cudaMemcpy3DParms *params),
          graph_set_node_locked(NULL, node, cudaGraphNodeTypeMemcpy, params))
GRAPH_API(cudaGraphMemsetNodeSetParams,
          (cudaGraphNode_t node, const struct cudaMemsetParams *params),
          graph_set_node_locked(NULL, node, cudaGraphNodeTypeMemset, params))
GRAPH_API(cudaGraphExecKernelNodeSetParams,
          (cudaGraphExec_t exec, cudaGraphNode_t node, const struct cudaKernelNodeParams *params),
          graph_set_node_locked(exec, node, cudaGraphNodeTypeKernel, params))
GRAPH_API(cudaGraphExecMemcpyNodeSetParams,
          (cudaGraphExec_t exec, cudaGraphNode_t node, const cudaMemcpy3DParms *params),
          graph_set_node_locked(exec, node, cudaGraphNodeTypeMemcpy, params))
GRAPH_API(cudaGraphExecMemsetNodeSetParams,
          (cudaGraphExec_t exec, cudaGraphNode_t node, const struct cudaMemsetParams *params),
          graph_set_node_locked(exec, node, cudaGraphNodeTypeMemset, params))
GRAPH_API(cudaGraphKernelNodeGetParams, (cudaGraphNode_t node, struct cudaKernelNodeParams *params),
          graph_get_params_locked(node, cudaGraphNodeTypeKernel, params))
GRAPH_API(cudaGraphMemcpyNodeGetParams, (cudaGraphNode_t node, cudaMemcpy3DParms *params),
          graph_get_params_locked(node, cudaGraphNodeTypeMemcpy, params))
GRAPH_API(cudaGraphMemsetNodeGetParams, (cudaGraphNode_t node, struct cudaMemsetParams *params),
          graph_get_params_locked(node, cudaGraphNodeTypeMemset, params))
GRAPH_API(cudaGraphInstantiate,
          (cudaGraphExec_t * out, cudaGraph_t graph, cudaGraphNode_t *error, char *log,
           size_t logsize),
          graph_instantiate_locked(out, graph, 0, error, log, logsize))
GRAPH_API(cudaGraphInstantiateWithFlags,
          (cudaGraphExec_t * out, cudaGraph_t graph, unsigned long long flags),
          graph_instantiate_locked(out, graph, flags, NULL, NULL, 0))
GRAPH_API(cudaGraphExecDestroy, (cudaGraphExec_t exec), graph_exec_destroy_locked(exec))
GRAPH_API(cudaGraphLaunch, (cudaGraphExec_t exec, cudaStream_t stream),
          graph_launch_locked(exec, stream))
GRAPH_API(cudaGraphGetNodes, (cudaGraph_t graph, cudaGraphNode_t *nodes, size_t *count),
          graph_inspect_locked(GRAPH_GET_NODES, graph, NULL, nodes, NULL, count))
GRAPH_API(cudaGraphGetRootNodes, (cudaGraph_t graph, cudaGraphNode_t *nodes, size_t *count),
          graph_inspect_locked(GRAPH_GET_ROOTS, graph, NULL, nodes, NULL, count))
GRAPH_API(cudaGraphGetEdges,
          (cudaGraph_t graph, cudaGraphNode_t *from, cudaGraphNode_t *to, size_t *count),
          graph_inspect_locked(GRAPH_GET_EDGES, graph, NULL, from, to, count))
GRAPH_API(cudaGraphNodeGetDependencies,
          (cudaGraphNode_t node, cudaGraphNode_t *deps, size_t *count),
          graph_inspect_locked(GRAPH_GET_DEPENDENCIES, NULL, node, deps, NULL, count))
GRAPH_API(cudaGraphNodeGetDependentNodes,
          (cudaGraphNode_t node, cudaGraphNode_t *deps, size_t *count),
          graph_inspect_locked(GRAPH_GET_DEPENDENTS, NULL, node, deps, NULL, count))
GRAPH_API(cudaGraphAddDependencies,
          (cudaGraph_t graph, const cudaGraphNode_t *from, const cudaGraphNode_t *to, size_t count),
          graph_dependencies_locked(GRAPH_ADD_DEPENDENCIES, graph, from, to, count))
GRAPH_API(cudaGraphRemoveDependencies,
          (cudaGraph_t graph, const cudaGraphNode_t *from, const cudaGraphNode_t *to, size_t count),
          graph_dependencies_locked(GRAPH_REMOVE_DEPENDENCIES, graph, from, to, count))
GRAPH_API(cudaGraphDestroyNode, (cudaGraphNode_t node), graph_destroy_node_locked(node))
GRAPH_API(cudaGraphNodeGetType, (cudaGraphNode_t node, enum cudaGraphNodeType *type),
          graph_node_type_locked(node, type))
GRAPH_API(cudaGraphNodeSetEnabled, (cudaGraphExec_t exec, cudaGraphNode_t node, unsigned enabled),
          graph_node_enabled_locked(exec, node, &enabled, 1))
GRAPH_API(cudaGraphNodeGetEnabled, (cudaGraphExec_t exec, cudaGraphNode_t node, unsigned *enabled),
          graph_node_enabled_locked(exec, node, enabled, 0))
GRAPH_API(cudaStreamBeginCapture, (cudaStream_t stream, enum cudaStreamCaptureMode mode),
          capture_begin_locked(stream, mode))
GRAPH_API(cudaStreamBeginCaptureToGraph,
          (cudaStream_t stream, cudaGraph_t graph, const cudaGraphNode_t *deps,
           const cudaGraphEdgeData *edge_data, size_t count, enum cudaStreamCaptureMode mode),
          capture_begin_to_graph_locked(stream, graph, deps, edge_data, count, mode))
GRAPH_API(cudaStreamUpdateCaptureDependencies,
          (cudaStream_t stream, cudaGraphNode_t *deps, size_t count, unsigned flags),
          capture_dependencies_locked(stream, deps, count, flags))
GRAPH_API(cudaStreamEndCapture, (cudaStream_t stream, cudaGraph_t *graph),
          capture_end_locked(stream, graph))
GRAPH_API(cudaStreamIsCapturing, (cudaStream_t stream, enum cudaStreamCaptureStatus *status),
          capture_info_locked(stream, status, NULL, NULL, NULL, NULL))
GRAPH_API(cudaStreamGetCaptureInfo,
          (cudaStream_t stream, enum cudaStreamCaptureStatus *status, unsigned long long *id),
          capture_info_locked(stream, status, id, NULL, NULL, NULL))
GRAPH_API(cudaStreamGetCaptureInfo_v2,
          (cudaStream_t stream, enum cudaStreamCaptureStatus *status, unsigned long long *id,
           cudaGraph_t *graph, const cudaGraphNode_t **dependencies, size_t *count),
          capture_info_locked(stream, status, id, graph, dependencies, count))
GRAPH_API(cudaGraphClone, (cudaGraph_t * out, cudaGraph_t original),
          graph_clone_locked(out, original))
GRAPH_API(cudaGraphNodeFindInClone,
          (cudaGraphNode_t * out, cudaGraphNode_t original, cudaGraph_t clone),
          graph_find_clone_locked(out, original, clone))
GRAPH_API(cudaGraphExecUpdate,
          (cudaGraphExec_t exec, cudaGraph_t graph, cudaGraphNode_t *error,
           enum cudaGraphExecUpdateResult *result),
          graph_exec_update_locked(exec, graph, error, result))
GRAPH_API(cudaGraphAddEventRecordNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count,
           cudaEvent_t event),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeEventRecord, &event))
GRAPH_API(cudaGraphAddEventWaitNode,
          (cudaGraphNode_t * out, cudaGraph_t graph, const cudaGraphNode_t *deps, size_t count,
           cudaEvent_t event),
          graph_add_node_locked(out, graph, deps, count, cudaGraphNodeTypeWaitEvent, &event))
GRAPH_API(cudaGraphEventRecordNodeGetEvent, (cudaGraphNode_t node, cudaEvent_t *event),
          graph_get_params_locked(node, cudaGraphNodeTypeEventRecord, event))
GRAPH_API(cudaGraphEventWaitNodeGetEvent, (cudaGraphNode_t node, cudaEvent_t *event),
          graph_get_params_locked(node, cudaGraphNodeTypeWaitEvent, event))
GRAPH_API(cudaGraphEventRecordNodeSetEvent, (cudaGraphNode_t node, cudaEvent_t event),
          graph_set_node_locked(NULL, node, cudaGraphNodeTypeEventRecord, &event))
GRAPH_API(cudaGraphEventWaitNodeSetEvent, (cudaGraphNode_t node, cudaEvent_t event),
          graph_set_node_locked(NULL, node, cudaGraphNodeTypeWaitEvent, &event))
GRAPH_API(cudaGraphExecEventRecordNodeSetEvent,
          (cudaGraphExec_t exec, cudaGraphNode_t node, cudaEvent_t event),
          graph_set_node_locked(exec, node, cudaGraphNodeTypeEventRecord, &event))
GRAPH_API(cudaGraphExecEventWaitNodeSetEvent,
          (cudaGraphExec_t exec, cudaGraphNode_t node, cudaEvent_t event),
          graph_set_node_locked(exec, node, cudaGraphNodeTypeWaitEvent, &event))
#undef GRAPH_API
