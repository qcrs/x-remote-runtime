#include <cuda.h>
#include <stdio.h>
#include <string.h>

static int result(const char *label, CUresult r)
{
    const char *name = NULL;
    (void)cuGetErrorName(r, &name);
    printf("%s CUresult=%d name=%s\n", label, (int)r, name ? name : "UNKNOWN");
    return r != CUDA_SUCCESS;
}

/* Each direction is seeded/read back with linear copies, independently of
 * the other layout primitives. Host padding is checked as well as row data. */
static int probe(int three_d, int async, CUstream stream)
{
    const size_t width = three_d ? 17 : 37, height = 5;
    const size_t depth = three_d ? 3 : 1;
    const size_t dpitch = three_d ? 32 : 37;
    const size_t rows = three_d ? 7 : 5, hpitch = 64;
    const size_t dbytes = dpitch * rows * depth;
    const size_t hbytes = hpitch * rows * depth;
    unsigned char input_storage[1344], expected[1344], output_storage[1344], device_data[1344];
    unsigned char *input = input_storage, *output = output_storage;
    if (async) {
        void *pinned_input = NULL, *pinned_output = NULL;
        if (result("cuMemAllocHost(input)", cuMemAllocHost(&pinned_input, hbytes))) return 1;
        if (result("cuMemAllocHost(output)", cuMemAllocHost(&pinned_output, hbytes))) {
            (void)cuMemFreeHost(pinned_input);
            return 1;
        }
        input = pinned_input; output = pinned_output;
    }
    CUdeviceptr a = 0, b = 0;
    if (result("cuMemAlloc(a)", cuMemAlloc(&a, dbytes))) return 1;
    if (result("cuMemAlloc(b)", cuMemAlloc(&b, dbytes))) {
        (void)cuMemFree(a);
        return 1;
    }
    memset(input, 0xCC, hbytes);
    memset(device_data, 0xCC, dbytes);
    for (size_t z = 0; z < depth; ++z)
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x) {
                unsigned char value = (unsigned char)(1 + x + 11*y + 53*z);
                input[(z*rows+y)*hpitch+x] = value;
                device_data[(z*rows+y)*dpitch+x] = value;
            }
    int failures = 0;
    for (int direction = 1; direction <= 3; ++direction) {
        memset(output, 0xCC, hbytes);
        memset(expected, 0xCC, dbytes);
        if (result("seed(a)", cuMemcpyHtoD(a, device_data, dbytes)) ||
            result("seed(b)", cuMemcpyHtoD(b, expected, dbytes))) {
            ++failures;
            continue;
        }
        CUresult r;
        if (three_d) {
            CUDA_MEMCPY3D c;
            memset(&c, 0, sizeof(c));
            c.WidthInBytes = width; c.Height = height; c.Depth = depth;
            c.srcMemoryType = direction == 1 ? CU_MEMORYTYPE_HOST : CU_MEMORYTYPE_DEVICE;
            c.dstMemoryType = direction == 2 ? CU_MEMORYTYPE_HOST : CU_MEMORYTYPE_DEVICE;
            c.srcHost = direction == 1 ? input : NULL; c.srcDevice = direction == 1 ? 0 : a;
            c.dstHost = direction == 2 ? output : NULL; c.dstDevice = direction == 2 ? 0 : b;
            c.srcPitch = direction == 1 ? hpitch : dpitch; c.srcHeight = rows;
            c.dstPitch = direction == 2 ? hpitch : dpitch; c.dstHeight = rows;
            r = async ? cuMemcpy3DAsync(&c, stream) : cuMemcpy3D(&c);
        } else {
            CUDA_MEMCPY2D c;
            memset(&c, 0, sizeof(c));
            c.WidthInBytes = width; c.Height = height;
            c.srcMemoryType = direction == 1 ? CU_MEMORYTYPE_HOST : CU_MEMORYTYPE_DEVICE;
            c.dstMemoryType = direction == 2 ? CU_MEMORYTYPE_HOST : CU_MEMORYTYPE_DEVICE;
            c.srcHost = direction == 1 ? input : NULL; c.srcDevice = direction == 1 ? 0 : a;
            c.dstHost = direction == 2 ? output : NULL; c.dstDevice = direction == 2 ? 0 : b;
            c.srcPitch = direction == 1 ? hpitch : dpitch;
            c.dstPitch = direction == 2 ? hpitch : dpitch;
            r = async ? cuMemcpy2DAsync(&c, stream) : cuMemcpy2D(&c);
        }
        char label[64];
        snprintf(label, sizeof(label), "COREX_%dD%s_%s", three_d ? 3 : 2,
                 async ? "_ASYNC" : "", direction == 1 ? "H2D" : direction == 2 ? "D2H" : "D2D");
        int failed = result(label, r);
        if (async && result("cuStreamSynchronize", cuStreamSynchronize(stream))) failed = 1;
        if (!failed && direction != 2) {
            r = cuMemcpyDtoH(expected, b, dbytes);
            failed = result("readback(b)", r) || memcmp(expected, device_data, dbytes);
        } else if (!failed) {
            failed = memcmp(output, input, hbytes) != 0;
        }
        printf("%s_DATA=%s\n", label, failed ? "FAIL" : "PASS");
        failures += failed != 0;
    }
    (void)cuMemFree(b); (void)cuMemFree(a);
    if (async) { (void)cuMemFreeHost(output); (void)cuMemFreeHost(input); }
    return failures;
}

int main(void)
{
    CUdevice device; CUcontext context; CUstream stream;
    if (result("cuInit", cuInit(0)) || result("cuDeviceGet", cuDeviceGet(&device, 0)) ||
        result("cuCtxCreate", cuCtxCreate(&context, 0, device))) return 1;
    char name[128] = {0}; int version = 0;
    (void)cuDeviceGetName(name, sizeof(name), device);
    (void)cuDriverGetVersion(&version);
    printf("DEVICE=%s DRIVER_VERSION=%d\n", name, version);
    CUdeviceptr pitched = 0; size_t pitch = 0;
    if (!result("cuMemAllocPitch(width=37,height=5,element=1)",
                cuMemAllocPitch(&pitched, &pitch, 37, 5, 1))) (void)cuMemFree(pitched);
    int failures = probe(0, 0, NULL) + probe(1, 0, NULL);
    if (!result("cuStreamCreate", cuStreamCreate(&stream, CU_STREAM_DEFAULT))) {
        failures += probe(0, 1, stream) + probe(1, 1, stream);
        (void)cuStreamDestroy(stream);
    } else ++failures;
    (void)cuCtxDestroy(context);
    printf("COREX_MEMORY_LAYOUT_PROBE_FAILURES=%d\n", failures);
    return 0; /* Unsupported directions are recorded results, not setup failures. */
}
