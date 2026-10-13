#include "corex_backend.h"

CUresult corex_backend_module_global(CUdeviceptr *pointer, size_t *bytes, CUmodule module, const char *name)
{ return cuModuleGetGlobal(pointer,bytes,module,name); }

CUresult corex_backend_occupancy_flags(int *blocks, CUfunction function, int block_size,
                                       size_t dynamic_shared, unsigned int flags)
{ return cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(blocks, function, block_size, dynamic_shared, flags); }
CUresult corex_backend_occupancy_potential(int *grid, int *block, CUfunction function,
                                           size_t dynamic_shared, int block_limit)
{ return cuOccupancyMaxPotentialBlockSize(grid, block, function, NULL, dynamic_shared, block_limit); }
CUresult corex_backend_function_set_shared_config(CUfunction function, CUsharedconfig config)
{ return cuFuncSetSharedMemConfig(function, config); }

CUresult corex_backend_init(void) { return cuInit(0); }
CUresult corex_backend_driver_version(int *version) { return cuDriverGetVersion(version); }
CUresult corex_backend_device_count(int *count) { return cuDeviceGetCount(count); }
CUresult corex_backend_device_get(CUdevice *device, int ordinal) { return cuDeviceGet(device, ordinal); }
CUresult corex_backend_device_name(char *name, int length, CUdevice device) { return cuDeviceGetName(name, length, device); }
CUresult corex_backend_device_total_mem(size_t *bytes, CUdevice device) { return cuDeviceTotalMem(bytes, device); }
CUresult corex_backend_device_attribute(int *value, CUdevice_attribute attribute, CUdevice device) { return cuDeviceGetAttribute(value, attribute, device); }
CUresult corex_backend_device_pci_bus_id(char *pci_bus_id, int length, CUdevice device) { return cuDeviceGetPCIBusId(pci_bus_id, length, device); }
CUresult corex_backend_device_get_by_pci_bus_id(CUdevice *device, const char *pci_bus_id) { return cuDeviceGetByPCIBusId(device, pci_bus_id); }
CUresult corex_backend_context_create(CUcontext *context, unsigned int flags, CUdevice device) { return cuCtxCreate(context, flags, device); }
CUresult corex_backend_context_destroy(CUcontext context) { return cuCtxDestroy(context); }
CUresult corex_backend_context_sync(void) { return cuCtxSynchronize(); }
CUresult corex_backend_context_get_flags(unsigned int *flags) { return cuCtxGetFlags(flags); }
CUresult corex_backend_context_get_stream_priority_range(int *least_priority, int *greatest_priority) { return cuCtxGetStreamPriorityRange(least_priority, greatest_priority); }
CUresult corex_backend_context_get_limit(size_t *value, CUlimit limit) { return cuCtxGetLimit(value, limit); }
CUresult corex_backend_context_get_cache_config(CUfunc_cache *config) { return cuCtxGetCacheConfig(config); }
CUresult corex_backend_context_get_shared_mem_config(CUsharedconfig *config) { return cuCtxGetSharedMemConfig(config); }
CUresult corex_backend_mem_info(size_t *free_bytes, size_t *total_bytes) { return cuMemGetInfo(free_bytes, total_bytes); }
CUresult corex_backend_mem_alloc(CUdeviceptr *pointer, size_t bytes) { return cuMemAlloc(pointer, bytes); }
CUresult corex_backend_mem_alloc_pitch(CUdeviceptr *pointer, size_t *pitch, size_t width, size_t height, unsigned int element_size) { return cuMemAllocPitch(pointer, pitch, width, height, element_size); }
CUresult corex_backend_mem_free(CUdeviceptr pointer) { return cuMemFree(pointer); }
CUresult corex_backend_host_alloc(void **pointer, size_t bytes) { return cuMemAllocHost(pointer, bytes); }
CUresult corex_backend_host_free(void *pointer) { return cuMemFreeHost(pointer); }
CUresult corex_backend_copy_h2d(CUdeviceptr dst, const void *src, size_t bytes) { return cuMemcpyHtoD(dst, src, bytes); }
CUresult corex_backend_copy_d2h(void *dst, CUdeviceptr src, size_t bytes) { return cuMemcpyDtoH(dst, src, bytes); }
CUresult corex_backend_copy_d2d(CUdeviceptr dst, CUdeviceptr src, size_t bytes) { return cuMemcpyDtoD(dst, src, bytes); }
CUresult corex_backend_copy_d2d_async(CUdeviceptr dst, CUdeviceptr src, size_t bytes, CUstream stream) { return cuMemcpyDtoDAsync(dst, src, bytes, stream); }
CUresult corex_backend_copy_2d(const CUDA_MEMCPY2D *copy) { return cuMemcpy2D(copy); }
CUresult corex_backend_copy_2d_async(const CUDA_MEMCPY2D *copy, CUstream stream) { return cuMemcpy2DAsync(copy, stream); }
CUresult corex_backend_copy_3d(const CUDA_MEMCPY3D *copy) { return cuMemcpy3D(copy); }
CUresult corex_backend_copy_3d_async(const CUDA_MEMCPY3D *copy, CUstream stream) { return cuMemcpy3DAsync(copy, stream); }
CUresult corex_backend_memset_d8(CUdeviceptr dst, unsigned char value, size_t bytes) { return cuMemsetD8(dst, value, bytes); }
CUresult corex_backend_memset_d8_async(CUdeviceptr dst, unsigned char value, size_t bytes, CUstream stream) { return cuMemsetD8Async(dst, value, bytes, stream); }
CUresult corex_backend_memset_d2d8(CUdeviceptr dst, size_t pitch, unsigned char value, size_t width, size_t height) { return cuMemsetD2D8(dst, pitch, value, width, height); }
CUresult corex_backend_memset_d2d8_async(CUdeviceptr dst, size_t pitch, unsigned char value, size_t width, size_t height, CUstream stream) { return cuMemsetD2D8Async(dst, pitch, value, width, height, stream); }
CUresult corex_backend_copy_h2d_async(CUdeviceptr dst, const void *src, size_t bytes, CUstream stream) { return cuMemcpyHtoDAsync(dst, src, bytes, stream); }
CUresult corex_backend_copy_d2h_async(void *dst, CUdeviceptr src, size_t bytes, CUstream stream) { return cuMemcpyDtoHAsync(dst, src, bytes, stream); }
CUresult corex_backend_stream_create(CUstream *stream, unsigned int flags) { return cuStreamCreate(stream, flags); }
CUresult corex_backend_stream_create_priority(CUstream *stream, unsigned int flags, int priority) { return cuStreamCreateWithPriority(stream, flags, priority); }
CUresult corex_backend_stream_get_flags(CUstream stream, unsigned int *flags) { return cuStreamGetFlags(stream, flags); }
CUresult corex_backend_stream_get_priority(CUstream stream, int *priority) { return cuStreamGetPriority(stream, priority); }
CUresult corex_backend_stream_destroy(CUstream stream) { return cuStreamDestroy(stream); }
CUresult corex_backend_stream_query(CUstream stream) { return cuStreamQuery(stream); }
CUresult corex_backend_stream_sync(CUstream stream) { return cuStreamSynchronize(stream); }
CUresult corex_backend_stream_wait_event(CUstream stream, CUevent event, unsigned int flags) { return cuStreamWaitEvent(stream, event, flags); }
CUresult corex_backend_event_create(CUevent *event, unsigned int flags) { return cuEventCreate(event, flags); }
CUresult corex_backend_event_destroy(CUevent event) { return cuEventDestroy(event); }
CUresult corex_backend_event_record(CUevent event, CUstream stream) { return cuEventRecord(event, stream); }
CUresult corex_backend_event_query(CUevent event) { return cuEventQuery(event); }
CUresult corex_backend_event_sync(CUevent event) { return cuEventSynchronize(event); }
CUresult corex_backend_event_elapsed(float *milliseconds, CUevent start, CUevent end) { return cuEventElapsedTime(milliseconds, start, end); }
CUresult corex_backend_module_load(CUmodule *module, const void *image) { return cuModuleLoadData(module, image); }
CUresult corex_backend_module_unload(CUmodule module) { return cuModuleUnload(module); }
CUresult corex_backend_module_function(CUfunction *function, CUmodule module, const char *name) { return cuModuleGetFunction(function, module, name); }
CUresult corex_backend_function_attribute(int *value, CUfunction function, CUfunction_attribute attribute) { return cuFuncGetAttribute(value, attribute, function); }
CUresult corex_backend_function_set_attribute(CUfunction function, CUfunction_attribute attribute, int value) { return cuFuncSetAttribute(function, attribute, value); }
CUresult corex_backend_function_set_cache_config(CUfunction function, CUfunc_cache config) { return cuFuncSetCacheConfig(function, config); }
CUresult corex_backend_occupancy(int *blocks, CUfunction function, int block_size, size_t dynamic_shared) { return cuOccupancyMaxActiveBlocksPerMultiprocessor(blocks, function, block_size, dynamic_shared); }
CUresult corex_backend_launch(CUfunction function, unsigned int grid_x, unsigned int grid_y, unsigned int grid_z, unsigned int block_x, unsigned int block_y, unsigned int block_z, unsigned int shared_bytes, CUstream stream, void **kernel_params, void **extra)
{
    return cuLaunchKernel(function, grid_x, grid_y, grid_z,
                          block_x, block_y, block_z, shared_bytes,
                          stream, kernel_params, extra);
}

