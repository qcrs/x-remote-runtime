#ifndef COREX_CUDA_RUNTIME_H
#define COREX_CUDA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical application-facing source-level CUDA Runtime compatibility
 * declarations. Project-specific remote helpers live below this same facade
 * so every public declaration has one source of truth.
 *
 * This header intentionally exposes only CUDA-style API plus the controlled
 * module/kernel registration helper retained from Gate 6D. Remote object IDs,
 * CRX9 protocol details, hidden TransferIDs, and debug helpers are not part of
 * the application surface.
 */
typedef enum cudaError {
    cudaSuccess                      = 0,
    cudaErrorInvalidValue           = 1,
    cudaErrorMemoryAllocation       = 2,
    cudaErrorInitializationError    = 3,
    cudaErrorInvalidDevice          = 101,
    cudaErrorInvalidDevicePointer   = 17,
    cudaErrorInvalidMemcpyDirection = 21,
    cudaErrorInvalidResourceHandle  = 400,
    cudaErrorNotReady               = 600,
    cudaErrorNotSupported           = 801,
    cudaErrorUnknown                = 999
} cudaError_t;

typedef enum cudaMemcpyKind {
    cudaMemcpyHostToHost     = 0,
    cudaMemcpyHostToDevice   = 1,
    cudaMemcpyDeviceToHost   = 2,
    cudaMemcpyDeviceToDevice = 3,
    cudaMemcpyDefault        = 4
} cudaMemcpyKind;

/* Only values proven against CoreX 4.4 are exposed in this compatibility ABI. */
typedef enum cudaDeviceAttr {
    cudaDevAttrMaxThreadsPerBlock = 1,
    cudaDevAttrMaxSharedMemoryPerBlock = 8,
    cudaDevAttrWarpSize = 10,
    cudaDevAttrClockRate = 13,
    cudaDevAttrMultiProcessorCount = 16,
    cudaDevAttrMemoryClockRate = 36,
    cudaDevAttrGlobalMemoryBusWidth = 37,
    cudaDevAttrComputeCapabilityMajor = 75,
    cudaDevAttrComputeCapabilityMinor = 76
} cudaDeviceAttr;

typedef enum cudaLimit {
    cudaLimitStackSize = 0,
    cudaLimitPrintfFifoSize = 1,
    cudaLimitMallocHeapSize = 2,
    cudaLimitDevRuntimeSyncDepth = 3,
    cudaLimitDevRuntimePendingLaunchCount = 4,
    cudaLimitMaxL2FetchGranularity = 5,
    cudaLimitPersistingL2CacheSize = 6
} cudaLimit;

typedef enum cudaFuncAttribute {
    cudaFuncAttributeMaxDynamicSharedMemorySize = 8,
    cudaFuncAttributePreferredSharedMemoryCarveout = 9
} cudaFuncAttribute;

typedef enum cudaFuncCache {
    cudaFuncCachePreferNone = 0,
    cudaFuncCachePreferShared = 1,
    cudaFuncCachePreferL1 = 2,
    cudaFuncCachePreferEqual = 3
} cudaFuncCache;

typedef enum cudaSharedMemConfig {
    cudaSharedMemBankSizeDefault = 0,
    cudaSharedMemBankSizeFourByte = 1,
    cudaSharedMemBankSizeEightByte = 2
} cudaSharedMemConfig;

typedef struct dim3 {
    unsigned int x;
    unsigned int y;
    unsigned int z;
} dim3;

typedef struct corexCudaStreamHandle *cudaStream_t;
typedef struct corexCudaEventHandle  *cudaEvent_t;
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;

#define cudaStreamDefault 0x0u
#define cudaStreamNonBlocking 0x1u
#define cudaEventDefault 0x0u
#define cudaEventBlockingSync 0x1u
#define cudaEventDisableTiming 0x2u
#define cudaEventInterprocess 0x4u

typedef enum corexRemoteKernelArgKind {
    COREX_REMOTE_KERNEL_ARG_DEVICE_PTR = 1,
    COREX_REMOTE_KERNEL_ARG_BY_VALUE   = 2
} corexRemoteKernelArgKind;

typedef struct corexRemoteKernelArgDesc {
    corexRemoteKernelArgKind kind;
    uint32_t size;
} corexRemoteKernelArgDesc;

