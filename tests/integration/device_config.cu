#include <cuda_runtime.h>

#include <stdio.h>

extern "C" __global__ void corex_config_kernel(int *value)
{
    if (threadIdx.x == 0)
        *value = 1;
}

static int expect(cudaError_t actual, cudaError_t wanted, const char *label)
{
    if (actual != wanted) {
        printf("M3_S7_%s=FAIL actual=%d expected=%d\n", label, (int)actual, (int)wanted);
        return 1;
    }
    return 0;
}

int main()
{
    unsigned int flags = 0xffffffffu;
    int failures = 0;
    cudaError_t rc = cudaGetDeviceFlags(&flags);
    failures += expect(rc, cudaSuccess, "DEVICE_FLAGS");
    if (rc == cudaSuccess && flags != 0)
        failures++;
    failures += expect(cudaGetDeviceFlags(NULL), cudaErrorInvalidValue, "DEVICE_FLAGS_NULL");

    int value = 0;
    failures += expect(cudaDeviceGetAttribute(&value, cudaDevAttrMaxThreadsPerBlock, 0),
                       cudaSuccess, "DEVICE_ATTRIBUTE");
    if (value <= 0)
        failures++;
    failures += expect(cudaDeviceGetAttribute(&value, cudaDevAttrMaxThreadsPerBlock, 1),
                       cudaErrorInvalidDevice, "DEVICE_ATTRIBUTE_INVALID_DEVICE");
    failures += expect(cudaDeviceGetAttribute(&value, (cudaDeviceAttr)999, 0),
                       cudaErrorInvalidValue, "DEVICE_ATTRIBUTE_INVALID_ENUM");

    int least = 0;
    int greatest = 0;
    failures += expect(cudaDeviceGetStreamPriorityRange(&least, &greatest),
                       cudaSuccess, "PRIORITY_RANGE");
    if (least != 0 || greatest != -1)
        failures++;
    cudaStream_t stream = NULL;
    failures += expect(cudaStreamCreateWithPriority(&stream, cudaStreamDefault, greatest),
                       cudaSuccess, "PRIORITY_STREAM_CREATE");
    int stream_priority = 0;
    failures += expect(cudaStreamGetPriority(stream, &stream_priority),
                       cudaSuccess, "PRIORITY_STREAM_QUERY");
    if (stream_priority != greatest)
        failures++;
    failures += expect(cudaStreamDestroy(stream), cudaSuccess, "PRIORITY_STREAM_DESTROY");

    size_t limit = 0;
    failures += expect(cudaDeviceGetLimit(&limit, cudaLimitPrintfFifoSize),
                       cudaSuccess, "LIMIT_SUPPORTED");
    if (limit == 0)
        failures++;
    failures += expect(cudaDeviceGetLimit(&limit, cudaLimitStackSize),
                       cudaErrorNotSupported, "LIMIT_UNSUPPORTED");
    failures += expect(cudaDeviceGetLimit(&limit, (cudaLimit)99),
                       cudaErrorInvalidValue, "LIMIT_INVALID_ENUM");

    cudaFuncCache cache = cudaFuncCachePreferL1;
    failures += expect(cudaDeviceGetCacheConfig(&cache), cudaSuccess, "CACHE_CONFIG");
    if (cache != cudaFuncCachePreferNone)
        failures++;
    cudaSharedMemConfig shared = cudaSharedMemBankSizeDefault;
    failures += expect(cudaDeviceGetSharedMemConfig(&shared), cudaSuccess, "SHARED_CONFIG");
    if (shared != cudaSharedMemBankSizeFourByte)
        failures++;

    failures += expect(cudaFuncSetAttribute((const void *)corex_config_kernel,
                                             cudaFuncAttributeMaxDynamicSharedMemorySize, 0),
                       cudaSuccess, "FUNCTION_ATTRIBUTE");
    failures += expect(cudaFuncSetAttribute((const void *)corex_config_kernel,
                                             (cudaFuncAttribute)7, 0),
                       cudaErrorInvalidValue, "FUNCTION_ATTRIBUTE_INVALID_ENUM");
    failures += expect(cudaFuncSetCacheConfig((const void *)corex_config_kernel,
                                               cudaFuncCachePreferNone),
                       cudaSuccess, "FUNCTION_CACHE_CONFIG");
    failures += expect(cudaFuncSetCacheConfig((const void *)corex_config_kernel,
                                               (cudaFuncCache)4),
                       cudaErrorInvalidValue, "FUNCTION_CACHE_CONFIG_INVALID_ENUM");

    if (failures) {
        printf("M3_S7_DEVICE_CONFIG=FAIL failures=%d\n", failures);
        return 1;
    }
    printf("M3_S7_DEVICE_CONFIG=PASS flags=%u attribute=%d priority=%d limit=%zu cache=%d shared=%d\n",
           flags, value, greatest, limit, (int)cache, (int)shared);
    return 0;
}