CUresult corex_backend_context_set_limit(CUlimit limit, size_t value) { return cuCtxSetLimit(limit, value); }
CUresult corex_backend_context_set_cache_config(CUfunc_cache config) { return cuCtxSetCacheConfig(config); }
CUresult corex_backend_context_set_shared_mem_config(CUsharedconfig config) { return cuCtxSetSharedMemConfig(config); }
CUresult corex_backend_stream_copy_attributes(CUstream dst, CUstream src) { return cuStreamCopyAttributes(dst, src); }

CUresult corex_backend_stream_get_attribute(CUstream stream, CUstreamAttrID attr, CUstreamAttrValue *value) { return cuStreamGetAttribute(stream, attr, value); }
CUresult corex_backend_stream_set_attribute(CUstream stream, CUstreamAttrID attr, const CUstreamAttrValue *value) { return cuStreamSetAttribute(stream, attr, value); }

CUresult corex_backend_device_default_pool(CUmemoryPool *pool, CUdevice device) { return cuDeviceGetDefaultMemPool(pool,device); }
CUresult corex_backend_device_get_pool(CUmemoryPool *pool, CUdevice device) { return cuDeviceGetMemPool(pool,device); }
CUresult corex_backend_device_set_pool(CUdevice device, CUmemoryPool pool) { return cuDeviceSetMemPool(device,pool); }
CUresult corex_backend_pool_create(CUmemoryPool *pool, const CUmemPoolProps *props) { return cuMemPoolCreate(pool,props); }
CUresult corex_backend_pool_destroy(CUmemoryPool pool) { return cuMemPoolDestroy(pool); }
CUresult corex_backend_pool_trim(CUmemoryPool pool, size_t bytes)
{
    if (bytes) return cuMemPoolTrimTo(pool,bytes);
    /* CoreX's native TrimTo leaves cached blocks reserved even after stream
     * completion. Its zero-threshold synchronization path really releases
     * them. Use that measured path for zero-target trim, preserving policy;
     * nonzero targets stay native so we never reclaim below their minimum. */
    uint64_t threshold=0,zero=0;
    CUresult result=cuMemPoolGetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&threshold);
    if (result!=CUDA_SUCCESS) return result;
    result=cuMemPoolSetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&zero);
    if (result!=CUDA_SUCCESS) return result;
    result=cuCtxSynchronize();
    if (result==CUDA_SUCCESS) result=cuMemPoolTrimTo(pool,0);
    CUresult restored=cuMemPoolSetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&threshold);
    return result==CUDA_SUCCESS?restored:result;
}
CUresult corex_backend_pool_get_attribute(CUmemoryPool pool, CUmemPool_attribute attr, void *value) { return cuMemPoolGetAttribute(pool,attr,value); }
CUresult corex_backend_pool_set_attribute(CUmemoryPool pool, CUmemPool_attribute attr, void *value) { return cuMemPoolSetAttribute(pool,attr,value); }
CUresult corex_backend_pool_get_access(CUmemAccess_flags *flags, CUmemoryPool pool, CUmemLocation *location) { return cuMemPoolGetAccess(flags,pool,location); }
CUresult corex_backend_pool_set_access(CUmemoryPool pool, const CUmemAccessDesc *desc, size_t count) { return cuMemPoolSetAccess(pool,desc,count); }
CUresult corex_backend_mem_alloc_async(CUdeviceptr *ptr, size_t bytes, CUstream stream) { return cuMemAllocAsync(ptr,bytes,stream); }
CUresult corex_backend_mem_alloc_from_pool_async(CUdeviceptr *ptr, size_t bytes, CUmemoryPool pool, CUstream stream) { return cuMemAllocFromPoolAsync(ptr,bytes,pool,stream); }
CUresult corex_backend_mem_free_async(CUdeviceptr ptr, CUstream stream) { return cuMemFreeAsync(ptr,stream); }

