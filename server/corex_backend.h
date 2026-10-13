#ifndef COREX_SERVER_BACKEND_H
#define COREX_SERVER_BACKEND_H

#include <cuda.h>
#include <stddef.h>

/*
 * Narrow, statically linked seam for the proven CoreX Driver primitives.
 * Server handlers own protocol semantics and object tables; this module owns
 * direct vendor calls. No runtime backend selection or plugin ABI is implied.
 */
CUresult corex_backend_init(void);
CUresult corex_backend_driver_version(int *version);
CUresult corex_backend_device_count(int *count);
CUresult corex_backend_device_get(CUdevice *device, int ordinal);
CUresult corex_backend_device_name(char *name, int length, CUdevice device);
CUresult corex_backend_device_total_mem(size_t *bytes, CUdevice device);
CUresult corex_backend_device_attribute(int *value, CUdevice_attribute attribute,
                                        CUdevice device);
CUresult corex_backend_device_pci_bus_id(char *pci_bus_id, int length,
                                         CUdevice device);
CUresult corex_backend_device_get_by_pci_bus_id(CUdevice *device,
                                                const char *pci_bus_id);
CUresult corex_backend_context_create(CUcontext *context, unsigned int flags,
                                      CUdevice device);
CUresult corex_backend_context_destroy(CUcontext context);
CUresult corex_backend_context_sync(void);
CUresult corex_backend_context_get_flags(unsigned int *flags);
CUresult corex_backend_context_get_stream_priority_range(int *least_priority,
                                                         int *greatest_priority);
CUresult corex_backend_context_get_limit(size_t *value, CUlimit limit);
CUresult corex_backend_context_get_cache_config(CUfunc_cache *config);
CUresult corex_backend_context_get_shared_mem_config(CUsharedconfig *config);
CUresult corex_backend_mem_info(size_t *free_bytes, size_t *total_bytes);
CUresult corex_backend_mem_alloc(CUdeviceptr *pointer, size_t bytes);
CUresult corex_backend_mem_alloc_pitch(CUdeviceptr *pointer, size_t *pitch,
                                        size_t width, size_t height,
                                        unsigned int element_size);
CUresult corex_backend_mem_free(CUdeviceptr pointer);
CUresult corex_backend_device_default_pool(CUmemoryPool *pool, CUdevice device);
CUresult corex_backend_device_get_pool(CUmemoryPool *pool, CUdevice device);
CUresult corex_backend_device_set_pool(CUdevice device, CUmemoryPool pool);
CUresult corex_backend_pool_create(CUmemoryPool *pool, const CUmemPoolProps *props);
CUresult corex_backend_pool_destroy(CUmemoryPool pool);
CUresult corex_backend_pool_trim(CUmemoryPool pool, size_t bytes);
CUresult corex_backend_pool_get_attribute(CUmemoryPool pool, CUmemPool_attribute attr, void *value);
CUresult corex_backend_pool_set_attribute(CUmemoryPool pool, CUmemPool_attribute attr, void *value);
CUresult corex_backend_pool_get_access(CUmemAccess_flags *flags, CUmemoryPool pool, CUmemLocation *location);
CUresult corex_backend_pool_set_access(CUmemoryPool pool, const CUmemAccessDesc *desc, size_t count);
CUresult corex_backend_mem_alloc_async(CUdeviceptr *ptr, size_t bytes, CUstream stream);
CUresult corex_backend_mem_alloc_from_pool_async(CUdeviceptr *ptr, size_t bytes, CUmemoryPool pool, CUstream stream);
CUresult corex_backend_mem_free_async(CUdeviceptr ptr, CUstream stream);
CUresult corex_backend_host_alloc(void **pointer, size_t bytes);
CUresult corex_backend_host_free(void *pointer);
CUresult corex_backend_copy_h2d(CUdeviceptr dst, const void *src, size_t bytes);
CUresult corex_backend_copy_d2h(void *dst, CUdeviceptr src, size_t bytes);
CUresult corex_backend_copy_d2d(CUdeviceptr dst, CUdeviceptr src, size_t bytes);
CUresult corex_backend_copy_d2d_async(CUdeviceptr dst, CUdeviceptr src, size_t bytes, CUstream stream);
CUresult corex_backend_copy_2d(const CUDA_MEMCPY2D *copy);
CUresult corex_backend_copy_2d_async(const CUDA_MEMCPY2D *copy, CUstream stream);
CUresult corex_backend_copy_3d(const CUDA_MEMCPY3D *copy);
CUresult corex_backend_copy_3d_async(const CUDA_MEMCPY3D *copy, CUstream stream);
CUresult corex_backend_memset_d8(CUdeviceptr dst, unsigned char value, size_t bytes);
CUresult corex_backend_memset_d8_async(CUdeviceptr dst, unsigned char value,
                                       size_t bytes, CUstream stream);
