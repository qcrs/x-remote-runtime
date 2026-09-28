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
CUresult corex_backend_host_alloc(void **pointer, size_t bytes);
CUresult corex_backend_host_free(void *pointer);
CUresult corex_backend_copy_h2d(CUdeviceptr dst, const void *src, size_t bytes);
CUresult corex_backend_copy_d2h(void *dst, CUdeviceptr src, size_t bytes);
CUresult corex_backend_copy_d2d(CUdeviceptr dst, CUdeviceptr src, size_t bytes);
CUresult corex_backend_copy_d2d_async(CUdeviceptr dst, CUdeviceptr src, size_t bytes, CUstream stream);
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
CUresult corex_backend_launch(CUfunction function,
                              unsigned int grid_x, unsigned int grid_y,
                              unsigned int grid_z, unsigned int block_x,
                              unsigned int block_y, unsigned int block_z,
                              unsigned int shared_bytes, CUstream stream,
                              void **kernel_params, void **extra);

#endif