/* Native graph boundary. DAG and owner policy belongs to ServerSession. */
CUresult corex_backend_graph_create(CUgraph *graph, unsigned int flags)
{
    return cuGraphCreate(graph, flags);
}

CUresult corex_backend_graph_destroy(CUgraph graph)
{
    return cuGraphDestroy(graph);
}

CUresult corex_backend_graph_add_empty(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count)
{
    return cuGraphAddEmptyNode(node, graph, deps, count);
}

CUresult corex_backend_graph_add_kernel(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_KERNEL_NODE_PARAMS *params)
{
    return cuGraphAddKernelNode(node, graph, deps, count, params);
}

CUresult corex_backend_graph_set_kernel(CUgraphNode node, const CUDA_KERNEL_NODE_PARAMS *params)
{
    return cuGraphKernelNodeSetParams(node, params);
}

CUresult corex_backend_graph_exec_set_kernel(CUgraphExec exec, CUgraphNode node, const CUDA_KERNEL_NODE_PARAMS *params)
{
    return cuGraphExecKernelNodeSetParams(exec, node, params);
}

CUresult corex_backend_graph_add_copy(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_MEMCPY3D *params, CUcontext ctx)
{
    return cuGraphAddMemcpyNode(node, graph, deps, count, params, ctx);
}

