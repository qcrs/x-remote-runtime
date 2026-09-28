#include "corex_cuda_runtime.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    void *device = NULL;
    size_t pitch = 0;
    if (cudaMallocPitch(&device, &pitch, 37, 5) != cudaSuccess || pitch < 37)
        return 1;
    unsigned char src[5][64], dst[5][64];
    memset(src, 0xA5, sizeof(src));
    memset(dst, 0xCC, sizeof(dst));
    if (cudaMemcpy2D(device, pitch, src, sizeof(src[0]), 37, 5,
                     cudaMemcpyHostToDevice) != cudaSuccess)
        return 2;
    if (cudaMemset2D(device, pitch, 0x5A, 37, 5) != cudaSuccess)
        return 3;
    if (cudaMemcpy2D(dst, sizeof(dst[0]), device, pitch, 37, 5,
                     cudaMemcpyDeviceToHost) != cudaSuccess)
        return 4;
    for (size_t y = 0; y < 5; ++y) {
        for (size_t x = 0; x < 37; ++x)
            if (dst[y][x] != 0x5A) return 5;
        for (size_t x = 37; x < sizeof(dst[0]); ++x)
            if (dst[y][x] != 0xCC) return 6;
    }
    if (cudaFree(device) != cudaSuccess)
        return 7;
    puts("MEMORY_LAYOUT_INTEGRATION=PASS");
    return 0;
}
