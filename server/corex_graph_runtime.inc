/* Handwritten native graph ownership and node policy. Included after the
 * common checked kernel decoder; no vendor calls belong in this file. */
static GraphEntry *find_graph(ServerSession *s, uint64_t id) {
  for (size_t i = 0; i < CRX_GRAPH_MAX_GRAPHS; ++i)
    if (s->graphs[i].used && s->graphs[i].id == id)
      return &s->graphs[i];
  return NULL;
}
static GraphNodeEntry *find_graph_node(ServerSession *s, uint64_t id) {
  for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
    if (s->graph_nodes[i].used && s->graph_nodes[i].id == id)
      return &s->graph_nodes[i];
  return NULL;
}
static GraphExecEntry *find_graph_exec(ServerSession *s, uint64_t id) {
  for (size_t i = 0; i < CRX_GRAPH_MAX_EXECS; ++i)
    if (s->graph_execs[i].used && s->graph_execs[i].id == id)
      return &s->graph_execs[i];
  return NULL;
}
static GraphNodeEntry *exec_graph_node(GraphExecEntry *e, uint64_t id) {
  for (size_t i = 0; i < e->count; ++i)
    if (e->nodes[i].id == id)
      return &e->nodes[i];
  return NULL;
}
static void release_graph_node(GraphNodeEntry *n) {
  free(n->params);
  memset(n, 0, sizeof(*n));
}
static void cleanup_graphs(ServerSession *s) {
  if (s->capture_graph) {
    StreamEntry *stream = find_stream(s, s->capture_stream);
    GraphEntry *graph = find_graph(s, s->capture_graph);
    CUgraph captured = NULL;
    if (stream)
      corex_backend_capture_end(stream->stream, &captured);
    /* A failed EndCapture discards the invalidated native graph. */
    if (!captured && graph) {
      for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
        if (s->graph_nodes[i].used && s->graph_nodes[i].graph_id == graph->id)
          release_graph_node(&s->graph_nodes[i]);
      memset(graph, 0, sizeof(*graph));
    }
    s->capture_graph = s->capture_stream = 0;
  }
  /* Native graph launches may use default/nonblocking streams. Complete
   * them before releasing executable snapshots and their resource owners. */
  corex_backend_context_sync();
  for (size_t i = 0; i < CRX_GRAPH_MAX_EXECS; ++i) {
    GraphExecEntry *e = &s->graph_execs[i];
    if (!e->used)
      continue;
    corex_backend_graph_exec_destroy(e->exec);
    for (size_t j = 0; j < e->count; ++j)
      release_graph_node(&e->nodes[j]);
    memset(e, 0, sizeof(*e));
  }
  for (size_t i = 0; i < CRX_GRAPH_MAX_GRAPHS; ++i) {
    if (s->graphs[i].used)
      corex_backend_graph_destroy(s->graphs[i].graph);
    memset(&s->graphs[i], 0, sizeof(s->graphs[i]));
  }
  for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
    release_graph_node(&s->graph_nodes[i]);
}
static uint32_t graph_result(CUresult r) {
  if (r == CUDA_SUCCESS)
    return ST_OK;
  if (r == CUDA_ERROR_INVALID_VALUE)
    return ST_BAD_REQUEST;
  if (r == CUDA_ERROR_NOT_SUPPORTED)
    return ST_NOT_SUPPORTED;
  return ST_CUDA_ERROR;
}
typedef struct {
  CUDA_MEMCPY3D copy;
  CUDA_MEMSET_NODE_PARAMS fill;
  DecodedKernelLaunch kernel;
  CUevent event;
} GraphNativeParams;
static uint32_t decode_graph_params(ServerSession *s, uint32_t kind,
                                    const unsigned char *p, uint32_t len,
                                    GraphNativeParams *d) {
  memset(d, 0, sizeof(*d));
  if (kind == CU_GRAPH_NODE_TYPE_EVENT_RECORD ||
      kind == CU_GRAPH_NODE_TYPE_WAIT_EVENT) {
    if (len != 8)
      return ST_BAD_REQUEST;
    EventEntry *e = find_event(s, corex_protocol_read_u64(p));
    if (!e)
      return ST_NOT_FOUND;
    d->event = e->event;
    return ST_OK;
  }
  if (kind == CU_GRAPH_NODE_TYPE_EMPTY)
    return len == 0 ? ST_OK : ST_BAD_REQUEST;
  if (kind == CU_GRAPH_NODE_TYPE_KERNEL) {
    if (len < 48 || corex_protocol_read_u64(p + 8) != 0)
      return ST_BAD_REQUEST;
    return decode_kernel_launch(s, p, len, &d->kernel);
  }
  if (kind == CU_GRAPH_NODE_TYPE_MEMCPY) {
    if (len != 88)
      return ST_BAD_REQUEST;
    uint64_t v[11];
    for (size_t i = 0; i < 11; ++i)
      v[i] = corex_protocol_read_u64(p + 8 * i);
    Allocation *dst = find_allocation(s, v[0]), *src = find_allocation(s, v[4]);
    if (!dst || !src)
      return ST_NOT_FOUND;
    if (!v[8] || !v[9] || !v[10] ||
        !valid_3d_allocation(dst, v[1], v[2], v[3], v[8], v[9], v[10]) ||
        !valid_3d_allocation(src, v[5], v[6], v[7], v[8], v[9], v[10]))
      return ST_BAD_REQUEST;
    d->copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    d->copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    d->copy.srcDevice = src->ptr + v[5];
    d->copy.dstDevice = dst->ptr + v[1];
    d->copy.srcPitch = v[6];
    d->copy.srcHeight = v[7] / v[6];
    d->copy.dstPitch = v[2];
    d->copy.dstHeight = v[3] / v[2];
    d->copy.WidthInBytes = v[8];
    d->copy.Height = v[9];
    d->copy.Depth = v[10];
    return ST_OK;
  }

  if (kind == CU_GRAPH_NODE_TYPE_MEMSET) {
    if (len != 48)
      return ST_BAD_REQUEST;
    Allocation *a = find_allocation(s, corex_protocol_read_u64(p));
    if (!a)
      return ST_NOT_FOUND;
    uint64_t off = corex_protocol_read_u64(p + 8),
             pitch = corex_protocol_read_u64(p + 16),
             width = corex_protocol_read_u64(p + 24),
             height = corex_protocol_read_u64(p + 32);
    uint32_t value = corex_protocol_read_u32(p + 40),
             element = corex_protocol_read_u32(p + 44);
    if ((element != 1 && element != 2 && element != 4) || !width || !height ||
        width > UINT64_MAX / element)
      return ST_BAD_REQUEST;
    uint64_t row = width * element;
    if (height == 1)
      pitch = row;
    if (pitch < row || height - 1 > (UINT64_MAX - row) / pitch)
      return ST_BAD_REQUEST;
    uint64_t bytes = (height - 1) * pitch + row;
    if (off > a->size || bytes > a->size - off || (a->ptr + off) % element)
      return ST_BAD_REQUEST;
    d->fill.dst = a->ptr + off;
    d->fill.pitch = pitch;
    d->fill.width = width;
    d->fill.height = height;
    d->fill.value = value;
    d->fill.elementSize = element;
    return ST_OK;
  }
  return ST_NOT_SUPPORTED;
}
static void release_graph_params(uint32_t kind, GraphNativeParams *p) {
  if (kind == CU_GRAPH_NODE_TYPE_KERNEL)
    release_raw_kernel_args(p->kernel.raw, p->kernel.argc);
}
static uint64_t native_graph_node_id(ServerSession *s, CUgraphNode native) {
  for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
    if (s->graph_nodes[i].used && s->graph_nodes[i].node == native)
      return s->graph_nodes[i].id;
  return 0;
}
static uint32_t resolve_graph_dependencies(ServerSession *s, uint64_t graph,
                                           const unsigned char *p, size_t count,
                                           CUgraphNode *out) {
  if (count > CRX_GRAPH_MAX_NODES)
    return ST_BAD_REQUEST;
  for (size_t i = 0; i < count; ++i) {
    GraphNodeEntry *n = find_graph_node(s, corex_protocol_read_u64(p + 8 * i));
    if (!n)
      return ST_NOT_FOUND;
    if (n->graph_id != graph)
      return ST_BAD_REQUEST;
    for (size_t j = 0; j < i; ++j)
      if (out[j] == n->node)
        return ST_BAD_REQUEST;
    out[i] = n->node;
  }
  return ST_OK;
}
static uint32_t validate_graph_edge_change(CUgraph graph, CUgraphNode *from,
                                           CUgraphNode *to, size_t count,
                                           int add) {
  CUgraphNode nodes[CRX_GRAPH_MAX_NODES], old_from[4032], old_to[4032];
  size_t n = CRX_GRAPH_MAX_NODES, edges = 4032;
  CUresult r = corex_backend_graph_nodes(graph, nodes, &n);
  if (r)
    return graph_result(r);
  r = corex_backend_graph_edges(graph, old_from, old_to, &edges);
  if (r)
    return graph_result(r);
  if (n > CRX_GRAPH_MAX_NODES || edges > 4032)
    return ST_INTERNAL;
  unsigned char adjacent[CRX_GRAPH_MAX_NODES][CRX_GRAPH_MAX_NODES] = {{0}};
  for (size_t i = 0; i < edges + count; ++i) {
    CUgraphNode f = i < edges ? old_from[i] : from[i - edges],
                t = i < edges ? old_to[i] : to[i - edges];
    size_t fi = n, ti = n;
    for (size_t j = 0; j < n; ++j) {
      if (nodes[j] == f)
        fi = j;
      if (nodes[j] == t)
        ti = j;
    }
    if (fi == n || ti == n)
      return ST_BAD_REQUEST;
    if (i < edges)
      adjacent[fi][ti] = 1;
    else {
      if (fi == ti || adjacent[fi][ti] == add)
        return ST_BAD_REQUEST;
      adjacent[fi][ti] = (unsigned char)add;
    }
  }
  if (add) {
    unsigned incoming[CRX_GRAPH_MAX_NODES] = {0};
    unsigned char retired[CRX_GRAPH_MAX_NODES] = {0};
    for (size_t i = 0; i < n; ++i)
      for (size_t j = 0; j < n; ++j)
        incoming[j] += adjacent[i][j];
    for (size_t step = 0; step < n; ++step) {
      size_t root = n;
      for (size_t i = 0; i < n; ++i)
        if (!retired[i] && incoming[i] == 0) {
          root = i;
          break;
        }
      if (root == n)
        return ST_BAD_REQUEST;
      retired[root] = 1;
      for (size_t i = 0; i < n; ++i)
        incoming[i] -= adjacent[root][i];
    }
  }
  return ST_OK;
}
static int handle_graph(ServerSession *s, int fd, uint32_t req_id,
                        const unsigned char *payload, uint32_t len) {
  CorexGeneratedcudaGraphCreateRequest q;
  CorexGeneratedcudaGraphCreateResponse out = {0};
  uint32_t status = ST_OK;
  CUresult r = CUDA_SUCCESS;
  if (corex_generated_decode_cudaGraphCreate_request(payload, len, &q) != 0)
    return send_response(fd, OP_GRAPH, req_id, ST_BAD_REQUEST, NULL, 0);
  GraphEntry *g = find_graph(s, q.graph);
  GraphNodeEntry *n = find_graph_node(s, q.node);
  GraphExecEntry *e = find_graph_exec(s, q.exec);
  if (s->capture_graph && q.action != GRAPH_CAPTURE_ADD &&
      q.action != GRAPH_CAPTURE_END && q.action != GRAPH_CAPTURE_ABORT &&
      q.action != GRAPH_GET_NODES && q.action != GRAPH_GET_ROOTS &&
      q.action != GRAPH_GET_EDGES && q.action != GRAPH_GET_DEPENDENCIES &&
      q.action != GRAPH_GET_DEPENDENTS && q.action != GRAPH_GET_TYPE &&
      q.action != GRAPH_GET_PARAMS)
    return send_response(fd, OP_GRAPH, req_id, ST_NOT_SUPPORTED, NULL, 0);
  switch (q.action) {
  case GRAPH_CLONE: {
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    GraphEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_GRAPHS; ++i)
      if (!s->graphs[i].used) {
        slot = &s->graphs[i];
        break;
      }
    if (!slot) {
      status = ST_NO_RESOURCE;
      break;
    }
    size_t needed = 0, free_nodes = 0;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      if (!s->graph_nodes[i].used)
        ++free_nodes;
      else if (s->graph_nodes[i].graph_id == g->id)
        ++needed;
    }
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      GraphNodeEntry *src = &s->graph_nodes[i];
      if (!src->used || src->graph_id != g->id)
        continue;
      GraphNativeParams params;
      status =
          decode_graph_params(s, src->kind, src->params, src->length, &params);
      if (status)
        break;
      release_graph_params(src->kind, &params);
    }
    if (status)
      break;
    if (free_nodes < needed) {
      status = ST_NO_RESOURCE;
      break;
    }
    r = corex_backend_graph_clone(&slot->graph, g->graph);
    if (r)
      break;
    slot->id = ++s->next_graph_id;
    size_t pos = 0;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      GraphNodeEntry *src = &s->graph_nodes[i];
      if (!src->used || src->graph_id != g->id)
        continue;
      GraphNodeEntry *dst = NULL;
      for (size_t j = 0; j < CRX_GRAPH_MAX_NODE_HANDLES; ++j)
        if (!s->graph_nodes[j].used) {
          dst = &s->graph_nodes[j];
          break;
        }
      unsigned char *params = malloc(src->length ? src->length : 1);
      if (!params) {
        status = ST_NO_RESOURCE;
        break;
      }
      CUgraphNode native = NULL;
      r = corex_backend_graph_find_clone(&native, src->node, slot->graph);
      if (r) {
        free(params);
        break;
      }
      *dst = *src;
      memcpy(params, src->params, src->length);
      dst->params = params;
      dst->graph_id = slot->id;
      dst->id = ++s->next_graph_node_id;
      dst->original_id = src->id;
      dst->node = native;
      corex_protocol_write_u64(out.data, &pos, src->id);
      corex_protocol_write_u64(out.data, &pos, dst->id);
    }
    if (status || r) {
      corex_backend_graph_destroy(slot->graph);
      for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
        if (s->graph_nodes[i].used && s->graph_nodes[i].graph_id == slot->id)
          release_graph_node(&s->graph_nodes[i]);
      memset(slot, 0, sizeof(*slot));
      break;
    }
    slot->used = 1;
    out.object = slot->id;
    out.data_length = (uint32_t)pos;
    out.value = (uint32_t)needed;
    break;
  }
  case GRAPH_FIND_CLONE:
    if (!g || !n) {
      status = ST_NOT_FOUND;
      break;
    }
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      GraphNodeEntry *v = &s->graph_nodes[i];
      if (v->used && v->graph_id == g->id && v->original_id == n->id) {
        out.object = v->id;
        break;
      }
    }
    if (!out.object)
      status = ST_BAD_REQUEST;
    break;
  case GRAPH_EXEC_UPDATE: {
    if (!g || !e) {
      status = ST_NOT_FOUND;
      break;
    }
    /* This measured slice accepts the original graph's node identities.
     * Recapture/clone correspondence is deliberately outside this update. */
    for (size_t i = 0; i < e->count; ++i) {
      GraphNodeEntry *v = find_graph_node(s, e->nodes[i].id);
      if (!v || v->graph_id != g->id) {
        status = ST_NOT_SUPPORTED;
        break;
      }
    }
    if (status)
      break;
    GraphNodeEntry snapshots[CRX_GRAPH_MAX_NODES] = {0};
    size_t count = 0;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      GraphNodeEntry *v = &s->graph_nodes[i];
      if (!v->used || v->graph_id != g->id)
        continue;
      GraphNativeParams d;
      status = decode_graph_params(s, v->kind, v->params, v->length, &d);
      if (status)
        break;
      release_graph_params(v->kind, &d);
      GraphNodeEntry *dst = &snapshots[count++];
      *dst = *v;
      dst->params = malloc(v->length ? v->length : 1);
      if (!dst->params) {
        status = ST_NO_RESOURCE;
        break;
      }
      memcpy(dst->params, v->params, v->length);
    }
    CUgraphNode error = NULL;
    CUgraphExecUpdateResult result = CU_GRAPH_EXEC_UPDATE_ERROR;
    if (!status)
      r = corex_backend_graph_exec_update(e->exec, g->graph, &error, &result);
    printf("GRAPH_EXEC_UPDATE rc=%d result=%d\n", (int)r, (int)result);
    if (!status && !r && result == CU_GRAPH_EXEC_UPDATE_SUCCESS) {
      for (size_t i = 0; i < e->count; ++i)
        release_graph_node(&e->nodes[i]);
      memcpy(e->nodes, snapshots, count * sizeof(*snapshots));
      e->count = count;
    } else
      for (size_t i = 0; i < count; ++i)
        release_graph_node(&snapshots[i]);
    out.value = (uint32_t)result;
    out.object = error ? native_graph_node_id(s, error) : 0;
    /* CoreX 4.4 returns 1000 (rather than Driver 910) for the measured
     * topology rejection, while reporting the correct update result. */
    if (r == CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE ||
        ((int)r == 1000 &&
         result == CU_GRAPH_EXEC_UPDATE_ERROR_TOPOLOGY_CHANGED))
      r = CUDA_SUCCESS;
    break;
  }
  case GRAPH_CAPTURE_BEGIN: {
    StreamEntry *stream = find_stream(s, q.stream);
    if (!stream) {
      status = ST_NOT_FOUND;
      break;
    }
    if (q.value != CU_STREAM_CAPTURE_MODE_THREAD_LOCAL) {
      status = ST_NOT_SUPPORTED;
      break;
    }
    GraphEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_GRAPHS; ++i)
      if (!s->graphs[i].used) {
        slot = &s->graphs[i];
        break;
      }
    if (!slot) {
      status = ST_NO_RESOURCE;
      break;
    }
    r = corex_backend_capture_begin(stream->stream,
                                    CU_STREAM_CAPTURE_MODE_THREAD_LOCAL);
    if (r)
      break;
    CUstreamCaptureStatus capture_status;
    cuuint64_t id;
    r = corex_backend_capture_info(stream->stream, &capture_status, &id,
                                   &slot->graph, NULL, NULL);
    if (r || capture_status != CU_STREAM_CAPTURE_STATUS_ACTIVE ||
        !slot->graph) {
      CUgraph abandoned = NULL;
      corex_backend_capture_end(stream->stream, &abandoned);
      if (abandoned)
        corex_backend_graph_destroy(abandoned);
      memset(slot, 0, sizeof(*slot));
      status = ST_CUDA_ERROR;
      break;
    }
    slot->used = 1;
    slot->id = ++s->next_graph_id;
    s->capture_stream = q.stream;
    s->capture_graph = slot->id;
    out.object = slot->id;
    break;
  }
  case GRAPH_CAPTURE_ADD: {
    if (!g || g->id != s->capture_graph || q.stream != s->capture_stream) {
      status = ST_BAD_REQUEST;
      break;
    }
    StreamEntry *stream = find_stream(s, q.stream);
    if (!stream) {
      status = ST_NOT_FOUND;
      break;
    }
    size_t count = 0;
    r = corex_backend_graph_nodes(g->graph, NULL, &count);
    if (r)
      break;
    if (count >= CRX_GRAPH_MAX_NODES) {
      status = ST_NO_RESOURCE;
      break;
    }
    GraphNodeEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
      if (!s->graph_nodes[i].used) {
        slot = &s->graph_nodes[i];
        break;
      }
    if (!slot || q.data_length > CRX_GRAPH_PARAM_BYTES) {
      status = ST_NO_RESOURCE;
      break;
    }
    GraphNativeParams d;
    status = decode_graph_params(s, q.kind, q.data, q.data_length, &d);
    if (status)
      break;
    unsigned char *saved = malloc(q.data_length ? q.data_length : 1);
    if (!saved) {
      release_graph_params(q.kind, &d);
      status = ST_NO_RESOURCE;
      break;
    }
    memcpy(saved, q.data, q.data_length);
    if (q.kind == CU_GRAPH_NODE_TYPE_KERNEL)
      r = corex_backend_launch(
          d.kernel.native.func, d.kernel.native.gridDimX,
          d.kernel.native.gridDimY, d.kernel.native.gridDimZ,
          d.kernel.native.blockDimX, d.kernel.native.blockDimY,
          d.kernel.native.blockDimZ, d.kernel.native.sharedMemBytes,
          stream->stream, d.kernel.params, NULL);
    else if (q.kind == CU_GRAPH_NODE_TYPE_MEMCPY)
      r = corex_backend_copy_3d_async(&d.copy, stream->stream);
    else if (q.kind == CU_GRAPH_NODE_TYPE_MEMSET && d.fill.elementSize == 1) {
      if (d.fill.height == 1)
        r = corex_backend_memset_d8_async(d.fill.dst,
                                          (unsigned char)d.fill.value,
                                          d.fill.width, stream->stream);
      else
        r = corex_backend_memset_d2d8_async(
            d.fill.dst, d.fill.pitch, (unsigned char)d.fill.value, d.fill.width,
            d.fill.height, stream->stream);
    } else
      r = CUDA_ERROR_NOT_SUPPORTED;
    release_graph_params(q.kind, &d);
    if (r) {
      free(saved);
      break;
    }
    CUstreamCaptureStatus capture_status;
    cuuint64_t id;
    CUgraph native_graph;
    const CUgraphNode *deps = NULL;
    size_t deps_count = 0;
    r = corex_backend_capture_info(stream->stream, &capture_status, &id,
                                   &native_graph, &deps, &deps_count);
    if (r || capture_status != CU_STREAM_CAPTURE_STATUS_ACTIVE ||
        native_graph != g->graph || deps_count != 1 || !deps) {
      free(saved);
      status = ST_CUDA_ERROR;
      break;
    }
    slot->used = 1;
    slot->id = ++s->next_graph_node_id;
    slot->graph_id = g->id;
    slot->node = deps[0];
    slot->kind = q.kind;
    slot->params = saved;
    slot->length = q.data_length;
    out.object = slot->id;
    break;
  }
  case GRAPH_CAPTURE_END:
  case GRAPH_CAPTURE_ABORT: {
    if (!g || g->id != s->capture_graph || q.stream != s->capture_stream) {
      status = ST_BAD_REQUEST;
      break;
    }
    StreamEntry *stream = find_stream(s, q.stream);
    CUgraph native_graph = NULL;
    if (!stream) {
      status = ST_NOT_FOUND;
      break;
    }
    r = corex_backend_capture_end(stream->stream, &native_graph);
    s->capture_graph = s->capture_stream = 0;
    if (q.action == GRAPH_CAPTURE_END && !r && native_graph == g->graph) {
      size_t count = 0, tracked = 0;
      r = corex_backend_graph_nodes(g->graph, NULL, &count);
      for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
        if (s->graph_nodes[i].used && s->graph_nodes[i].graph_id == g->id)
          ++tracked;
      if (!r && count == tracked) {
        out.object = g->id;
        break;
      }
      status = ST_CUDA_ERROR;
    }
    if (native_graph)
      corex_backend_graph_destroy(native_graph);
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
      if (s->graph_nodes[i].used && s->graph_nodes[i].graph_id == g->id)
        release_graph_node(&s->graph_nodes[i]);
    memset(g, 0, sizeof(*g));
    break;
  }
  case GRAPH_CREATE: {
    if (q.value || q.data_length) {
      status = ST_BAD_REQUEST;
      break;
    }
    GraphEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_GRAPHS; ++i)
      if (!s->graphs[i].used) {
        slot = &s->graphs[i];
        break;
      }
    if (!slot) {
      status = ST_NO_RESOURCE;
      break;
    }
    r = corex_backend_graph_create(&slot->graph, 0);
    if (r)
      break;
    slot->used = 1;
    slot->id = ++s->next_graph_id;
    out.object = slot->id;
    break;
  }
  case GRAPH_DESTROY:
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    r = corex_backend_graph_destroy(g->graph);
    if (r)
      break;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
      if (s->graph_nodes[i].used && s->graph_nodes[i].graph_id == g->id)
        release_graph_node(&s->graph_nodes[i]);
    memset(g, 0, sizeof(*g));
    break;
  case GRAPH_ADD_NODE: {
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    size_t count = 0;
    r = corex_backend_graph_nodes(g->graph, NULL, &count);
    if (r)
      break;
    if (count >= CRX_GRAPH_MAX_NODES) {
      status = ST_NO_RESOURCE;
      break;
    }
    if (q.data_length < 4) {
      status = ST_BAD_REQUEST;
      break;
    }
    uint32_t deps = corex_protocol_read_u32(q.data);
    if (deps > CRX_GRAPH_MAX_NODES || q.data_length < 4 + 8 * deps) {
      status = ST_BAD_REQUEST;
      break;
    }
    CUgraphNode native_deps[CRX_GRAPH_MAX_NODES];
    status =
        resolve_graph_dependencies(s, g->id, q.data + 4, deps, native_deps);
    if (status)
      break;
    GraphNodeEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i)
      if (!s->graph_nodes[i].used) {
        slot = &s->graph_nodes[i];
        break;
      }
    if (!slot) {
      status = ST_NO_RESOURCE;
      break;
    }
    uint32_t plen = q.data_length - 4 - 8 * deps;
    const unsigned char *p = q.data + 4 + 8 * deps;
    if (plen > CRX_GRAPH_PARAM_BYTES) {
      status = ST_BAD_REQUEST;
      break;
    }
    GraphNativeParams d;
    status = decode_graph_params(s, q.kind, p, plen, &d);
    if (status)
      break;
    unsigned char *saved = malloc(plen ? plen : 1);
    if (!saved) {
      release_graph_params(q.kind, &d);
      status = ST_NO_RESOURCE;
      break;
    }
    memcpy(saved, p, plen);
    switch (q.kind) {
    case CU_GRAPH_NODE_TYPE_EMPTY:
      r = corex_backend_graph_add_empty(&slot->node, g->graph, native_deps,
                                        deps);
      break;
    case CU_GRAPH_NODE_TYPE_MEMCPY:
      r = corex_backend_graph_add_copy(&slot->node, g->graph, native_deps, deps,
                                       &d.copy, g_ctx);
      break;
    case CU_GRAPH_NODE_TYPE_MEMSET:
      r = corex_backend_graph_add_fill(&slot->node, g->graph, native_deps, deps,
                                       &d.fill, g_ctx);
      break;
    case CU_GRAPH_NODE_TYPE_KERNEL:
      r = corex_backend_graph_add_kernel(&slot->node, g->graph, native_deps,
                                         deps, &d.kernel.native);
      break;
    case CU_GRAPH_NODE_TYPE_EVENT_RECORD:
      r = corex_backend_graph_add_record(&slot->node, g->graph, native_deps,
                                         deps, d.event);
      break;
    case CU_GRAPH_NODE_TYPE_WAIT_EVENT:
      r = corex_backend_graph_add_wait(&slot->node, g->graph, native_deps, deps,
                                       d.event);
      break;
    default:
      r = CUDA_ERROR_NOT_SUPPORTED;
      break;
    }
    release_graph_params(q.kind, &d);
    if (r) {
      free(saved);
      memset(slot, 0, sizeof(*slot));
      break;
    }
    slot->used = 1;
    slot->id = ++s->next_graph_node_id;
    slot->graph_id = g->id;
    slot->kind = q.kind;
    slot->params = saved;
    slot->length = plen;
    out.object = slot->id;
    break;
  }
  case GRAPH_SET_NODE:
  case GRAPH_EXEC_SET_NODE: {
    if (q.action == GRAPH_SET_NODE && q.exec) {
      status = ST_BAD_REQUEST;
      break;
    }
    if (!n || (q.action == GRAPH_EXEC_SET_NODE && !e)) {
      status = ST_NOT_FOUND;
      break;
    }
    GraphNodeEntry *target =
        q.action == GRAPH_SET_NODE ? n : exec_graph_node(e, n->id);
    if (!target) {
      status = ST_BAD_REQUEST;
      break;
    }
    if (target->kind != q.kind || q.data_length > CRX_GRAPH_PARAM_BYTES) {
      status = ST_BAD_REQUEST;
      break;
    }
    /* CUDA executable updates cannot change kernel function identity. */
    if (q.action == GRAPH_EXEC_SET_NODE &&
        q.kind == CU_GRAPH_NODE_TYPE_KERNEL &&
        (q.data_length < 48 || corex_protocol_read_u64(q.data) !=
                                   corex_protocol_read_u64(target->params))) {
      status = ST_NOT_SUPPORTED;
      break;
    }
    GraphNativeParams d;
    status = decode_graph_params(s, q.kind, q.data, q.data_length, &d);
    if (status)
      break;
    unsigned char *saved = malloc(q.data_length ? q.data_length : 1);
    if (!saved) {
      release_graph_params(q.kind, &d);
      status = ST_NO_RESOURCE;
      break;
    }
    memcpy(saved, q.data, q.data_length);
    if (q.kind == CU_GRAPH_NODE_TYPE_MEMCPY)
      r = e ? corex_backend_graph_exec_set_copy(e->exec, n->node, &d.copy,
                                                g_ctx)
            : corex_backend_graph_set_copy(n->node, &d.copy);
    else if (q.kind == CU_GRAPH_NODE_TYPE_MEMSET)
      r = e ? corex_backend_graph_exec_set_fill(e->exec, n->node, &d.fill,
                                                g_ctx)
            : corex_backend_graph_set_fill(n->node, &d.fill);
    else if (q.kind == CU_GRAPH_NODE_TYPE_KERNEL)
      r = e ? corex_backend_graph_exec_set_kernel(e->exec, n->node,
                                                  &d.kernel.native)
            : corex_backend_graph_set_kernel(n->node, &d.kernel.native);
    else
      r = CUDA_ERROR_NOT_SUPPORTED;
    release_graph_params(q.kind, &d);
    if (r) {
      free(saved);
      break;
    }
    free(target->params);
    target->params = saved;
    target->length = q.data_length;
    break;
  }
  case GRAPH_GET_PARAMS:
  case GRAPH_GET_TYPE:
    if (!n) {
      status = ST_NOT_FOUND;
      break;
    }
    out.value = n->kind;
    if (q.action == GRAPH_GET_PARAMS) {
      memcpy(out.data, n->params, n->length);
      out.data_length = n->length;
    }
    break;
  case GRAPH_DESTROY_NODE:
    if (!n) {
      status = ST_NOT_FOUND;
      break;
    }
    r = corex_backend_graph_destroy_node(n->node);
    if (!r)
      release_graph_node(n);
    break;
  case GRAPH_INSTANTIATE: {
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    if (q.value) {
      status = ST_NOT_SUPPORTED;
      break;
    }
    GraphExecEntry *slot = NULL;
    for (size_t i = 0; i < CRX_GRAPH_MAX_EXECS; ++i)
      if (!s->graph_execs[i].used) {
        slot = &s->graph_execs[i];
        break;
      }
    if (!slot) {
      status = ST_NO_RESOURCE;
      break;
    }
    memset(slot, 0, sizeof(*slot));
    for (size_t i = 0; i < CRX_GRAPH_MAX_NODE_HANDLES; ++i) {
      GraphNodeEntry *src = &s->graph_nodes[i];
      if (!src->used || src->graph_id != g->id)
        continue;
      GraphNativeParams d;
      status = decode_graph_params(s, src->kind, src->params, src->length, &d);
      if (status)
        break;
      release_graph_params(src->kind, &d);
      GraphNodeEntry *dst = &slot->nodes[slot->count++];
      *dst = *src;
      dst->params = malloc(src->length ? src->length : 1);
      if (!dst->params) {
        status = ST_NO_RESOURCE;
        break;
      }
      memcpy(dst->params, src->params, src->length);
    }
    if (!status)
      r = corex_backend_graph_instantiate(&slot->exec, g->graph);
    if (status || r) {
      for (size_t i = 0; i < slot->count; ++i)
        release_graph_node(&slot->nodes[i]);
      memset(slot, 0, sizeof(*slot));
      break;
    }
    slot->used = 1;
    slot->id = ++s->next_graph_exec_id;
    out.object = slot->id;
    break;
  }
  case GRAPH_EXEC_DESTROY:
    if (!e) {
      status = ST_NOT_FOUND;
      break;
    }
    r = corex_backend_graph_exec_destroy(e->exec);
    if (r)
      break;
    for (size_t i = 0; i < e->count; ++i)
      release_graph_node(&e->nodes[i]);
    memset(e, 0, sizeof(*e));
    break;
  case GRAPH_LAUNCH: {
    if (!e) {
      status = ST_NOT_FOUND;
      break;
    }
    CUstream stream = 0;
    if (q.stream) {
      StreamEntry *v = find_stream(s, q.stream);
      if (!v) {
        status = ST_NOT_FOUND;
        break;
      }
      stream = v->stream;
    }
    /* Native graph objects contain backend pointers. Recheck all owners
     * before launch so stale AllocationID/KernelID cannot reach CoreX. */
    for (size_t i = 0; i < e->count; ++i) {
      GraphNodeEntry *v = &e->nodes[i];
      GraphNativeParams d;
      status = decode_graph_params(s, v->kind, v->params, v->length, &d);
      if (status)
        break;
      release_graph_params(v->kind, &d);
    }
    if (!status)
      r = corex_backend_graph_launch(e->exec, stream);
    break;
  }
  case GRAPH_GET_NODES:
  case GRAPH_GET_ROOTS:
  case GRAPH_GET_DEPENDENCIES:
  case GRAPH_GET_DEPENDENTS: {
    if ((q.action == GRAPH_GET_NODES || q.action == GRAPH_GET_ROOTS) ? !g
                                                                     : !n) {
      status = ST_NOT_FOUND;
      break;
    }
    CUgraphNode list[CRX_GRAPH_MAX_NODES];
    size_t count = CRX_GRAPH_MAX_NODES;
    if (q.action == GRAPH_GET_NODES)
      r = corex_backend_graph_nodes(g->graph, list, &count);
    else if (q.action == GRAPH_GET_ROOTS)
      r = corex_backend_graph_roots(g->graph, list, &count);
    else if (q.action == GRAPH_GET_DEPENDENCIES)
      r = corex_backend_graph_dependencies(n->node, list, &count);
    else
      r = corex_backend_graph_dependents(n->node, list, &count);
    if (r)
      break;
    if (count > CRX_GRAPH_MAX_NODES) {
      status = ST_INTERNAL;
      break;
    }
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
      uint64_t id = native_graph_node_id(s, list[i]);
      if (!id) {
        status = ST_INTERNAL;
        break;
      }
      corex_protocol_write_u64(out.data, &pos, id);
    }
    out.data_length = (uint32_t)pos;
    out.value = (uint32_t)count;
    break;
  }
  case GRAPH_GET_EDGES: {
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    CUgraphNode from[CRX_GRAPH_MAX_NODES * CRX_GRAPH_MAX_NODES],
        to[CRX_GRAPH_MAX_NODES * CRX_GRAPH_MAX_NODES];
    size_t count = CRX_GRAPH_MAX_NODES * CRX_GRAPH_MAX_NODES;
    r = corex_backend_graph_edges(g->graph, from, to, &count);
    if (r)
      break;
    if (count > 4032) {
      status = ST_INTERNAL;
      break;
    }
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
      corex_protocol_write_u64(out.data, &pos,
                               native_graph_node_id(s, from[i]));
      corex_protocol_write_u64(out.data, &pos, native_graph_node_id(s, to[i]));
    }
    out.data_length = (uint32_t)pos;
    out.value = (uint32_t)count;
    break;
  }
  case GRAPH_ADD_DEPENDENCIES:
  case GRAPH_REMOVE_DEPENDENCIES: {
    if (!g) {
      status = ST_NOT_FOUND;
      break;
    }
    if (q.data_length % 16 || q.data_length / 16 > CRX_GRAPH_MAX_NODES) {
      status = ST_BAD_REQUEST;
      break;
    }
    size_t count = q.data_length / 16;
    CUgraphNode from[CRX_GRAPH_MAX_NODES], to[CRX_GRAPH_MAX_NODES];
    for (size_t i = 0; i < count; ++i) {
      GraphNodeEntry *f = find_graph_node(
                         s, corex_protocol_read_u64(q.data + 16 * i)),
                     *t = find_graph_node(
                         s, corex_protocol_read_u64(q.data + 16 * i + 8));
      if (!f || !t) {
        status = ST_NOT_FOUND;
        break;
      }
      if (f == t || f->graph_id != g->id || t->graph_id != g->id) {
        status = ST_BAD_REQUEST;
        break;
      }
      from[i] = f->node;
      to[i] = t->node;
    }
    if (!status)
      status = validate_graph_edge_change(g->graph, from, to, count,
                                          q.action == GRAPH_ADD_DEPENDENCIES);
    if (!status)
      r = q.action == GRAPH_ADD_DEPENDENCIES
              ? corex_backend_graph_add_dependencies(g->graph, from, to, count)
              : corex_backend_graph_remove_dependencies(g->graph, from, to,
                                                        count);
    break;
  }
  case GRAPH_SET_ENABLED:
  case GRAPH_GET_ENABLED:
    if (!e || !n) {
      status = ST_NOT_FOUND;
      break;
    }
    if (!exec_graph_node(e, n->id) ||
        (n->kind != CU_GRAPH_NODE_TYPE_KERNEL &&
         n->kind != CU_GRAPH_NODE_TYPE_MEMCPY &&
         n->kind != CU_GRAPH_NODE_TYPE_MEMSET) ||
        q.value > 1) {
      status = ST_BAD_REQUEST;
      break;
    }
    if (q.action == GRAPH_SET_ENABLED)
      r = corex_backend_graph_set_enabled(e->exec, n->node, (unsigned)q.value);
    else
      r = corex_backend_graph_get_enabled(e->exec, n->node, &out.value);
    break;
  default:
    status = ST_BAD_REQUEST;
    break;
  }
  if (!status)
    status = graph_result(r);
  if (status)
    return send_response(fd, OP_GRAPH, req_id, status, NULL, 0);
  unsigned char
      wire[(16 + COREX_GENERATED_CUDAGRAPHCREATE_RESPONSE_DATA_MAX_BYTES)];
  size_t wire_len = 0;
  if (corex_generated_encode_cudaGraphCreate_response(wire, sizeof(wire),
                                                      &wire_len, &out) != 0)
    return send_response(fd, OP_GRAPH, req_id, ST_INTERNAL, NULL, 0);
  return send_response(fd, OP_GRAPH, req_id, ST_OK, wire, (uint32_t)wire_len);
}
