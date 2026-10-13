#include <cuda_runtime_api.h>
#include <cuda.h>

#include "corex_device_info.h"

#include <stdio.h>
#include <string.h>

/* These queried Driver entry points share the Runtime's single implicit
 * context, fake pointers and transport. No native Driver pointers escape. */
static CUresult driver_result(cudaError_t result)
{
    switch (result) {
    case cudaSuccess: return CUDA_SUCCESS;
    case cudaErrorInvalidValue:
    case cudaErrorInvalidDevicePointer:
    case cudaErrorInvalidMemcpyDirection: return CUDA_ERROR_INVALID_VALUE;
    case cudaErrorMemoryAllocation: return CUDA_ERROR_OUT_OF_MEMORY;
    case cudaErrorInitializationError: return CUDA_ERROR_NOT_INITIALIZED;
    case cudaErrorInvalidDevice: return CUDA_ERROR_INVALID_DEVICE;
    case cudaErrorInvalidResourceHandle: return CUDA_ERROR_INVALID_HANDLE;
    case cudaErrorNotSupported: return CUDA_ERROR_NOT_SUPPORTED;
    default: return CUDA_ERROR_UNKNOWN;
    }
}
static CUresult CUDAAPI entry_mem_alloc(CUdeviceptr *out, size_t bytes)
{
    if (!out || !bytes) return CUDA_ERROR_INVALID_VALUE;
    void *ptr = NULL;
    cudaError_t result = cudaMalloc(&ptr, bytes);
    *out = (CUdeviceptr)(uintptr_t)ptr;
    return driver_result(result);
}
static CUresult CUDAAPI entry_mem_free(CUdeviceptr ptr)
{
    if (!ptr) return CUDA_SUCCESS;
    return driver_result(cudaFree((void *)(uintptr_t)ptr));
}
static CUresult CUDAAPI entry_htod(CUdeviceptr dst, const void *src, size_t bytes)
{
    return driver_result(cudaMemcpy((void *)(uintptr_t)dst, src, bytes, cudaMemcpyHostToDevice));
}
static CUresult CUDAAPI entry_dtoh(void *dst, CUdeviceptr src, size_t bytes)
{
    return driver_result(cudaMemcpy(dst, (void *)(uintptr_t)src, bytes, cudaMemcpyDeviceToHost));
}
static CUresult CUDAAPI entry_dtod(CUdeviceptr dst, CUdeviceptr src, size_t bytes)
{
    return driver_result(cudaMemcpy((void *)(uintptr_t)dst, (void *)(uintptr_t)src, bytes, cudaMemcpyDeviceToDevice));
}
static CUresult CUDAAPI entry_context_sync(void)
{
    return driver_result(cudaDeviceSynchronize());
}
cudaError_t cudaGetDriverEntryPoint(const char *symbol, void **out,
                                  unsigned long long flags)
{
    if (!out || !symbol)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidValue);
    *out = NULL;
    if (flags == cudaEnablePerThreadDefaultStream)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorNotSupported);
    if (flags > cudaEnablePerThreadDefaultStream)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidValue);
    void *entry = NULL;
    if (!strcmp(symbol, "cuMemAlloc")) entry = (void *)entry_mem_alloc;
    else if (!strcmp(symbol, "cuMemFree")) entry = (void *)entry_mem_free;
    else if (!strcmp(symbol, "cuMemcpyHtoD")) entry = (void *)entry_htod;
    else if (!strcmp(symbol, "cuMemcpyDtoH")) entry = (void *)entry_dtoh;
    else if (!strcmp(symbol, "cuMemcpyDtoD")) entry = (void *)entry_dtod;
    else if (!strcmp(symbol, "cuCtxSynchronize")) entry = (void *)entry_context_sync;
    if (!entry)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorNotSupported);
    int device;
    cudaError_t result = cudaGetDevice(&device);
    if (result == cudaSuccess) *out = entry;
    return result;
}

cudaError_t cudaLaunchKernelExC(const cudaLaunchConfig_t *config,
                               const void *func, void **args)
{
    if (!config || (config->numAttrs && !config->attrs))
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidValue);
    for (unsigned i = 0; i < config->numAttrs; ++i)
        if (config->attrs[i].id != cudaLaunchAttributeIgnore)
            return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorNotSupported);
    return cudaLaunchKernel(func, config->gridDim, config->blockDim, args,
                            config->dynamicSmemBytes, config->stream);
}

struct cudaChannelFormatDesc cudaCreateChannelDesc(
    int x, int y, int z, int w, enum cudaChannelFormatKind f)
{
    struct cudaChannelFormatDesc desc = {x, y, z, w, f};
    return desc;
}

