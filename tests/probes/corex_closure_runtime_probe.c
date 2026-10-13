#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(call) printf("%s result=%d\n", #call, (int)(call))
int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (cudaSetDevice(0)) return 1;
    int value = -1;
    R(cudaDeviceCanAccessPeer(&value, 0, 0));
    printf("SAME_DEVICE_CAN_ACCESS=%d\n", value);
    R(cudaDeviceCanAccessPeer(&value, 0, 1));
    R(cudaDeviceGetP2PAttribute(&value, cudaDevP2PAttrAccessSupported, 0, 0));
    struct cudaDeviceProp prop = {0};
    prop.totalGlobalMem = (size_t)-1;
    prop.major = 99;
    R(cudaChooseDevice(&value, &prop));
    printf("CHOSEN_DEVICE=%d\n", value);
    int devices[] = {0, 0};
    R(cudaSetValidDevices(devices, 2));
    void *p = NULL;
    if (posix_memalign(&p, 4096, 8192)) return 1;
    R(cudaHostRegister((char *)p + 1, 64, 0));
    R(cudaHostRegister((char *)p + 128, 64, 0));
    R(cudaHostUnregister((char *)p + 128));
    R(cudaHostUnregister((char *)p + 1));
    free(p);
    puts("CLOSURE_RUNTIME_PROBE=COMPLETE");
    return 0;
}