CUresult corex_backend_graph_set_copy(CUgraphNode node, const CUDA_MEMCPY3D *params)
{
    return cuGraphMemcpyNodeSetParams(node, params);
}

CUresult corex_backend_graph_exec_set_copy(CUgraphExec exec, CUgraphNode node, const CUDA_MEMCPY3D *params, CUcontext ctx)
{
    return cuGraphExecMemcpyNodeSetParams(exec, node, params, ctx);
}

CUresult corex_backend_graph_add_fill(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_MEMSET_NODE_PARAMS *params, CUcontext ctx)
{
    return cuGraphAddMemsetNode(node, graph, deps, count, params, ctx);
}

CUresult corex_backend_graph_set_fill(CUgraphNode node, const CUDA_MEMSET_NODE_PARAMS *params)
{
    return cuGraphMemsetNodeSetParams(node, params);
}

CUresult corex_backend_graph_exec_set_fill(CUgraphExec exec, CUgraphNode node, const CUDA_MEMSET_NODE_PARAMS *params, CUcontext ctx)
{
    return cuGraphExecMemsetNodeSetParams(exec, node, params, ctx);
}

CUresult corex_backend_graph_instantiate(CUgraphExec *exec, CUgraph graph)
{
    return cuGraphInstantiate(exec, graph, NULL, NULL, 0);
}

CUresult corex_backend_graph_exec_destroy(CUgraphExec exec)
{
    return cuGraphExecDestroy(exec);
}

CUresult corex_backend_graph_launch(CUgraphExec exec, CUstream stream)
{
    return cuGraphLaunch(exec, stream);
}

CUresult corex_backend_graph_nodes(CUgraph graph, CUgraphNode *nodes, size_t *count)
{
    return cuGraphGetNodes(graph, nodes, count);
}

CUresult corex_backend_graph_roots(CUgraph graph, CUgraphNode *nodes, size_t *count)
{
    return cuGraphGetRootNodes(graph, nodes, count);
}

CUresult corex_backend_graph_edges(CUgraph graph, CUgraphNode *from, CUgraphNode *to, size_t *count)
{
    return cuGraphGetEdges(graph, from, to, count);
}

CUresult corex_backend_graph_dependencies(CUgraphNode node, CUgraphNode *nodes, size_t *count)
{
    return cuGraphNodeGetDependencies(node, nodes, count);
}

