#include <cuda_runtime.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" __global__
void g8d_external_kernel(float *dst, const float *src, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n)
        dst[i] = src[i] * 5.0f + 3.0f;
}

int main(void)
{
    int count = -1;
    cudaError_t rc = cudaGetDeviceCount(&count);
    if (rc != cudaSuccess || count != 1)
        return 2;

    struct cudaDeviceProp prop;
    memset(&prop, 0, sizeof(prop));

    rc = cudaGetDeviceProperties(&prop, 0);
    if (rc != cudaSuccess)
        return 3;

    printf(
        "G8D_DEVICE name=%s total=%zu cc=%d.%d warp=%d\n",
        prop.name,
        prop.totalGlobalMem,
        prop.major,
        prop.minor,
        prop.warpSize);

    if (strcmp(prop.name, "Iluvatar MR-V100") != 0)
        return 4;

    size_t free_bytes = 0;
    size_t total_bytes = 0;

    rc = cudaMemGetInfo(&free_bytes, &total_bytes);
    if (rc != cudaSuccess ||
        total_bytes != prop.totalGlobalMem ||
        free_bytes == 0 ||
        free_bytes > total_bytes)
        return 5;

    enum { N = 256 };
    const size_t bytes = N * sizeof(float);

    float src[N];
    float dst[N];

    for (int i = 0; i < N; ++i) {
        src[i] = (float)(i + 1);
        dst[i] = -1.0f;
    }

    float *d_src = NULL;
    float *d_dst = NULL;

    rc = cudaMalloc((void **)&d_src, bytes);
    if (rc != cudaSuccess)
        return 6;

    rc = cudaMalloc((void **)&d_dst, bytes);
    if (rc != cudaSuccess)
        return 7;

    rc = cudaMemcpy(
        d_src,
        src,
        bytes,
        cudaMemcpyHostToDevice);
    if (rc != cudaSuccess)
        return 8;

    g8d_external_kernel<<<dim3(1,1,1), dim3(N,1,1)>>>(
        d_dst,
        d_src,
        N);

    rc = cudaPeekAtLastError();
    if (rc != cudaSuccess)
        return 9;

    rc = cudaDeviceSynchronize();
    if (rc != cudaSuccess)
        return 10;

    rc = cudaMemcpy(
        dst,
        d_dst,
        bytes,
        cudaMemcpyDeviceToHost);
    if (rc != cudaSuccess)
        return 11;

    int failures = 0;
    for (int i = 0; i < N; ++i) {
        float expected = src[i] * 5.0f + 3.0f;
        if (fabsf(dst[i] - expected) > 1e-6f)
            ++failures;
    }

    printf("G8D_NUMERICAL failures=%d\n", failures);

    cudaFree(d_dst);
    cudaFree(d_src);

    if (failures)
        return 12;

    printf("G8D_CLEAN_CONSUMER_RESULT=PASS\n");
    return 0;
}