typedef struct cudaFuncAttributes {
    size_t sharedSizeBytes;
    size_t constSizeBytes;
    size_t localSizeBytes;
    int maxThreadsPerBlock;
    int numRegs;
    int ptxVersion;
    int binaryVersion;
    int cacheModeCA;
    int maxDynamicSharedSizeBytes;
    int preferredShmemCarveout;
} cudaFuncAttributes;

cudaError_t cudaGetDeviceCount(int *count);
cudaError_t cudaGetDevice(int *device);
cudaError_t cudaSetDevice(int device);
cudaError_t cudaDriverGetVersion(int *driverVersion);
cudaError_t cudaRuntimeGetVersion(int *runtimeVersion);
cudaError_t cudaDeviceGetAttribute(int *value, cudaDeviceAttr attr, int device);
cudaError_t cudaGetDeviceFlags(unsigned int *flags);
cudaError_t cudaDeviceGetStreamPriorityRange(int *leastPriority, int *greatestPriority);
cudaError_t cudaDeviceGetLimit(size_t *value, cudaLimit limit);
cudaError_t cudaDeviceGetCacheConfig(cudaFuncCache *config);
cudaError_t cudaDeviceGetSharedMemConfig(cudaSharedMemConfig *config);
cudaError_t cudaFuncGetAttributes(cudaFuncAttributes *attr, const void *func);
cudaError_t cudaFuncSetAttribute(const void *func, cudaFuncAttribute attr, int value);
cudaError_t cudaFuncSetCacheConfig(const void *func, cudaFuncCache config);
cudaError_t cudaOccupancyMaxActiveBlocksPerMultiprocessor(int *numBlocks, const void *func, int blockSize, size_t dynamicSMemSize);

cudaError_t cudaMalloc(void **devPtr, size_t size);
cudaError_t cudaFree(void *devPtr);

cudaError_t cudaMemcpy(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind);

cudaError_t cudaMemcpyAsync(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind,
    cudaStream_t stream);

cudaError_t cudaMemset(void *devPtr, int value, size_t count);
cudaError_t cudaMemsetAsync(
    void *devPtr, int value, size_t count, cudaStream_t stream);

cudaError_t cudaDeviceSynchronize(void);

cudaError_t cudaStreamCreate(cudaStream_t *pStream);
cudaError_t cudaStreamCreateWithFlags(cudaStream_t *pStream, unsigned int flags);
cudaError_t cudaStreamCreateWithPriority(cudaStream_t *pStream, unsigned int flags, int priority);
cudaError_t cudaStreamGetFlags(cudaStream_t stream, unsigned int *flags);
cudaError_t cudaStreamGetPriority(cudaStream_t stream, int *priority);
cudaError_t cudaStreamDestroy(cudaStream_t stream);
cudaError_t cudaStreamSynchronize(cudaStream_t stream);
cudaError_t cudaStreamQuery(cudaStream_t stream);

cudaError_t cudaEventCreate(cudaEvent_t *event);
cudaError_t cudaEventCreateWithFlags(cudaEvent_t *event, unsigned int flags);
cudaError_t cudaEventDestroy(cudaEvent_t event);
cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream);
cudaError_t cudaEventSynchronize(cudaEvent_t event);
cudaError_t cudaEventQuery(cudaEvent_t event);
cudaError_t cudaEventElapsedTime(float *ms, cudaEvent_t start, cudaEvent_t end);
cudaError_t cudaStreamWaitEvent(
    cudaStream_t stream,
    cudaEvent_t event,
    unsigned int flags);

/*
 * Controlled registration helper retained for compatibility/regression.
 * Gate 7C compiler-generated module registration is transparent and does not
 * require an application-facing API.
 */
cudaError_t corexRemoteModuleLoad(
    const char *cubin_path,
    corexRemoteModule_t *module_out);

cudaError_t corexRemoteModuleUnload(corexRemoteModule_t module);

cudaError_t corexRemoteRegisterKernel(
    corexRemoteModule_t module,
    const char *kernel_name,
    const corexRemoteKernelArgDesc *args,
    size_t argc,
    const void **func_out);

cudaError_t cudaLaunchKernel(
    const void *func,
    dim3 gridDim,
    dim3 blockDim,
    void **args,
    size_t sharedMem,
    cudaStream_t stream);

cudaError_t cudaGetLastError(void);
cudaError_t cudaPeekAtLastError(void);
const char *cudaGetErrorString(cudaError_t error);

/* Test/debug helpers, not CUDA Runtime API. */
size_t corexRemoteDebugLiveTransfers(void);
void corexRemoteRuntimeShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
