#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CUresult report(const char *name, CUresult r)
{
    const char *error = NULL;
    (void)cuGetErrorName(r, &error);
    printf("%s result=%d name=%s\n", name, (int)r, error ? error : "UNKNOWN");
    return r;
}
#define R(call) report(#call, (call))

static void host(void)
{
    for (unsigned flags = 0; flags <= 4; ++flags) {
        void *p = NULL; unsigned actual = 0; CUdeviceptr mapped = 0;
        char label[80];
        snprintf(label, sizeof(label), "cuMemHostAlloc(flags=%u)", flags);
        if (report(label, cuMemHostAlloc(&p, 4096, flags)) == CUDA_SUCCESS) {
            memset(p, 0x3c, 4096);
            R(cuMemHostGetFlags(&actual, p));
            printf("HOST_FLAGS requested=%u actual=%u\n", flags, actual);
            R(cuMemHostGetDevicePointer(&mapped, p, 0));
            CUdeviceptr device = 0; unsigned char out[4096];
            if (R(cuMemAlloc(&device, 4096)) == CUDA_SUCCESS) {
                R(cuMemcpyHtoD(device, p, 4096));
                R(cuMemcpyDtoH(out, device, 4096));
                printf("HOST_ROUNDTRIP=%s\n", memcmp(out, p, 4096) ? "FAIL" : "PASS");
                R(cuMemFree(device));
            }
            R(cuMemFreeHost(p));
        }
    }
    void *p = NULL;
    if (posix_memalign(&p, 4096, 4096)) return;
    memset(p, 0, 4096);
    if (R(cuMemHostRegister(p, 4096, 0)) == CUDA_SUCCESS) {
        CUdeviceptr mapped = 0;
        R(cuMemHostGetDevicePointer(&mapped, p, 0));
        R(cuMemHostUnregister(p));
    }
    free(p);
}

