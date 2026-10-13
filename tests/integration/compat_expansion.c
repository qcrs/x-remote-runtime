#define _GNU_SOURCE
#ifdef COREX_COMPAT_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

_Static_assert(sizeof(struct cudaPointerAttributes) == 40, "pointer attributes size");
_Static_assert(offsetof(struct cudaPointerAttributes, type) == 4, "pointer attributes type");
_Static_assert(offsetof(struct cudaPointerAttributes, devicePointer) == 16, "pointer attributes device");
_Static_assert(offsetof(struct cudaPointerAttributes, hostPointer) == 24, "pointer attributes host");
_Static_assert(offsetof(struct cudaPointerAttributes, isManaged) == 32, "pointer attributes managed");
_Static_assert(sizeof(union cudaStreamAttrValue) == 64, "stream attribute union size");
_Static_assert(sizeof(struct cudaAccessPolicyWindow) == 32, "access policy window size");
_Static_assert(offsetof(struct cudaAccessPolicyWindow, hitRatio) == 16, "access policy float offset");

#ifndef COREX_COMPAT_INTERNAL_ABI
#define CHECK(call) do { cudaError_t r = (call); if (r != cudaSuccess) { \
    printf("FAIL line=%d call=%s result=%d\n", __LINE__, #call, r); return 1; } } while (0)
#define EXPECT(call, error) do { cudaError_t r = (call); if (r != (error)) { \
    printf("FAIL line=%d call=%s result=%d expected=%d\n", __LINE__, #call, r, error); return 1; } } while (0)

static int host_memory(void)
{
    unsigned char *input = NULL, *output = NULL; void *device = NULL;
    cudaStream_t stream = NULL; unsigned flags = 99;
    CHECK(cudaMallocHost((void **)&input, 4096));
    CHECK(cudaHostAlloc((void **)&output, 4096, cudaHostAllocPortable));
    CHECK(cudaHostGetFlags(&flags, output + 17));
    if (flags != cudaHostAllocPortable) return 1;
    struct cudaPointerAttributes attr;
    CHECK(cudaPointerGetAttributes(&attr, output + 17));
    if (attr.type != cudaMemoryTypeHost || attr.hostPointer != output + 17 || attr.devicePointer) return 1;
    CHECK(cudaMalloc(&device, 4096)); CHECK(cudaStreamCreate(&stream));
    for (int i = 0; i < 4096; ++i) input[i] = (unsigned char)(i + i / 17);
    memset(output, 0, 4096);
    CHECK(cudaMemcpyAsync(device, input, 4096, cudaMemcpyHostToDevice, stream));
    memset(input, 0, 4096); /* Submission owns the source snapshot. */
    CHECK(cudaMemcpyAsync(output, device, 4096, cudaMemcpyDeviceToHost, stream));
    CHECK(cudaFreeHost(input)); /* Drains pending transfer retirement. */
    for (int i = 0; i < 4096; ++i) if (output[i] != (unsigned char)(i + i / 17)) return 1;
    CHECK(cudaPointerGetAttributes(&attr, (char *)device + 11));
    if (attr.type != cudaMemoryTypeDevice || attr.devicePointer != (char *)device + 11 || attr.hostPointer) return 1;
    void *mapped = (void *)1;
    EXPECT(cudaHostGetDevicePointer(&mapped, output, 0), cudaErrorNotSupported);
    if (mapped) return 1;
    void *unsupported = (void *)1;
    EXPECT(cudaHostAlloc(&unsupported, 4096, cudaHostAllocMapped), cudaErrorNotSupported);
    if (unsupported) return 1;
    EXPECT(cudaHostAlloc(&unsupported, 4096, cudaHostAllocWriteCombined), cudaErrorNotSupported);
    EXPECT(cudaFreeHost(output + 1), cudaErrorInvalidValue);
    CHECK(cudaFreeHost(output)); CHECK(cudaFreeHost(NULL));
    EXPECT(cudaFreeHost(output), cudaErrorInvalidValue);
    CHECK(cudaFree(device));
    EXPECT(cudaPointerGetAttributes(&attr, device), cudaErrorInvalidValue);
    CHECK(cudaStreamDestroy(stream));

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *registered = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (registered == MAP_FAILED) return 1;
    CHECK(cudaHostRegister(registered, page * 2, cudaHostRegisterPortable));
    EXPECT(cudaHostRegister(registered + page, page, 0), cudaErrorHostMemoryAlreadyRegistered);
    EXPECT(cudaFreeHost(registered), cudaErrorInvalidValue);
    EXPECT(cudaHostRegister(registered + 1, 16, 0), cudaErrorHostMemoryAlreadyRegistered);
    CHECK(cudaHostGetFlags(&flags, registered));
    if (flags != cudaHostRegisterPortable) return 1;
    CHECK(cudaMalloc(&device, 32)); memset(registered, 0x5c, 32);
    CHECK(cudaMemcpy(device, registered, 32, cudaMemcpyHostToDevice));
    memset(registered, 0, 32);
    CHECK(cudaMemcpy(registered, device, 32, cudaMemcpyDeviceToHost));
    for (int i = 0; i < 32; ++i) if (registered[i] != 0x5c) return 1;
    CHECK(cudaHostUnregister(registered));
    EXPECT(cudaHostUnregister(registered), cudaErrorHostMemoryNotRegistered);
    CHECK(cudaPointerGetAttributes(&attr, registered));
    if (attr.type != cudaMemoryTypeUnregistered || attr.device != -1) return 1;
    munmap(registered, page * 2); CHECK(cudaFree(device));
    /* A failed OS page lock must fail the allocation, not claim pinning. */
    struct rlimit saved, zero = {0, 0};
    if (getrlimit(RLIMIT_MEMLOCK, &saved)) return 1;
    zero.rlim_max = saved.rlim_max;
    if (setrlimit(RLIMIT_MEMLOCK, &zero)) return 1;
    EXPECT(cudaMallocHost(&unsupported, page), cudaErrorMemoryAllocation);
    if (unsupported) return 1;
    if (setrlimit(RLIMIT_MEMLOCK, &saved)) return 1;
    puts("HOST_MEMORY_STAGING_POINTERS_AND_NEGATIVES=PASS");
    return 0;
}

static int controls(void)
{
    enum cudaFuncCache saved, actual;
    CHECK(cudaDeviceGetCacheConfig(&saved));
    CHECK(cudaDeviceSetCacheConfig(cudaFuncCachePreferShared));
    CHECK(cudaDeviceGetCacheConfig(&actual));
    if (actual != cudaFuncCachePreferShared) return 1;
    CHECK(cudaDeviceSetCacheConfig(saved));
    CHECK(cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeFourByte));
    CHECK(cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeDefault));
    size_t value = 0;
    CHECK(cudaDeviceGetLimit(&value, cudaLimitPrintfFifoSize));
    CHECK(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, value));
    size_t actual_limit = 0; CHECK(cudaDeviceGetLimit(&actual_limit, cudaLimitPrintfFifoSize));
    if (actual_limit != value) return 1;
    EXPECT(cudaDeviceSetLimit(cudaLimitStackSize, 1024), cudaErrorNotSupported);
    EXPECT(cudaDeviceSetCacheConfig((enum cudaFuncCache)99), cudaErrorInvalidValue);
    EXPECT(cudaDeviceSetSharedMemConfig((enum cudaSharedMemConfig)99), cudaErrorInvalidValue);
    cudaStream_t a = NULL, b = NULL;
    CHECK(cudaStreamCreate(&a)); CHECK(cudaStreamCreate(&b));
    union cudaStreamAttrValue window = {0}, got;
    CHECK(cudaStreamSetAttribute(a, cudaStreamAttributeAccessPolicyWindow, &window));
    memset(&got, 0xa5, sizeof(got));
    CHECK(cudaStreamGetAttribute(a, cudaStreamAttributeAccessPolicyWindow, &got));
    if (got.accessPolicyWindow.base_ptr || got.accessPolicyWindow.num_bytes ||
        got.accessPolicyWindow.hitRatio != 0 || got.accessPolicyWindow.hitProp != cudaAccessPropertyNormal ||
        got.accessPolicyWindow.missProp != cudaAccessPropertyNormal) return 1;
    void *allocation = NULL; CHECK(cudaMalloc(&allocation, 4096));
    window.accessPolicyWindow.base_ptr = allocation;
    window.accessPolicyWindow.num_bytes = 32;
    window.accessPolicyWindow.hitRatio = 0.5f;
    EXPECT(cudaStreamSetAttribute(a, cudaStreamAttributeAccessPolicyWindow, &window), cudaErrorInvalidValue);
    /* Nonempty windows are rejected by the measured CoreX backend. */
    CHECK(cudaFree(allocation));
    memset(&window, 0, sizeof(window));
    EXPECT(cudaStreamSetAttribute(a, cudaStreamAttributeSynchronizationPolicy, &window), cudaErrorNotSupported);
    EXPECT(cudaStreamGetAttribute(a, cudaStreamAttributeSynchronizationPolicy, &got), cudaErrorNotSupported);
    CHECK(cudaStreamCopyAttributes(b, a)); CHECK(cudaStreamCopyAttributes(NULL, a));
    CHECK(cudaStreamGetAttribute(b, cudaStreamAttributeAccessPolicyWindow, &got));
    if (got.accessPolicyWindow.num_bytes || got.accessPolicyWindow.base_ptr) return 1;
    CHECK(cudaStreamDestroy(a));
    EXPECT(cudaStreamCopyAttributes(b, a), cudaErrorInvalidResourceHandle);
    EXPECT(cudaStreamGetAttribute(a, cudaStreamAttributeAccessPolicyWindow, &got), cudaErrorInvalidResourceHandle);
    CHECK(cudaStreamDestroy(b));
    puts("DEVICE_CONTROL_AND_STREAM_COPY=PASS");
    return 0;
}

int main(void)
{
    if (host_memory() || controls()) return 1;
    struct cudaChannelFormatDesc d = cudaCreateChannelDesc(8, 16, 8, 0, cudaChannelFormatKindUnsigned);
    if (d.x != 8 || d.y != 16 || d.z != 8 || d.w || d.f != cudaChannelFormatKindUnsigned) return 1;
    if (strcmp(cudaGetErrorName(cudaErrorHostMemoryAlreadyRegistered), "cudaErrorHostMemoryAlreadyRegistered") ||
        strcmp(cudaGetErrorName(cudaErrorNotSupported), "cudaErrorNotSupported") ||
        strcmp(cudaGetErrorName((cudaError_t)123456), "unrecognized error code")) return 1;
    puts("CHANNEL_DESC_AND_ERROR_NAMES=PASS");
    puts("REMOTE_COMPAT_EXPANSION=PASS");
    return 0;
}
#endif
