#define _GNU_SOURCE
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R(call) report(#call, (call))
static cudaError_t report(const char *name, cudaError_t r)
{
    printf("%s result=%d name=%s\n", name, (int)r, cudaGetErrorName(r));
    return r;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (R(cudaSetDevice(0))) return 1;
    struct cudaChannelFormatDesc desc = cudaCreateChannelDesc(8, 0, 0, 0, cudaChannelFormatKindUnsigned);
    printf("cudaCreateChannelDesc MATCH=%s\n", desc.x == 8 && desc.y == 0 && desc.z == 0 &&
        desc.w == 0 && desc.f == cudaChannelFormatKindUnsigned ? "PASS" : "FAIL");
    printf("NATIVE_CHANNEL_DESC x=%d y=%d z=%d w=%d kind=%d\n", desc.x, desc.y, desc.z, desc.w, desc.f);
    desc = (struct cudaChannelFormatDesc){8, 0, 0, 0, cudaChannelFormatKindUnsigned};
    printf("cudaGetErrorName(unrecognized)=%s\n", cudaGetErrorName((cudaError_t)123456));
    cudaArray_t a = NULL;
    if (R(cudaMallocArray(&a, &desc, 16, 4, 0)) == cudaSuccess) R(cudaFreeArray(a));
    struct cudaExtent extent = {16, 4, 2};
    if (R(cudaMalloc3DArray(&a, &desc, extent, 0)) == cudaSuccess) R(cudaFreeArray(a));
    extent.depth = 0;
    if (R(cudaMalloc3DArray(&a, &desc, extent, cudaArraySurfaceLoadStore)) == cudaSuccess) {
        struct cudaResourceDesc res = {0}; cudaSurfaceObject_t surf = 0;
        res.resType = cudaResourceTypeArray; res.res.array.array = a;
        if (R(cudaCreateSurfaceObject(&surf, &res)) == cudaSuccess) R(cudaDestroySurfaceObject(surf));
        R(cudaFreeArray(a));
    }
    cudaMipmappedArray_t mip = NULL;
    if (R(cudaMallocMipmappedArray(&mip, &desc, extent, 3, 0)) == cudaSuccess)
        R(cudaFreeMipmappedArray(mip));
    void *p = NULL;
    if (R(cudaMalloc(&p, 4096)) == cudaSuccess) {
        struct cudaResourceDesc res = {0}; struct cudaTextureDesc tex = {0}; cudaTextureObject_t t = 0;
        res.resType = cudaResourceTypeLinear; res.res.linear.devPtr = p;
        res.res.linear.desc = desc; res.res.linear.sizeInBytes = 4096;
        tex.readMode = cudaReadModeElementType;
        if (R(cudaCreateTextureObject(&t, &res, &tex, NULL)) == cudaSuccess) R(cudaDestroyTextureObject(t));
        struct cudaPointerAttributes attributes = {0};
        R(cudaPointerGetAttributes(&attributes, p));
        R(cudaFree(p));
    }
    if (R(cudaMallocManaged(&p, 4096, cudaMemAttachGlobal)) == cudaSuccess) R(cudaFree(p));
    unsigned char pageable[16]; struct cudaPointerAttributes unregistered = {0};
    R(cudaPointerGetAttributes(&unregistered, pageable));
    printf("PAGEABLE_ATTRIBUTES type=%d device=%d host_matches=%d\n",
        unregistered.type, unregistered.device, unregistered.hostPointer == pageable);
    for (unsigned flags = 0; flags <= 4; ++flags) {
        p = NULL; unsigned actual = 99; void *mapped = NULL;
        char label[64]; snprintf(label, sizeof(label), "cudaHostAlloc(flags=%u)", flags);
        if (report(label, cudaHostAlloc(&p, 4096, flags)) == cudaSuccess) {
            R(cudaHostGetFlags(&actual, p));
            printf("RUNTIME_HOST_FLAGS requested=%u actual=%u\n", flags, actual);
            R(cudaHostGetDevicePointer(&mapped, p, 0)); R(cudaFreeHost(p));
        }
    }
    if (R(cudaMallocHost(&p, 4096)) == cudaSuccess) R(cudaFreeHost(p));
    if (posix_memalign(&p, 4096, 4096)) return 1;
    if (R(cudaHostRegister(p, 4096, 0)) == cudaSuccess) R(cudaHostUnregister(p));
    free(p);
    enum cudaFuncCache cache;
    R(cudaDeviceGetCacheConfig(&cache)); R(cudaDeviceSetCacheConfig(cudaFuncCachePreferShared));
    R(cudaDeviceSetCacheConfig(cache));
    R(cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeFourByte));
    R(cudaDeviceSetSharedMemConfig(cudaSharedMemBankSizeDefault));
    size_t limit = 0;
    R(cudaDeviceGetLimit(&limit, cudaLimitPrintfFifoSize));
    R(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, limit));
    cudaStream_t s = NULL, t = NULL;
    if (R(cudaStreamCreate(&s)) || R(cudaStreamCreate(&t))) return 1;
    /* Header declares newer APIs which need not be present in CoreX's DSO. */
    typedef cudaError_t (*get_attr_t)(cudaStream_t, enum cudaStreamAttrID, union cudaStreamAttrValue *);
    typedef cudaError_t (*set_attr_t)(cudaStream_t, enum cudaStreamAttrID, const union cudaStreamAttrValue *);
    typedef cudaError_t (*copy_attr_t)(cudaStream_t, cudaStream_t);
    get_attr_t get_attr = (get_attr_t)dlsym(RTLD_DEFAULT, "cudaStreamGetAttribute");
    set_attr_t set_attr = (set_attr_t)dlsym(RTLD_DEFAULT, "cudaStreamSetAttribute");
    copy_attr_t copy_attr = (copy_attr_t)dlsym(RTLD_DEFAULT, "cudaStreamCopyAttributes");
    union cudaStreamAttrValue value = {0};
    if (get_attr) report("cudaStreamGetAttribute(sync-policy)", get_attr(s, cudaStreamAttributeSynchronizationPolicy, &value));
    else puts("cudaStreamGetAttribute SYMBOL=ABSENT");
    if (set_attr) report("cudaStreamSetAttribute(sync-policy)", set_attr(s, cudaStreamAttributeSynchronizationPolicy, &value));
    else puts("cudaStreamSetAttribute SYMBOL=ABSENT");
    if (get_attr) report("cudaStreamGetAttribute(access-policy-window)", get_attr(s, cudaStreamAttributeAccessPolicyWindow, &value));
    memset(&value, 0, sizeof(value));
    if (set_attr) report("cudaStreamSetAttribute(access-policy-window)", set_attr(s, cudaStreamAttributeAccessPolicyWindow, &value));
    if (copy_attr) report("cudaStreamCopyAttributes", copy_attr(t, s));
    else puts("cudaStreamCopyAttributes SYMBOL=ABSENT");
    if (set_attr && get_attr && R(cudaMalloc(&p, 4096)) == cudaSuccess) {
        memset(&value, 0, sizeof(value));
        value.accessPolicyWindow.base_ptr = p;
        value.accessPolicyWindow.num_bytes = 32;
        value.accessPolicyWindow.hitRatio = 0.5f;
        report("cudaStreamSetAttribute(nonempty-normal-window)",
               set_attr(s, cudaStreamAttributeAccessPolicyWindow, &value));
        memset(&value, 0xa5, sizeof(value));
        report("cudaStreamGetAttribute(window-readback)", get_attr(s, cudaStreamAttributeAccessPolicyWindow, &value));
        printf("RUNTIME_WINDOW_READBACK bytes=%zu base_matches=%d hitRatio=%g\n",
            value.accessPolicyWindow.num_bytes, value.accessPolicyWindow.base_ptr == p,
            (double)value.accessPolicyWindow.hitRatio);
        R(cudaFree(p));
    }
    R(cudaStreamDestroy(t)); R(cudaStreamDestroy(s));
    puts("COREX_RUNTIME_FAMILIES_PROBE=COMPLETE");
    return 0;
}