static void objects(CUstream stream)
{
    CUDA_ARRAY_DESCRIPTOR d = { .Width = 16, .Height = 4,
        .Format = CU_AD_FORMAT_UNSIGNED_INT8, .NumChannels = 1 };
    CUarray a = NULL, b = NULL;
    CUresult ar = R(cuArrayCreate(&a, &d));
    if (ar == CUDA_SUCCESS) {
        CUDA_ARRAY_DESCRIPTOR info = {0};
        R(cuArrayGetDescriptor(&info, a));
        printf("ARRAY_DESCRIPTOR_MATCH=%s\n", info.Width == 16 && info.Height == 4 &&
            info.Format == d.Format && info.NumChannels == 1 ? "PASS" : "FAIL");
        void *in = NULL, *out = NULL;
        if (R(cuMemAllocHost(&in, 64)) == CUDA_SUCCESS &&
            R(cuMemAllocHost(&out, 64)) == CUDA_SUCCESS) {
            memset(in, 0x5b, 64); memset(out, 0, 64);
            CUDA_MEMCPY2D c = {0};
            c.srcMemoryType = CU_MEMORYTYPE_HOST; c.srcHost = in; c.srcPitch = 16;
            c.dstMemoryType = CU_MEMORYTYPE_ARRAY; c.dstArray = a;
            c.WidthInBytes = 16; c.Height = 4;
            R(cuMemcpy2D(&c)); R(cuMemcpy2DAsync(&c, stream)); R(cuStreamSynchronize(stream));
            c.srcMemoryType = CU_MEMORYTYPE_ARRAY; c.srcArray = a; c.srcHost = NULL;
            c.dstMemoryType = CU_MEMORYTYPE_HOST; c.dstArray = NULL; c.dstHost = out; c.dstPitch = 16;
            R(cuMemcpy2D(&c)); R(cuMemcpy2DAsync(&c, stream)); R(cuStreamSynchronize(stream));
            printf("ARRAY_ROUNDTRIP=%s\n", memcmp(in, out, 64) ? "FAIL" : "PASS");
            if (R(cuArrayCreate(&b, &d)) == CUDA_SUCCESS) {
                c.dstMemoryType = CU_MEMORYTYPE_ARRAY; c.dstArray = b; c.dstHost = NULL;
                R(cuMemcpy2D(&c)); R(cuArrayDestroy(b));
            }
            R(cuMemFreeHost(out)); R(cuMemFreeHost(in));
        }
        CUDA_RESOURCE_DESC res = {0}; CUDA_TEXTURE_DESC tex = {0};
        CUDA_RESOURCE_DESC got = {0}; CUDA_TEXTURE_DESC got_tex = {0};
        CUDA_RESOURCE_VIEW_DESC view = {0}; CUtexObject t = 0;
        res.resType = CU_RESOURCE_TYPE_ARRAY; res.res.array.hArray = a;
        tex.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
        tex.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
        tex.flags = CU_TRSF_READ_AS_INTEGER;
        if (R(cuTexObjectCreate(&t, &res, &tex, NULL)) == CUDA_SUCCESS) {
            R(cuTexObjectGetResourceDesc(&got, t));
            R(cuTexObjectGetTextureDesc(&got_tex, t));
            R(cuTexObjectGetResourceViewDesc(&view, t)); R(cuTexObjectDestroy(t));
        }
        R(cuArrayDestroy(a));
    } else printf("ARRAY_DEPENDENTS=BLOCKED_BY_CREATE result=%d\n", ar);
    CUDA_ARRAY3D_DESCRIPTOR d3 = { .Width = 16, .Height = 4, .Depth = 2,
        .Format = CU_AD_FORMAT_UNSIGNED_INT8, .NumChannels = 1 };
    if (R(cuArray3DCreate(&a, &d3)) == CUDA_SUCCESS) {
        CUDA_ARRAY3D_DESCRIPTOR got = {0}; R(cuArray3DGetDescriptor(&got, a));
        R(cuArrayDestroy(a));
    }
    d3.Depth = 0; d3.Flags = CUDA_ARRAY3D_SURFACE_LDST;
    if (R(cuArray3DCreate(&a, &d3)) == CUDA_SUCCESS) {
        CUDA_RESOURCE_DESC res = {0}, got = {0}; CUsurfObject s = 0;
        res.resType = CU_RESOURCE_TYPE_ARRAY; res.res.array.hArray = a;
        if (R(cuSurfObjectCreate(&s, &res)) == CUDA_SUCCESS) {
            R(cuSurfObjectGetResourceDesc(&got, s)); R(cuSurfObjectDestroy(s));
        }
        R(cuArrayDestroy(a));
    } else printf("SURFACE_DEPENDENTS=BLOCKED_BY_ARRAY_CREATE\n");
    d3.Flags = 0;
    CUmipmappedArray mip = NULL;
    if (R(cuMipmappedArrayCreate(&mip, &d3, 3)) == CUDA_SUCCESS) {
        R(cuMipmappedArrayGetLevel(&a, mip, 1)); R(cuMipmappedArrayDestroy(mip));
    } else printf("MIPMAP_DEPENDENTS=BLOCKED_BY_CREATE\n");
    /* Linear textures have no array prerequisite. */
    CUdeviceptr p = 0;
    if (R(cuMemAlloc(&p, 4096)) == CUDA_SUCCESS) {
        CUDA_RESOURCE_DESC res = {0}; CUDA_TEXTURE_DESC tex = {0}; CUtexObject t = 0;
        res.resType = CU_RESOURCE_TYPE_LINEAR; res.res.linear.devPtr = p;
        res.res.linear.format = CU_AD_FORMAT_UNSIGNED_INT8;
        res.res.linear.numChannels = 1; res.res.linear.sizeInBytes = 4096;
        tex.flags = CU_TRSF_READ_AS_INTEGER;
        if (R(cuTexObjectCreate(&t, &res, &tex, NULL)) == CUDA_SUCCESS) {
            CUDA_RESOURCE_DESC got = {0}; CUDA_TEXTURE_DESC got_tex = {0};
            CUDA_RESOURCE_VIEW_DESC got_view = {0};
            R(cuTexObjectGetResourceDesc(&got, t)); R(cuTexObjectGetTextureDesc(&got_tex, t));
            R(cuTexObjectGetResourceViewDesc(&got_view, t)); R(cuTexObjectDestroy(t));
        }
        R(cuMemFree(p));
    }
}

