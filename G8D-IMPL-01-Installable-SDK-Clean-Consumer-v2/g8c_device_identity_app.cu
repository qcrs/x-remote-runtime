#include <cuda_runtime.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" __global__
void g8c_identity_kernel(
    float *dst,
    const float *src,
    int n,
    float alpha)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        dst[i] = src[i] * alpha + 1.0f;
}

int main()
{
    int count = -1;
    cudaError_t rc = cudaGetDeviceCount(&count);
    if (rc != cudaSuccess || count != 1)
        return 2;

    struct cudaDeviceProp prop;
    memset(&prop, 0xA5, sizeof(prop));

    rc = cudaGetDeviceProperties(&prop, 0);
    printf(
        "G8C_APP_PROPERTIES rc=%d name=%s total=%zu "
        "shared=%zu regs=%d warp=%d maxThreads=%d "
        "cc=%d.%d sm=%d memClock=%d bus=%d l2=%d\n",
        (int)rc,
        prop.name,
        prop.totalGlobalMem,
        prop.sharedMemPerBlock,
        prop.regsPerBlock,
        prop.warpSize,
        prop.maxThreadsPerBlock,
        prop.major,
        prop.minor,
        prop.multiProcessorCount,
        prop.memoryClockRate,
        prop.memoryBusWidth,
        prop.l2CacheSize);

    if (rc != cudaSuccess)
        return 3;

    if (strcmp(prop.name, "Iluvatar MR-V100") != 0)
        return 4;

    if (prop.totalGlobalMem != 34359738368ULL ||
        prop.sharedMemPerBlock != 131072 ||
        prop.regsPerBlock != 262144 ||
        prop.warpSize != 64 ||
        prop.maxThreadsPerBlock != 4096 ||
        prop.maxThreadsDim[0] != 4096 ||
        prop.maxThreadsDim[1] != 4096 ||
        prop.maxThreadsDim[2] != 256 ||
        prop.maxGridSize[0] != 2147483647 ||
        prop.maxGridSize[1] != 65535 ||
        prop.maxGridSize[2] != 65535 ||
        prop.clockRate != 1500000 ||
        prop.totalConstMem != 16384 ||
        prop.major != 7 ||
        prop.minor != 1 ||
        prop.multiProcessorCount != 16 ||
        prop.memoryClockRate != 1600000 ||
        prop.memoryBusWidth != 2048 ||
        prop.l2CacheSize != 16777216 ||
        prop.maxThreadsPerMultiProcessor != 8192 ||
        prop.asyncEngineCount != 1 ||
        prop.unifiedAddressing != 1) {
        printf("G8C_PROPERTY_MISMATCH=YES\n");
        return 5;
    }

    struct cudaDeviceProp invalid_prop;
    rc = cudaGetDeviceProperties(&invalid_prop, 1);
    printf(
        "G8C_INVALID_DEVICE rc=%d expected=%d\n",
        (int)rc,
        (int)cudaErrorInvalidDevice);

    if (rc != cudaErrorInvalidDevice)
        return 6;

    cudaError_t sticky = cudaGetLastError();
    printf(
        "G8C_INVALID_DEVICE_LAST_ERROR rc=%d expected=%d\n",
        (int)sticky,
        (int)cudaErrorInvalidDevice);

    if (sticky != cudaErrorInvalidDevice)
        return 7;

    cudaError_t cleared = cudaPeekAtLastError();
    printf(
        "G8C_LAST_ERROR_CLEARED rc=%d expected=%d\n",
        (int)cleared,
        (int)cudaSuccess);

    if (cleared != cudaSuccess)
        return 8;

    size_t free_before = 0;
    size_t total_before = 0;

    rc = cudaMemGetInfo(&free_before, &total_before);
    printf(
        "G8C_MEM_BEFORE rc=%d free=%zu total=%zu\n",
        (int)rc,
        free_before,
        total_before);

    if (rc != cudaSuccess ||
        total_before != prop.totalGlobalMem ||
        free_before == 0 ||
        free_before > total_before)
        return 7;

    enum { N = 1024 };
    const size_t bytes = N * sizeof(float);

    float src[N];
    float dst[N];

    for (int i = 0; i < N; ++i) {
        src[i] = (float)(i + 1);
        dst[i] = -999.0f;
    }

    float *d_src = NULL;
    float *d_dst = NULL;

    rc = cudaMalloc((void **)&d_src, bytes);
    if (rc != cudaSuccess)
        return 8;

    rc = cudaMalloc((void **)&d_dst, bytes);
    if (rc != cudaSuccess)
        return 9;

    size_t free_after_alloc = 0;
    size_t total_after_alloc = 0;

    rc = cudaMemGetInfo(
        &free_after_alloc,
        &total_after_alloc);
    printf(
        "G8C_MEM_AFTER_ALLOC rc=%d free=%zu total=%zu delta=%lld\n",
        (int)rc,
        free_after_alloc,
        total_after_alloc,
        (long long)free_before -
            (long long)free_after_alloc);

    if (rc != cudaSuccess ||
        total_after_alloc != total_before ||
        free_after_alloc > total_after_alloc)
        return 10;

    rc = cudaMemcpy(
        d_src,
        src,
        bytes,
        cudaMemcpyHostToDevice);
    if (rc != cudaSuccess)
        return 11;

    float alpha = 2.0f;
    g8c_identity_kernel<<<dim3(4,1,1), dim3(256,1,1)>>>(
        d_dst,
        d_src,
        N,
        alpha);

    rc = cudaPeekAtLastError();
    if (rc != cudaSuccess)
        return 12;

    rc = cudaDeviceSynchronize();
    if (rc != cudaSuccess)
        return 13;

    rc = cudaMemcpy(
        dst,
        d_dst,
        bytes,
        cudaMemcpyDeviceToHost);
    if (rc != cudaSuccess)
        return 14;

    int failures = 0;
    for (int i = 0; i < N; ++i) {
        float expected = src[i] * alpha + 1.0f;
        if (fabsf(dst[i] - expected) > 1e-6f)
            ++failures;
    }

    printf(
        "G8C_NUMERICAL failures=%d\n",
        failures);

    cudaFree(d_dst);
    cudaFree(d_src);

    if (failures)
        return 15;

    printf("G8C_DEVICE_IDENTITY_RESULT=PASS\n");
    return 0;
}