CUresult corex_backend_memset_d2d8(CUdeviceptr dst, size_t pitch,
                                    unsigned char value, size_t width, size_t height);
CUresult corex_backend_memset_d2d8_async(CUdeviceptr dst, size_t pitch,
                                          unsigned char value, size_t width,
                                          size_t height, CUstream stream);
CUresult corex_backend_copy_h2d_async(CUdeviceptr dst, const void *src,
                                      size_t bytes, CUstream stream);
CUresult corex_backend_copy_d2h_async(void *dst, CUdeviceptr src,
                                      size_t bytes, CUstream stream);
CUresult corex_backend_stream_create(CUstream *stream, unsigned int flags);
CUresult corex_backend_stream_create_priority(CUstream *stream, unsigned int flags,
                                              int priority);
CUresult corex_backend_stream_get_flags(CUstream stream, unsigned int *flags);
CUresult corex_backend_stream_get_priority(CUstream stream, int *priority);
CUresult corex_backend_stream_destroy(CUstream stream);
CUresult corex_backend_stream_query(CUstream stream);
CUresult corex_backend_stream_sync(CUstream stream);
CUresult corex_backend_stream_wait_event(CUstream stream, CUevent event,
                                         unsigned int flags);
CUresult corex_backend_event_create(CUevent *event, unsigned int flags);
CUresult corex_backend_event_destroy(CUevent event);
CUresult corex_backend_event_record(CUevent event, CUstream stream);
CUresult corex_backend_event_query(CUevent event);
CUresult corex_backend_event_sync(CUevent event);
CUresult corex_backend_event_elapsed(float *milliseconds, CUevent start, CUevent end);
CUresult corex_backend_module_load(CUmodule *module, const void *image);
CUresult corex_backend_module_unload(CUmodule module);
CUresult corex_backend_module_global(CUdeviceptr *pointer, size_t *bytes, CUmodule module, const char *name);
CUresult corex_backend_module_function(CUfunction *function, CUmodule module,
                                       const char *name);
CUresult corex_backend_function_attribute(int *value, CUfunction function,
                                          CUfunction_attribute attribute);
CUresult corex_backend_function_set_attribute(CUfunction function,
                                              CUfunction_attribute attribute,
                                              int value);
CUresult corex_backend_function_set_cache_config(CUfunction function,
                                                 CUfunc_cache config);
CUresult corex_backend_occupancy(int *blocks, CUfunction function,
                                 int block_size, size_t dynamic_shared);
CUresult corex_backend_occupancy_flags(int *blocks, CUfunction function,
                                       int block_size, size_t dynamic_shared,
                                       unsigned int flags);
CUresult corex_backend_occupancy_potential(int *grid, int *block, CUfunction function,
                                           size_t dynamic_shared, int block_limit);
CUresult corex_backend_function_set_shared_config(CUfunction function, CUsharedconfig config);
CUresult corex_backend_launch(CUfunction function,
                              unsigned int grid_x, unsigned int grid_y,
                              unsigned int grid_z, unsigned int block_x,
                              unsigned int block_y, unsigned int block_z,
                              unsigned int shared_bytes, CUstream stream,
                              void **kernel_params, void **extra);

CUresult corex_backend_context_set_limit(CUlimit limit, size_t value);
CUresult corex_backend_context_set_cache_config(CUfunc_cache config);
CUresult corex_backend_context_set_shared_mem_config(CUsharedconfig config);
CUresult corex_backend_stream_copy_attributes(CUstream dst, CUstream src);

