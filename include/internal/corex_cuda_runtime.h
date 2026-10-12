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
    cudaErrorInvalidSymbol          = 13,
    cudaErrorInvalidMemcpyDirection = 21,
    cudaErrorInvalidResourceHandle  = 400,
    cudaErrorSymbolNotFound         = 500,
    cudaErrorNotReady               = 600,
    cudaErrorHostMemoryAlreadyRegistered = 712,
    cudaErrorHostMemoryNotRegistered = 713,
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
    cudaDevAttrComputeCapabilityMinor = 76,
    cudaDevAttrMemoryPoolsSupported = 115
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
typedef struct corexCudaPoolHandle *cudaMemPool_t;
enum cudaMemPoolAttr {
    cudaMemPoolReuseFollowEventDependencies = 1,
    cudaMemPoolReuseAllowOpportunistic = 2,
    cudaMemPoolReuseAllowInternalDependencies = 3,
    cudaMemPoolAttrReleaseThreshold = 4,
    cudaMemPoolAttrReservedMemCurrent = 5,
    cudaMemPoolAttrReservedMemHigh = 6,
    cudaMemPoolAttrUsedMemCurrent = 7,
    cudaMemPoolAttrUsedMemHigh = 8,
};
enum cudaMemLocationType { cudaMemLocationTypeInvalid = 0, cudaMemLocationTypeDevice = 1 };
enum cudaMemAccessFlags { cudaMemAccessFlagsProtNone = 0, cudaMemAccessFlagsProtRead = 1, cudaMemAccessFlagsProtReadWrite = 3 };
enum cudaMemAllocationType { cudaMemAllocationTypeInvalid = 0, cudaMemAllocationTypePinned = 1 };
enum cudaMemAllocationHandleType { cudaMemHandleTypeNone = 0, cudaMemHandleTypePosixFileDescriptor = 1 };
struct cudaMemLocation { enum cudaMemLocationType type; int id; };
struct cudaMemAccessDesc { struct cudaMemLocation location; enum cudaMemAccessFlags flags; };
struct cudaMemPoolProps {
    enum cudaMemAllocationType allocType;
    enum cudaMemAllocationHandleType handleTypes;
    struct cudaMemLocation location;
    void *win32SecurityAttributes;
    unsigned char reserved[64];
};
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

typedef struct cudaExtent { size_t width, height, depth; } cudaExtent;
typedef struct cudaPos { size_t x, y, z; } cudaPos;
typedef struct cudaPitchedPtr { void *ptr; size_t pitch, xsize, ysize; } cudaPitchedPtr;
typedef struct cudaMemcpy3DParms {
    const void *srcArray;
    cudaPos srcPos;
    cudaPitchedPtr srcPtr;
    void *dstArray;
    cudaPos dstPos;
    cudaPitchedPtr dstPtr;
    cudaExtent extent;
    cudaMemcpyKind kind;
} cudaMemcpy3DParms;

typedef enum cudaMemoryType {
    cudaMemoryTypeUnregistered = 0, cudaMemoryTypeHost = 1,
    cudaMemoryTypeDevice = 2, cudaMemoryTypeManaged = 3
} cudaMemoryType;
typedef struct cudaPointerAttributes {
    cudaMemoryType memoryType;
    cudaMemoryType type;
    int device;
    void *devicePointer;
    void *hostPointer;
    int isManaged;
} cudaPointerAttributes;

typedef enum cudaStreamAttrID {
    cudaStreamAttributeAccessPolicyWindow = 1,
    cudaStreamAttributeSynchronizationPolicy = 3
} cudaStreamAttrID;
typedef enum cudaAccessProperty {
    cudaAccessPropertyNormal = 0, cudaAccessPropertyStreaming = 1,
    cudaAccessPropertyPersisting = 2
} cudaAccessProperty;
typedef struct cudaAccessPolicyWindow {
    void *base_ptr;
    size_t num_bytes;
    float hitRatio;
    cudaAccessProperty hitProp, missProp;
} cudaAccessPolicyWindow;
typedef union cudaStreamAttrValue {
    char pad[64];
    cudaAccessPolicyWindow accessPolicyWindow;
    int syncPolicy;
} cudaStreamAttrValue;
cudaError_t cudaStreamGetAttribute(cudaStream_t stream, cudaStreamAttrID attr, cudaStreamAttrValue *value);
cudaError_t cudaStreamSetAttribute(cudaStream_t stream, cudaStreamAttrID attr, const cudaStreamAttrValue *value);