cudaError_t cudaGetDeviceProperties(
    struct cudaDeviceProp *prop,
    int device)
{
    if (!prop)
        return (cudaError_t)corexRemoteRecordErrorInternal(
            (int)cudaErrorInvalidValue);

    if (device != 0)
        return (cudaError_t)corexRemoteRecordErrorInternal(
            (int)cudaErrorInvalidDevice);

    CorexRemoteDeviceInfo info;
    int rc = corexRemoteGetDeviceInfoInternal(
        (uint32_t)device,
        &info);

    if (rc != (int)cudaSuccess)
        return (cudaError_t)rc;

    /*
     * The caller owns a real CoreX-4.4 cudaDeviceProp object.
     * Never memcpy a wire/native-server struct into it.
     */
    memset(prop, 0, sizeof(*prop));

    size_t name_bytes = sizeof(info.name);
    if (name_bytes >= sizeof(prop->name))
        name_bytes = sizeof(prop->name) - 1;

    memcpy(prop->name, info.name, name_bytes);
    prop->name[name_bytes] = '\0';

    prop->totalGlobalMem = (size_t)info.total_global_mem;
    prop->sharedMemPerBlock = (size_t)info.shared_mem_per_block;
    prop->regsPerBlock = (int)info.regs_per_block;
    prop->warpSize = (int)info.warp_size;
    prop->memPitch = (size_t)info.mem_pitch;
    prop->maxThreadsPerBlock = (int)info.max_threads_per_block;

    for (int i = 0; i < 3; ++i) {
        prop->maxThreadsDim[i] = (int)info.max_threads_dim[i];
        prop->maxGridSize[i] = (int)info.max_grid_size[i];
    }

    prop->clockRate = (int)info.clock_rate;
    prop->totalConstMem = (size_t)info.total_const_mem;
    prop->major = (int)info.major;
    prop->minor = (int)info.minor;
    prop->textureAlignment = (size_t)info.texture_alignment;
    prop->deviceOverlap = (int)info.device_overlap;
    prop->multiProcessorCount = (int)info.multi_processor_count;
    prop->kernelExecTimeoutEnabled =
        (int)info.kernel_exec_timeout_enabled;
    prop->integrated = (int)info.integrated;
    prop->canMapHostMemory = (int)info.can_map_host_memory;
    prop->computeMode = (int)info.compute_mode;
    prop->concurrentKernels = (int)info.concurrent_kernels;
    prop->ECCEnabled = (int)info.ecc_enabled;
    prop->pciBusID = (int)info.pci_bus_id;
    prop->pciDeviceID = (int)info.pci_device_id;
    prop->tccDriver = (int)info.tcc_driver;
    prop->memoryClockRate = (int)info.memory_clock_rate;
    prop->memoryBusWidth = (int)info.memory_bus_width;
    prop->l2CacheSize = (int)info.l2_cache_size;
    prop->maxThreadsPerMultiProcessor =
        (int)info.max_threads_per_multiprocessor;
    prop->asyncEngineCount = (int)info.async_engine_count;
    prop->unifiedAddressing = (int)info.unified_addressing;

    printf(
        "G8C_DEVICE_PROPERTIES device=%d name=%s total=%zu "
        "cc=%d.%d sm=%d warp=%d result=PASS\n",
        device,
        prop->name,
        prop->totalGlobalMem,
        prop->major,
        prop->minor,
        prop->multiProcessorCount,
        prop->warpSize);

    return cudaSuccess;
}

cudaError_t cudaMemGetInfo(
    size_t *free_bytes,
    size_t *total_bytes)
{
    if (!free_bytes || !total_bytes)
        return (cudaError_t)corexRemoteRecordErrorInternal(
            (int)cudaErrorInvalidValue);

    CorexRemoteDeviceInfo info;
    int rc = corexRemoteGetDeviceInfoInternal(0, &info);

    if (rc != (int)cudaSuccess)
        return (cudaError_t)rc;

    *free_bytes = (size_t)info.free_mem;
    *total_bytes = (size_t)info.total_global_mem;

    printf(
        "G8C_MEM_INFO free=%zu total=%zu result=PASS\n",
        *free_bytes,
        *total_bytes);

    return cudaSuccess;
}

cudaError_t cudaChooseDevice(int *device,const struct cudaDeviceProp *properties)
{
    if(!device || !properties) return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidValue);
    /* The one configured logical device is the closest candidate for every
     * preference, including preferences larger than the native device. */
    return cudaGetDevice(device);
}

cudaError_t cudaDeviceCanAccessPeer(int *can_access, int device, int peer_device)
{
    if (!can_access)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidValue);
    if (device != 0 || peer_device != 0)
        return (cudaError_t)corexRemoteRecordErrorInternal(cudaErrorInvalidDevice);
    int count;
    cudaError_t result = cudaGetDeviceCount(&count);
    if (result == cudaSuccess) {
        /* The sole logical device cannot be its own peer. Both native
         * CoreX Runtime and Driver queries return success with zero. */
        *can_access = 0;
    }
    return result;
}
