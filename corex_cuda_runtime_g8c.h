#ifndef COREX_CUDA_RUNTIME_G8C_H
#define COREX_CUDA_RUNTIME_G8C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gate 7C application-facing source-level CUDA Runtime subset.
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
    cudaErrorInvalidDevice          = 10,
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

typedef struct dim3 {
    unsigned int x;
    unsigned int y;
    unsigned int z;
} dim3;

typedef struct corexCudaStreamHandle *cudaStream_t;
typedef struct corexCudaEventHandle  *cudaEvent_t;
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;

typedef enum corexRemoteKernelArgKind {
    COREX_REMOTE_KERNEL_ARG_DEVICE_PTR = 1,
    COREX_REMOTE_KERNEL_ARG_BY_VALUE   = 2
} corexRemoteKernelArgKind;

typedef struct corexRemoteKernelArgDesc {
    corexRemoteKernelArgKind kind;
    uint32_t size;
} corexRemoteKernelArgDesc;

cudaError_t cudaGetDeviceCount(int *count);
cudaError_t cudaGetDevice(int *device);
cudaError_t cudaSetDevice(int device);

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

cudaError_t cudaDeviceSynchronize(void);

cudaError_t cudaStreamCreate(cudaStream_t *pStream);
cudaError_t cudaStreamDestroy(cudaStream_t stream);
cudaError_t cudaStreamSynchronize(cudaStream_t stream);
cudaError_t cudaStreamQuery(cudaStream_t stream);

cudaError_t cudaEventCreate(cudaEvent_t *event);
cudaError_t cudaEventDestroy(cudaEvent_t event);
cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream);
cudaError_t cudaEventSynchronize(cudaEvent_t event);
cudaError_t cudaEventQuery(cudaEvent_t event);
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

#ifdef __cplusplus
}
#endif

#endif