cudaError_t cudaMallocHost(void **ptr, size_t size);
cudaError_t cudaHostAlloc(void **ptr, size_t size, unsigned int flags);
cudaError_t cudaFreeHost(void *ptr);
cudaError_t cudaHostRegister(void *ptr, size_t size, unsigned int flags);
cudaError_t cudaHostUnregister(void *ptr);
cudaError_t cudaHostGetFlags(unsigned int *flags, void *ptr);
cudaError_t cudaHostGetDevicePointer(void **device, void *host, unsigned int flags);
cudaError_t cudaPointerGetAttributes(cudaPointerAttributes *attributes, const void *ptr);
const char *cudaGetErrorName(cudaError_t error);

cudaError_t cudaMalloc3D(cudaPitchedPtr *pitchedDevPtr, cudaExtent extent);

cudaError_t cudaDeviceSetLimit(cudaLimit limit, size_t value);
cudaError_t cudaDeviceSetCacheConfig(cudaFuncCache config);
cudaError_t cudaDeviceSetSharedMemConfig(cudaSharedMemConfig config);
cudaError_t cudaStreamCopyAttributes(cudaStream_t dst, cudaStream_t src);

cudaError_t cudaGetDeviceCount(int *count);
cudaError_t cudaGetDevice(int *device);
cudaError_t cudaSetDevice(int device);
cudaError_t cudaDriverGetVersion(int *driverVersion);
cudaError_t cudaRuntimeGetVersion(int *runtimeVersion);
cudaError_t cudaDeviceGetAttribute(int *value, cudaDeviceAttr attr, int device);
cudaError_t cudaDeviceGetPCIBusId(char *pciBusId, int len, int device);
cudaError_t cudaDeviceGetByPCIBusId(int *device, const char *pciBusId);
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
cudaError_t cudaMallocPitch(void **devPtr, size_t *pitch, size_t width, size_t height);
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
cudaError_t cudaMemcpy2D(void *dst, size_t dpitch, const void *src, size_t spitch,
                         size_t width, size_t height, cudaMemcpyKind kind);
cudaError_t cudaMemcpy2DAsync(void *dst, size_t dpitch, const void *src, size_t spitch,
                              size_t width, size_t height, cudaMemcpyKind kind,
                              cudaStream_t stream);
cudaError_t cudaMemcpy3D(const cudaMemcpy3DParms *p);
cudaError_t cudaMemcpy3DAsync(const cudaMemcpy3DParms *p, cudaStream_t stream);

cudaError_t cudaMemset(void *devPtr, int value, size_t count);
cudaError_t cudaMemsetAsync(
    void *devPtr, int value, size_t count, cudaStream_t stream);

cudaError_t cudaDeviceSynchronize(void);
cudaError_t cudaDeviceReset(void);
cudaError_t cudaThreadExit(void);
cudaError_t cudaSetDeviceFlags(unsigned int flags);
cudaError_t cudaGetDriverEntryPoint(const char *symbol, void **entry, unsigned long long flags);
cudaError_t cudaConfigureCall(dim3 grid, dim3 block, size_t shared, cudaStream_t stream);
cudaError_t cudaLaunchHostFunc(cudaStream_t stream, void (*function)(void *), void *data);
cudaError_t cudaStreamAddCallback(cudaStream_t stream,
    void (*callback)(cudaStream_t, cudaError_t, void *), void *data, unsigned int flags);
cudaError_t cudaDeviceGetDefaultMemPool(cudaMemPool_t *pool, int device);
cudaError_t cudaDeviceGetMemPool(cudaMemPool_t *pool, int device);
cudaError_t cudaDeviceSetMemPool(int device, cudaMemPool_t pool);
cudaError_t cudaMemPoolCreate(cudaMemPool_t *pool, const struct cudaMemPoolProps *props);
cudaError_t cudaMemPoolDestroy(cudaMemPool_t pool);
cudaError_t cudaMemPoolTrimTo(cudaMemPool_t pool, size_t bytes);
cudaError_t cudaMemPoolGetAttribute(cudaMemPool_t pool, enum cudaMemPoolAttr attr, void *value);
cudaError_t cudaMemPoolSetAttribute(cudaMemPool_t pool, enum cudaMemPoolAttr attr, void *value);
cudaError_t cudaMemPoolGetAccess(enum cudaMemAccessFlags *flags, cudaMemPool_t pool, struct cudaMemLocation *location);
cudaError_t cudaMemPoolSetAccess(cudaMemPool_t pool, const struct cudaMemAccessDesc *desc, size_t count);
cudaError_t cudaMallocAsync(void **ptr, size_t bytes, cudaStream_t stream);
cudaError_t cudaMallocFromPoolAsync(void **ptr, size_t bytes, cudaMemPool_t pool, cudaStream_t stream);
cudaError_t cudaFreeAsync(void *ptr, cudaStream_t stream);

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
cudaError_t cudaMemset2D(void *devPtr, size_t pitch, int value, size_t width, size_t height);
cudaError_t cudaMemset2DAsync(void *devPtr, size_t pitch, int value, size_t width,
                              size_t height, cudaStream_t stream);