static void controls(CUstream stream)
{
    CUfunc_cache cache = CU_FUNC_CACHE_PREFER_NONE;
    R(cuCtxGetCacheConfig(&cache)); R(cuCtxSetCacheConfig(CU_FUNC_CACHE_PREFER_SHARED));
    CUfunc_cache actual = CU_FUNC_CACHE_PREFER_NONE; R(cuCtxGetCacheConfig(&actual));
    printf("CACHE_AFTER_SET=%d\n", actual); R(cuCtxSetCacheConfig(cache));
    for (int i = 0; i <= 3; ++i) {
        char label[64]; snprintf(label, sizeof(label), "cuCtxSetCacheConfig(%d)", i);
        report(label, cuCtxSetCacheConfig((CUfunc_cache)i));
    }
    R(cuCtxSetCacheConfig(cache));
    CUsharedconfig shared = CU_SHARED_MEM_CONFIG_DEFAULT_BANK_SIZE;
    R(cuCtxGetSharedMemConfig(&shared));
    R(cuCtxSetSharedMemConfig(CU_SHARED_MEM_CONFIG_FOUR_BYTE_BANK_SIZE));
    R(cuCtxSetSharedMemConfig(shared));
    for (int i = 0; i <= 2; ++i) {
        char label[64]; snprintf(label, sizeof(label), "cuCtxSetSharedMemConfig(%d)", i);
        report(label, cuCtxSetSharedMemConfig((CUsharedconfig)i));
    }
    R(cuCtxSetSharedMemConfig(shared));
    for (int i = 0; i <= 6; ++i) {
        size_t value = 0; char label[80];
        snprintf(label, sizeof(label), "cuCtxGetLimit(%d)", i);
        if (report(label, cuCtxGetLimit(&value, (CUlimit)i)) == CUDA_SUCCESS) {
            snprintf(label, sizeof(label), "cuCtxSetLimit(%d,value=%zu)", i, value);
            report(label, cuCtxSetLimit((CUlimit)i, value));
        }
    }
    CUdeviceptr managed = 0;
    if (R(cuMemAllocManaged(&managed, 4096, CU_MEM_ATTACH_GLOBAL)) == CUDA_SUCCESS) {
        R(cuMemAdvise(managed, 4096, CU_MEM_ADVISE_SET_READ_MOSTLY, 0));
        R(cuMemPrefetchAsync(managed, 4096, 0, stream)); R(cuStreamSynchronize(stream));
        int value = 0; void *values[] = {&value}; size_t sizes[] = {sizeof(value)};
        CUmem_range_attribute attrs[] = {CU_MEM_RANGE_ATTRIBUTE_READ_MOSTLY};
        R(cuMemRangeGetAttribute(&value, sizeof(value), attrs[0], managed, 4096));
        R(cuMemRangeGetAttributes(values, sizes, attrs, 1, managed, 4096));
        R(cuMemFree(managed));
    } else printf("MANAGED_DEPENDENTS=BLOCKED_BY_ALLOC\n");
    CUstreamAttrValue attr = {0};
    R(cuStreamGetAttribute(stream, CU_STREAM_ATTRIBUTE_SYNCHRONIZATION_POLICY, &attr));
    R(cuStreamSetAttribute(stream, CU_STREAM_ATTRIBUTE_SYNCHRONIZATION_POLICY, &attr));
    memset(&attr, 0, sizeof(attr));
    R(cuStreamGetAttribute(stream, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &attr));
    /* A zero-byte access-policy window disables the policy and needs no
     * managed pointer or unsupported allocation prerequisite. */
    memset(&attr, 0, sizeof(attr));
    R(cuStreamSetAttribute(stream, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &attr));
    CUstream other = NULL;
    if (R(cuStreamCreate(&other, 0)) == CUDA_SUCCESS) {
        CUdeviceptr window = 0;
        if (R(cuMemAlloc(&window, 4096)) == CUDA_SUCCESS) {
            CUstreamAttrValue requested = {0}, got;
            requested.accessPolicyWindow.base_ptr = (void *)(uintptr_t)window;
            requested.accessPolicyWindow.num_bytes = 4096;
            requested.accessPolicyWindow.hitRatio = 0.5f;
            requested.accessPolicyWindow.hitProp = CU_ACCESS_PROPERTY_PERSISTING;
            requested.accessPolicyWindow.missProp = CU_ACCESS_PROPERTY_STREAMING;
            R(cuStreamSetAttribute(stream, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &requested));
            requested.accessPolicyWindow.num_bytes = 32;
            requested.accessPolicyWindow.hitProp = CU_ACCESS_PROPERTY_NORMAL;
            requested.accessPolicyWindow.missProp = CU_ACCESS_PROPERTY_NORMAL;
            R(cuStreamSetAttribute(stream, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &requested));
            memset(&got, 0xa5, sizeof(got));
            R(cuStreamGetAttribute(stream, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &got));
            printf("ACCESS_WINDOW_READBACK base_matches=%d bytes=%zu hitRatio=%g hitProp=%d missProp=%d\n",
                got.accessPolicyWindow.base_ptr == requested.accessPolicyWindow.base_ptr,
                got.accessPolicyWindow.num_bytes, (double)got.accessPolicyWindow.hitRatio,
                got.accessPolicyWindow.hitProp, got.accessPolicyWindow.missProp);
            R(cuStreamCopyAttributes(other, stream));
            memset(&got, 0xa5, sizeof(got));
            R(cuStreamGetAttribute(other, CU_STREAM_ATTRIBUTE_ACCESS_POLICY_WINDOW, &got));
            printf("ACCESS_WINDOW_COPY_READBACK base_matches=%d bytes=%zu hitRatio=%g hitProp=%d missProp=%d\n",
                got.accessPolicyWindow.base_ptr == requested.accessPolicyWindow.base_ptr,
                got.accessPolicyWindow.num_bytes, (double)got.accessPolicyWindow.hitRatio,
                got.accessPolicyWindow.hitProp, got.accessPolicyWindow.missProp);
            R(cuMemFree(window));
        }
        R(cuStreamDestroy(other));
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    CUdevice device = 0; CUcontext ctx = NULL; CUstream stream = NULL;
    if (R(cuInit(0)) || R(cuDeviceGet(&device, 0)) || R(cuCtxCreate(&ctx, 0, device))) return 1;
    char name[128] = {0}; int version = 0;
    R(cuDeviceGetName(name, sizeof(name), device)); R(cuDriverGetVersion(&version));
    printf("DEVICE=%s DRIVER_VERSION=%d\n", name, version);
    if (R(cuStreamCreate(&stream, 0))) return 1;
    host(); objects(stream); controls(stream);
    R(cuStreamDestroy(stream)); R(cuCtxDestroy(ctx));
    puts("COMPAT_FAMILIES_PROBE=COMPLETE");
    return 0;
}