CUresult corex_backend_graph_dependents(CUgraphNode node, CUgraphNode *nodes, size_t *count)
{
    return cuGraphNodeGetDependentNodes(node, nodes, count);
}

CUresult corex_backend_graph_add_dependencies(CUgraph graph, const CUgraphNode *from, const CUgraphNode *to, size_t count)
{
    return cuGraphAddDependencies(graph, from, to, count);
}

CUresult corex_backend_graph_remove_dependencies(CUgraph graph, const CUgraphNode *from, const CUgraphNode *to, size_t count)
{
    return cuGraphRemoveDependencies(graph, from, to, count);
}

CUresult corex_backend_graph_destroy_node(CUgraphNode node)
{
    return cuGraphDestroyNode(node);
}

CUresult corex_backend_graph_set_enabled(CUgraphExec exec, CUgraphNode node, unsigned int enabled)
{
    return cuGraphNodeSetEnabled(exec, node, enabled);
}

CUresult corex_backend_graph_get_enabled(CUgraphExec exec, CUgraphNode node, unsigned int *enabled)
{
    return cuGraphNodeGetEnabled(exec, node, enabled);
}

CUresult corex_backend_graph_set_record(CUgraphNode node, CUevent event)
{ return cuGraphEventRecordNodeSetEvent(node, event); }
CUresult corex_backend_graph_mem_get(CUdevice device, CUgraphMem_attribute attr, uint64_t *value)
{ return cuDeviceGetGraphMemAttribute(device, attr, value); }
CUresult corex_backend_graph_mem_set(CUdevice device, CUgraphMem_attribute attr, uint64_t value)
{ return cuDeviceSetGraphMemAttribute(device, attr, &value); }
CUresult corex_backend_graph_mem_trim(CUdevice device)
{ return cuDeviceGraphMemTrim(device); }
CUresult corex_backend_graph_set_wait(CUgraphNode node, CUevent event)
{ return cuGraphEventWaitNodeSetEvent(node, event); }
CUresult corex_backend_graph_exec_set_record(CUgraphExec exec, CUgraphNode node, CUevent event)
{ return cuGraphExecEventRecordNodeSetEvent(exec, node, event); }
CUresult corex_backend_graph_exec_set_wait(CUgraphExec exec, CUgraphNode node, CUevent event)
{ return cuGraphExecEventWaitNodeSetEvent(exec, node, event); }
CUresult corex_backend_capture_begin(CUstream stream, CUstreamCaptureMode mode)
{
    return cuStreamBeginCapture(stream, mode);
}
CUresult corex_backend_capture_begin_to_graph(CUstream stream, CUgraph graph, const CUgraphNode *deps, size_t count)
{ return cuStreamBeginCaptureToGraph(stream, graph, deps, NULL, count, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL); }
CUresult corex_backend_capture_dependencies(CUstream stream, CUgraphNode *deps, size_t count, unsigned flags)
{ return cuStreamUpdateCaptureDependencies(stream, deps, count, flags); }

CUresult corex_backend_capture_end(CUstream stream, CUgraph *graph)
{
    return cuStreamEndCapture(stream, graph);
}

CUresult corex_backend_capture_info(CUstream stream, CUstreamCaptureStatus *status, cuuint64_t *id, CUgraph *graph, const CUgraphNode **deps, size_t *count)
{
    return cuStreamGetCaptureInfo_v2(stream, status, id, graph, deps, count);
}

CUresult corex_backend_graph_clone(CUgraph *clone, CUgraph graph)
{
    return cuGraphClone(clone, graph);
}

CUresult corex_backend_graph_find_clone(CUgraphNode *node, CUgraphNode original, CUgraph clone)
{
    return cuGraphNodeFindInClone(node, original, clone);
}

CUresult corex_backend_graph_exec_update(CUgraphExec exec, CUgraph graph, CUgraphNode *error, CUgraphExecUpdateResult *result)
{
    return cuGraphExecUpdate(exec, graph, error, result);
}

CUresult corex_backend_graph_add_record(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, CUevent event)
{
    return cuGraphAddEventRecordNode(node, graph, deps, count, event);
}

CUresult corex_backend_graph_add_wait(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, CUevent event)
{
    return cuGraphAddEventWaitNode(node, graph, deps, count, event);
}
