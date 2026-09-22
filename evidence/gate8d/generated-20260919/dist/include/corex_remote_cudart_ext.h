#ifndef COREX_REMOTE_CUDART_EXT_H
#define COREX_REMOTE_CUDART_EXT_H

#include <stddef.h>
#include <stdint.h>
#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional controlled-registration extension.
 *
 * Ordinary compiler-transparent CUDA source does not need these APIs.
 * They are retained as a stable explicit-control surface and regression path.
 */

typedef struct corexRemoteModuleHandle *corexRemoteModule_t;

typedef enum corexRemoteKernelArgKind {
    COREX_REMOTE_KERNEL_ARG_DEVICE_PTR = 1,
    COREX_REMOTE_KERNEL_ARG_BY_VALUE   = 2
} corexRemoteKernelArgKind;

typedef struct corexRemoteKernelArgDesc {
    corexRemoteKernelArgKind kind;
    uint32_t size;
} corexRemoteKernelArgDesc;

cudaError_t corexRemoteModuleLoad(
    const char *cubin_path,
    corexRemoteModule_t *module_out);

cudaError_t corexRemoteModuleUnload(
    corexRemoteModule_t module);

cudaError_t corexRemoteRegisterKernel(
    corexRemoteModule_t module,
    const char *kernel_name,
    const corexRemoteKernelArgDesc *args,
    size_t argc,
    const void **func_out);

#ifdef __cplusplus
}
#endif

#endif