cudaError_t cudaGetLastError(void);
cudaError_t cudaPeekAtLastError(void);
const char *cudaGetErrorString(cudaError_t error);

/* Test/debug helpers, not CUDA Runtime API. */
size_t corexRemoteDebugLiveTransfers(void);
void corexRemoteRuntimeShutdown(void);

cudaError_t cudaMemset3D(cudaPitchedPtr ptr, int value, cudaExtent extent);
cudaError_t cudaMemset3DAsync(cudaPitchedPtr ptr, int value, cudaExtent extent, cudaStream_t stream);
cudaError_t cudaFuncSetSharedMemConfig(const void *func, cudaSharedMemConfig config);
cudaError_t cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(int *blocks, const void *func, int block_size, size_t dynamic_shared, unsigned int flags);
cudaError_t cudaOccupancyMaxPotentialBlockSize(int *grid, int *block, const void *func, size_t dynamic_shared, int block_limit);

typedef struct corexRemoteModuleHandle *cudaLibrary_t;
typedef struct corexRemoteKernelHandle *cudaKernel_t;
typedef struct corexRemoteKernelHandle *cudaFunction_t;
cudaError_t cudaGetFuncBySymbol(cudaFunction_t *function,const void *symbol);
cudaError_t cudaGetKernel(cudaKernel_t *kernel,const void *symbol);
/* Option arrays are rejected when nonempty in this compiled-image slice. */
cudaError_t cudaLibraryLoadFromFile(cudaLibrary_t *library, const char *file, int *jit_options, void **jit_values, unsigned int jit_count, int *library_options, void **library_values, unsigned int library_count);
cudaError_t cudaLibraryLoadData(cudaLibrary_t *library, const void *code, int *jit_options, void **jit_values, unsigned int jit_count, int *library_options, void **library_values, unsigned int library_count);
cudaError_t cudaLibraryUnload(cudaLibrary_t library);
cudaError_t cudaLibraryGetKernel(cudaKernel_t *kernel, cudaLibrary_t library, const char *name);
cudaError_t cudaLibraryGetKernelCount(unsigned int *count, cudaLibrary_t library);
cudaError_t cudaLibraryEnumerateKernels(cudaKernel_t *kernels, unsigned int count, cudaLibrary_t library);

cudaError_t cudaSetValidDevices(int *devices,int length);
cudaError_t cudaThreadSynchronize(void);
cudaError_t cudaThreadGetLimit(size_t *value,cudaLimit limit);
cudaError_t cudaThreadSetLimit(cudaLimit limit,size_t value);
cudaError_t cudaThreadGetCacheConfig(cudaFuncCache *config);
cudaError_t cudaThreadSetCacheConfig(cudaFuncCache config);

void __cudaRegisterVar(void **handle,char *host_var,char *device_address,const char *device_name,int ext,size_t size,int constant,int global);
cudaError_t cudaGetSymbolAddress(void **pointer,const void *symbol);
cudaError_t cudaGetSymbolSize(size_t *size,const void *symbol);
cudaError_t cudaMemcpyToSymbol(const void *symbol,const void *src,size_t count,size_t offset,cudaMemcpyKind kind);
cudaError_t cudaMemcpyFromSymbol(void *dst,const void *symbol,size_t count,size_t offset,cudaMemcpyKind kind);
cudaError_t cudaMemcpyToSymbolAsync(const void *symbol,const void *src,size_t count,size_t offset,cudaMemcpyKind kind,cudaStream_t stream);
cudaError_t cudaMemcpyFromSymbolAsync(void *dst,const void *symbol,size_t count,size_t offset,cudaMemcpyKind kind,cudaStream_t stream);

#ifdef __cplusplus
}
#endif

#endif