CUresult corex_backend_stream_get_attribute(CUstream stream, CUstreamAttrID attr, CUstreamAttrValue *value);
CUresult corex_backend_stream_set_attribute(CUstream stream, CUstreamAttrID attr, const CUstreamAttrValue *value);

/* Native graphs use only server-owned handles. */
CUresult corex_backend_graph_create(CUgraph *graph, unsigned int flags);
CUresult corex_backend_graph_destroy(CUgraph graph);
CUresult corex_backend_graph_add_empty(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count);
CUresult corex_backend_graph_add_kernel(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_KERNEL_NODE_PARAMS *params);
CUresult corex_backend_graph_set_kernel(CUgraphNode node, const CUDA_KERNEL_NODE_PARAMS *params);
CUresult corex_backend_graph_exec_set_kernel(CUgraphExec exec, CUgraphNode node, const CUDA_KERNEL_NODE_PARAMS *params);
CUresult corex_backend_graph_add_copy(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_MEMCPY3D *params, CUcontext ctx);
CUresult corex_backend_graph_set_copy(CUgraphNode node, const CUDA_MEMCPY3D *params);
CUresult corex_backend_graph_exec_set_copy(CUgraphExec exec, CUgraphNode node, const CUDA_MEMCPY3D *params, CUcontext ctx);
CUresult corex_backend_graph_add_fill(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, const CUDA_MEMSET_NODE_PARAMS *params, CUcontext ctx);
CUresult corex_backend_graph_set_fill(CUgraphNode node, const CUDA_MEMSET_NODE_PARAMS *params);
CUresult corex_backend_graph_exec_set_fill(CUgraphExec exec, CUgraphNode node, const CUDA_MEMSET_NODE_PARAMS *params, CUcontext ctx);
CUresult corex_backend_graph_instantiate(CUgraphExec *exec, CUgraph graph);
CUresult corex_backend_graph_exec_destroy(CUgraphExec exec);
CUresult corex_backend_graph_launch(CUgraphExec exec, CUstream stream);
CUresult corex_backend_graph_nodes(CUgraph graph, CUgraphNode *nodes, size_t *count);
CUresult corex_backend_graph_roots(CUgraph graph, CUgraphNode *nodes, size_t *count);
CUresult corex_backend_graph_edges(CUgraph graph, CUgraphNode *from, CUgraphNode *to, size_t *count);
CUresult corex_backend_graph_dependencies(CUgraphNode node, CUgraphNode *nodes, size_t *count);
CUresult corex_backend_graph_dependents(CUgraphNode node, CUgraphNode *nodes, size_t *count);
CUresult corex_backend_graph_add_dependencies(CUgraph graph, const CUgraphNode *from, const CUgraphNode *to, size_t count);
CUresult corex_backend_graph_remove_dependencies(CUgraph graph, const CUgraphNode *from, const CUgraphNode *to, size_t count);
CUresult corex_backend_graph_destroy_node(CUgraphNode node);
CUresult corex_backend_graph_set_enabled(CUgraphExec exec, CUgraphNode node, unsigned int enabled);
CUresult corex_backend_graph_get_enabled(CUgraphExec exec, CUgraphNode node, unsigned int *enabled);

CUresult corex_backend_capture_begin(CUstream stream, CUstreamCaptureMode mode);
CUresult corex_backend_capture_end(CUstream stream, CUgraph *graph);
CUresult corex_backend_capture_info(CUstream stream, CUstreamCaptureStatus *status, cuuint64_t *id, CUgraph *graph, const CUgraphNode **deps, size_t *count);

CUresult corex_backend_graph_clone(CUgraph *clone, CUgraph graph);
CUresult corex_backend_graph_find_clone(CUgraphNode *node, CUgraphNode original, CUgraph clone);
CUresult corex_backend_graph_exec_update(CUgraphExec exec, CUgraph graph, CUgraphNode *error, CUgraphExecUpdateResult *result);
CUresult corex_backend_graph_add_record(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, CUevent event);
CUresult corex_backend_graph_add_wait(CUgraphNode *node, CUgraph graph, const CUgraphNode *deps, size_t count, CUevent event);

#endif
