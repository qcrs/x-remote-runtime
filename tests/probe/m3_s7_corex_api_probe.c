#include <cuda.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    return data;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <cubin>\n", argv[0]);
        return 2;
    }
    CUresult rc = cuInit(0);
    printf("cuInit=%d\n", rc);
    if (rc != CUDA_SUCCESS)
        return 1;

    CUdevice device = -1;
    rc = cuDeviceGet(&device, 0);
    printf("cuDeviceGet=%d device=%d\n", rc, device);
    if (rc != CUDA_SUCCESS)
        return 1;

    CUcontext context = NULL;
    rc = cuCtxCreate(&context, 0, device);
    printf("cuCtxCreate=%d\n", rc);
    if (rc != CUDA_SUCCESS)
        return 1;

    unsigned int flags = 0;
    rc = cuCtxGetFlags(&flags);
    printf("cuCtxGetFlags=%d flags=%u\n", rc, flags);
    int least = 0, greatest = 0;
    rc = cuCtxGetStreamPriorityRange(&least, &greatest);
    printf("cuCtxGetStreamPriorityRange=%d least=%d greatest=%d\n", rc, least, greatest);
    for (int limit = 0; limit <= 6; ++limit) {
        size_t value = 0;
        rc = cuCtxGetLimit(&value, (CUlimit)limit);
        printf("cuCtxGetLimit[%d]=%d value=%zu\n", limit, rc, value);
    }
    CUfunc_cache cache = CU_FUNC_CACHE_PREFER_NONE;
    rc = cuCtxGetCacheConfig(&cache);
    printf("cuCtxGetCacheConfig=%d value=%d\n", rc, (int)cache);
    CUsharedconfig shared = CU_SHARED_MEM_CONFIG_DEFAULT_BANK_SIZE;
    rc = cuCtxGetSharedMemConfig(&shared);
    printf("cuCtxGetSharedMemConfig=%d value=%d\n", rc, (int)shared);

    int attributes[] = {1, 8, 10, 13, 16, 36, 37, 75, 76, 999};
    for (size_t i = 0; i < sizeof(attributes) / sizeof(attributes[0]); ++i) {
        int value = -1;
        rc = cuDeviceGetAttribute(&value, (CUdevice_attribute)attributes[i], device);
        printf("cuDeviceGetAttribute[%d]=%d value=%d\n", attributes[i], rc, value);
    }
    int invalid_device_value = -1;
    rc = cuDeviceGetAttribute(&invalid_device_value, CU_DEVICE_ATTRIBUTE_WARP_SIZE, (CUdevice)-1);
    printf("cuDeviceGetAttribute[invalid-device]=%d\n", rc);

    char pci[64] = {0};
    rc = cuDeviceGetPCIBusId(pci, sizeof(pci), device);
    printf("cuDeviceGetPCIBusId=%d value=%s\n", rc, pci);
    CUdevice pci_device = -1;
    CUresult pci_lookup = cuDeviceGetByPCIBusId(&pci_device, pci);
    printf("cuDeviceGetByPCIBusId=%d device=%d\n", pci_lookup, pci_device);
    char one_byte[1] = {'X'};
    CUresult small_output = cuDeviceGetPCIBusId(one_byte, sizeof(one_byte), device);
    printf("cuDeviceGetPCIBusId[1]=%d byte=%02x\n", small_output, (unsigned char)one_byte[0]);
    char zero_length_output[1] = {'X'};
    CUresult zero_length = cuDeviceGetPCIBusId(zero_length_output, 0, device);
    printf("cuDeviceGetPCIBusId[0]=%d byte=%02x\n", zero_length,
           (unsigned char)zero_length_output[0]);
    char invalid_pci[32] = "0000:ff:ff.0";
    pci_device = -1;
    pci_lookup = cuDeviceGetByPCIBusId(&pci_device, invalid_pci);
    printf("cuDeviceGetByPCIBusId[unknown]=%d\n", pci_lookup);
    char empty_pci[1] = {0};
    pci_lookup = cuDeviceGetByPCIBusId(&pci_device, empty_pci);
    printf("cuDeviceGetByPCIBusId[empty]=%d\n", pci_lookup);

    size_t image_size = 0;
    unsigned char *image = read_file(argv[1]);
    if (!image) {
        fprintf(stderr, "cannot read cubin\n");
        return 1;
    }
    FILE *image_file = fopen(argv[1], "rb");
    fseek(image_file, 0, SEEK_END);
    image_size = (size_t)ftell(image_file);
    fclose(image_file);
    (void)image_size;
    CUmodule module = NULL;
    rc = cuModuleLoadData(&module, image);
    printf("cuModuleLoadData=%d\n", rc);
    if (rc == CUDA_SUCCESS) {
        CUfunction function = NULL;
        rc = cuModuleGetFunction(&function, module, "m1s2_lifecycle_kernel");
        printf("cuModuleGetFunction=%d\n", rc);
        if (rc == CUDA_SUCCESS) {
            printf("cuFuncSetAttribute[8,0]=%d\n",
                   cuFuncSetAttribute(function, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, 0));
            printf("cuFuncSetAttribute[9,50]=%d\n",
                   cuFuncSetAttribute(function, CU_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT, 50));
            printf("cuFuncSetAttribute[7,0]=%d\n",
                   cuFuncSetAttribute(function, (CUfunction_attribute)7, 0));
            printf("cuFuncSetAttribute[8,-1]=%d\n",
                   cuFuncSetAttribute(function, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, -1));
            printf("cuFuncSetAttribute[9,101]=%d\n",
                   cuFuncSetAttribute(function, CU_FUNC_ATTRIBUTE_PREFERRED_SHARED_MEMORY_CARVEOUT, 101));
            printf("cuFuncSetCacheConfig[0]=%d\n",
                   cuFuncSetCacheConfig(function, CU_FUNC_CACHE_PREFER_NONE));
            printf("cuFuncSetCacheConfig[4]=%d\n",
                   cuFuncSetCacheConfig(function, (CUfunc_cache)4));
        }
        cuModuleUnload(module);
    }
    free(image);
    printf("cuCtxDestroy=%d\n", cuCtxDestroy(context));
    return 0;
}
