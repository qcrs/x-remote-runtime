#include "library_kernels.cu"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    cudaFuncAttributes attributes = {};
    cudaError_t result = cudaFuncGetAttributes(&attributes, (const void *)m1s2_lifecycle_kernel);
    printf("FUNCTION_INFO result=%d cacheModeCA=%d maxDynamicSharedSizeBytes=%d carveout=%d\n",
        result, attributes.cacheModeCA, attributes.maxDynamicSharedSizeBytes, attributes.preferredShmemCarveout);
    if (result || attributes.maxDynamicSharedSizeBytes != atoi(argv[1]) || attributes.cacheModeCA ||
        attributes.preferredShmemCarveout != -1) return 1;
    puts("FUNCTION_INFO_REMOTE=PASS");
    return 0;
}
