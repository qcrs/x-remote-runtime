#include <cuda.h>
#include <stdio.h>
#define R(call) printf("%s result=%d\n", #call, (int)(call))
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc != 2) return 2;
    CUdevice device; CUcontext context; CUmodule module; CUfunction function;
    if (cuInit(0) || cuDeviceGet(&device, 0) || cuCtxCreate(&context, 0, device) ||
        cuModuleLoad(&module, argv[1]) || cuModuleGetFunction(&function, module, "m1s2_lifecycle_kernel")) return 1;
    for (int i = 0; i <= 10; ++i) {
        int value = -777;
        CUresult result = cuFuncGetAttribute(&value, (CUfunction_attribute)i, function);
        printf("FUNCTION_ATTRIBUTE id=%d result=%d value=%d\n", i, result, value);
    }
    int blocks = -1;
    R(cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags(&blocks, function, 32, 0, CU_OCCUPANCY_DISABLE_CACHING_OVERRIDE));
    printf("OCCUPANCY_DISABLE_CACHING_BLOCKS=%d\n", blocks);
    cuModuleUnload(module); cuCtxDestroy(context);
    puts("CLOSURE_FUNCTION_PROBE=COMPLETE");
    return 0;
}
