#ifdef COREX_LAYOUT_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Compile against both the installed public header and our internal ABI. */
_Static_assert(sizeof(struct cudaMemcpy3DParms) == 160, "3D parms ABI size");
_Static_assert(offsetof(struct cudaMemcpy3DParms, srcPtr) == 32, "3D source ABI");
_Static_assert(offsetof(struct cudaMemcpy3DParms, dstPtr) == 96, "3D destination ABI");
_Static_assert(offsetof(struct cudaMemcpy3DParms, extent) == 128, "3D extent ABI");
_Static_assert(offsetof(struct cudaMemcpy3DParms, kind) == 152, "3D kind ABI");

static int check(const char *label, cudaError_t r)
{
    if (r == cudaSuccess) return 0;
    printf("%s cudaError=%d\n", label, (int)r);
    return 1;
}

static int transfers(int three_d, int async, cudaStream_t stream)
{
    const size_t width = three_d ? 17 : 37, height = 5;
    const size_t depth = three_d ? 3 : 1, rows = three_d ? 7 : 5;
    const size_t hpitch = 64, offset = three_d ? 3 : 0;
    size_t pitch = three_d ? 32 : 0;
    void *a = NULL, *b = NULL;
    if (three_d) {
        if (check("malloc(a)", cudaMalloc(&a, offset + pitch*rows*depth)) ||
            check("malloc(b)", cudaMalloc(&b, offset + pitch*rows*depth))) return 1;
    } else {
        size_t bpitch = 0;
        if (check("mallocPitch(a)", cudaMallocPitch(&a, &pitch, width, height)) ||
            check("mallocPitch(b)", cudaMallocPitch(&b, &bpitch, width, height)) ||
            pitch != bpitch) return 1;
        printf("REMOTE_2D_PITCH=%zu\n", pitch);
    }
    unsigned char input[1344], output[1344], device_data[1344], readback[1344];
    size_t dbytes = pitch*rows*depth, hbytes = hpitch*rows*depth;
    if (dbytes > sizeof(device_data)) return 1;
    memset(input, 0xCC, hbytes); memset(device_data, 0xCC, dbytes);
    for (size_t z = 0; z < depth; ++z)
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x) {
                unsigned char value = (unsigned char)(1 + x + 11*y + 53*z);
                input[(z*rows+y)*hpitch+x] = value;
                device_data[(z*rows+y)*pitch+x] = value;
            }
    unsigned char *aptr = (unsigned char *)a + offset, *bptr = (unsigned char *)b + offset;
    int failures = 0;
    for (int direction = 1; direction <= 3; ++direction) {
        memset(output, 0xCC, hbytes); memset(readback, 0xCC, dbytes);
        if (check("seed(a)", cudaMemcpy(aptr, device_data, dbytes, cudaMemcpyHostToDevice)) ||
            check("seed(b)", cudaMemcpy(bptr, readback, dbytes, cudaMemcpyHostToDevice))) return 1;
        cudaError_t r;
        enum cudaMemcpyKind kind = direction == 1 ? cudaMemcpyHostToDevice :
                              direction == 2 ? cudaMemcpyDeviceToHost : cudaMemcpyDeviceToDevice;
        const void *src = direction == 1 ? input : aptr;
        void *dst = direction == 2 ? output : bptr;
        size_t sp = direction == 1 ? hpitch : pitch, dp = direction == 2 ? hpitch : pitch;
        if (three_d) {
            struct cudaMemcpy3DParms p;
            memset(&p, 0, sizeof(p));
            p.srcPtr = (struct cudaPitchedPtr){(void *)src, sp, width, rows};
            p.dstPtr = (struct cudaPitchedPtr){dst, dp, width, rows};
            p.extent = (struct cudaExtent){width, height, depth}; p.kind = kind;
            r = async ? cudaMemcpy3DAsync(&p, stream) : cudaMemcpy3D(&p);
        } else {
            r = async ? cudaMemcpy2DAsync(dst, dp, src, sp, width, height, kind, stream)
                      : cudaMemcpy2D(dst, dp, src, sp, width, height, kind);
        }
        char label[80];
        snprintf(label, sizeof(label), "REMOTE_%dD%s_%s", three_d ? 3 : 2,
                 async ? (stream ? "_ASYNC" : "_ASYNC_DEFAULT") : "",
                 direction == 1 ? "H2D" : direction == 2 ? "D2H" : "D2D");
        int failed = check(label, r);
        if (async && check("streamSynchronize", cudaStreamSynchronize(stream))) failed = 1;
        if (!failed && direction != 2) {
            failed = check("readback(b)", cudaMemcpy(readback, bptr, dbytes, cudaMemcpyDeviceToHost)) ||
                     memcmp(readback, device_data, dbytes) != 0;
        } else if (!failed) failed = memcmp(output, input, hbytes) != 0;
        printf("%s=%s\n", label, failed ? "FAIL" : "PASS");
        failures += failed;
    }
    failures += check("free(b)", cudaFree(b)) + check("free(a)", cudaFree(a));
    return failures;
}

int main(void)
{
    puts("MEMORY_LAYOUT_PUBLIC_ABI=CoreX_4.4");
    cudaStream_t stream = NULL;
    if (check("streamCreate", cudaStreamCreate(&stream))) return 1;
    int failures = transfers(0, 0, NULL);
    failures += transfers(0, 1, stream);
    failures += transfers(0, 1, NULL);
    failures += transfers(1, 0, NULL);
    failures += transfers(1, 1, stream);
    failures += check("streamDestroy", cudaStreamDestroy(stream));
    puts("REMOTE_3D_ASYNC_SEMANTICS=OPCODE_56_BACKEND_STREAM_TRANSFER_RETIREMENT");
    printf("MEMORY_LAYOUT_INTEGRATION=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
