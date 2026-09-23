#include "corex_backend.h"

CUresult corex_backend_init(void) { return cuInit(0); }
CUresult corex_backend_driver_version(int *version) { return cuDriverGetVersion(version); }
CUresult corex_backend_device_count(int *count) { return cuDeviceGetCount(count); }
CUresult corex_backend_device_get(CUdevice *device, int ordinal) { return cuDeviceGet(device, ordinal); }
CUresult corex_backend_device_name(char *name, int length, CUdevice device) { return cuDeviceGetName(name, length, device); }
CUresult corex_backend_device_total_mem(size_t *bytes, CUdevice device) { return cuDeviceTotalMem(bytes, device); }
CUresult corex_backend_device_attribute(int *value, CUdevice_attribute attribute, CUdevice device) { return cuDeviceGetAttribute(value, attribute, device); }
CUresult corex_backend_context_create(CUcontext *context, unsigned int flags, CUdevice device) { return cuCtxCreate(context, flags, device); }
CUresult corex_backend_context_destroy(CUcontext context) { return cuCtxDestroy(context); }
CUresult corex_backend_context_sync(void) { return cuCtxSynchronize(); }
CUresult corex_backend_mem_info(size_t *free_bytes, size_t *total_bytes) { return cuMemGetInfo(free_bytes, total_bytes); }
CUresult corex_backend_mem_alloc(CUdeviceptr *pointer, size_t bytes) { return cuMemAlloc(pointer, bytes); }
CUresult corex_backend_mem_free(CUdeviceptr pointer) { return cuMemFree(pointer); }
CUresult corex_backend_host_alloc(void **pointer, size_t bytes) { return cuMemAllocHost(pointer, bytes); }
CUresult corex_backend_host_free(void *pointer) { return cuMemFreeHost(pointer); }
CUresult corex_backend_copy_h2d(CUdeviceptr dst, const void *src, size_t bytes) { return cuMemcpyHtoD(dst, src, bytes); }
CUresult corex_backend_copy_d2h(void *dst, CUdeviceptr src, size_t bytes) { return cuMemcpyDtoH(dst, src, bytes); }
CUresult corex_backend_copy_d2d(CUdeviceptr dst, CUdeviceptr src, size_t bytes) { return cuMemcpyDtoD(dst, src, bytes); }
CUresult corex_backend_memset_d8(CUdeviceptr dst, unsigned char value, size_t bytes) { return cuMemsetD8(dst, value, bytes); }
CUresult corex_backend_memset_d8_async(CUdeviceptr dst, unsigned char value, size_t bytes, CUstream stream) { return cuMemsetD8Async(dst, value, bytes, stream); }
CUresult corex_backend_copy_h2d_async(CUdeviceptr dst, const void *src, size_t bytes, CUstream stream) { return cuMemcpyHtoDAsync(dst, src, bytes, stream); }
CUresult corex_backend_copy_d2h_async(void *dst, CUdeviceptr src, size_t bytes, CUstream stream) { return cuMemcpyDtoHAsync(dst, src, bytes, stream); }
CUresult corex_backend_stream_create(CUstream *stream, unsigned int flags) { return cuStreamCreate(stream, flags); }
CUresult corex_backend_stream_destroy(CUstream stream) { return cuStreamDestroy(stream); }
CUresult corex_backend_stream_query(CUstream stream) { return cuStreamQuery(stream); }
CUresult corex_backend_stream_sync(CUstream stream) { return cuStreamSynchronize(stream); }
CUresult corex_backend_stream_wait_event(CUstream stream, CUevent event, unsigned int flags) { return cuStreamWaitEvent(stream, event, flags); }
CUresult corex_backend_event_create(CUevent *event, unsigned int flags) { return cuEventCreate(event, flags); }
CUresult corex_backend_event_destroy(CUevent event) { return cuEventDestroy(event); }
CUresult corex_backend_event_record(CUevent event, CUstream stream) { return cuEventRecord(event, stream); }
CUresult corex_backend_event_query(CUevent event) { return cuEventQuery(event); }
CUresult corex_backend_event_sync(CUevent event) { return cuEventSynchronize(event); }
CUresult corex_backend_module_load(CUmodule *module, const void *image) { return cuModuleLoadData(module, image); }
CUresult corex_backend_module_unload(CUmodule module) { return cuModuleUnload(module); }
CUresult corex_backend_module_function(CUfunction *function, CUmodule module, const char *name) { return cuModuleGetFunction(function, module, name); }
CUresult corex_backend_launch(CUfunction function, unsigned int grid_x, unsigned int grid_y, unsigned int grid_z, unsigned int block_x, unsigned int block_y, unsigned int block_z, unsigned int shared_bytes, CUstream stream, void **kernel_params, void **extra)
{
    return cuLaunchKernel(function, grid_x, grid_y, grid_z,
                          block_x, block_y, block_z, shared_bytes,
                          stream, kernel_params, extra);
}
