#include <cuda_runtime.h>

#include "corex_remote_cudart_ext.h"

#include <stdio.h>
#include <string.h>

extern "C" void corexRemoteRuntimeShutdown(void);

extern "C" __global__
void m1s2_lifecycle_kernel(int *value)
{
    if (threadIdx.x == 0)
        *value += 1;
}

static int expect(const char *name, cudaError_t actual, cudaError_t expected)
{
    printf("M1_S2_CHECK name=%s actual=%d expected=%d result=%s\n",
           name,
           (int)actual,
           (int)expected,
           actual == expected ? "PASS" : "FAIL");
    return actual == expected ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <device-image>\n", argv[0]);
        return 2;
    }

    int failures = 0;
    int host_value = 41;
    int *old_ptr = NULL;
    cudaStream_t old_stream = NULL;
    cudaEvent_t old_event = NULL;
    corexRemoteModule_t old_module = NULL;
    const void *old_controlled_kernel = NULL;

    failures += expect("malloc-generation-1",
                       cudaMalloc((void **)&old_ptr, sizeof(host_value)),
                       cudaSuccess);
    failures += expect("copy-generation-1",
                       cudaMemcpy(old_ptr,
                                  &host_value,
                                  sizeof(host_value),
                                  cudaMemcpyHostToDevice),
                       cudaSuccess);
    failures += expect("stream-generation-1",
                       cudaStreamCreate(&old_stream),
                       cudaSuccess);
    failures += expect("live-transfer-at-shutdown",
                       cudaMemcpyAsync(old_ptr,
                                       &host_value,
                                       sizeof(host_value),
                                       cudaMemcpyHostToDevice,
                                       old_stream),
                       cudaSuccess);
    failures += expect("event-generation-1",
                       cudaEventCreate(&old_event),
                       cudaSuccess);
    failures += expect("module-generation-1",
                       corexRemoteModuleLoad(argv[1], &old_module),
                       cudaSuccess);

    corexRemoteKernelArgDesc arg = {
        COREX_REMOTE_KERNEL_ARG_DEVICE_PTR,
        (uint32_t)sizeof(void *),
    };
    failures += expect("controlled-kernel-generation-1",
                       corexRemoteRegisterKernel(old_module,
                                                 "m1s2_lifecycle_kernel",
                                                 &arg,
                                                 1,
                                                 &old_controlled_kernel),
                       cudaSuccess);

    if (failures != 0)
        return 3;

    corexRemoteRuntimeShutdown();
    corexRemoteRuntimeShutdown();

    int count = 0;
    failures += expect("lazy-reconnect", cudaGetDeviceCount(&count), cudaSuccess);
    if (count != 1)
        failures++;

    failures += expect("stale-pointer",
                       cudaFree(old_ptr),
                       cudaErrorInvalidDevicePointer);
    failures += expect("stale-stream",
                       cudaStreamQuery(old_stream),
                       cudaErrorInvalidResourceHandle);
    failures += expect("stale-event",
                       cudaEventQuery(old_event),
                       cudaErrorInvalidResourceHandle);
    failures += expect("stale-module",
                       corexRemoteModuleUnload(old_module),
                       cudaErrorInvalidResourceHandle);

    void *old_args[] = {&old_ptr};
    failures += expect("stale-compiler-kernel",
                       cudaLaunchKernel((const void *)m1s2_lifecycle_kernel,
                                        dim3(1, 1, 1),
                                        dim3(1, 1, 1),
                                        old_args,
                                        0,
                                        NULL),
                       cudaErrorInvalidResourceHandle);
    failures += expect("stale-controlled-kernel",
                       cudaLaunchKernel(old_controlled_kernel,
                                        dim3(1, 1, 1),
                                        dim3(1, 1, 1),
                                        old_args,
                                        0,
                                        NULL),
                       cudaErrorInvalidResourceHandle);

    int *fresh_ptr = NULL;
    cudaStream_t fresh_stream = NULL;
    cudaEvent_t fresh_event = NULL;
    int roundtrip = 0;

    failures += expect("malloc-generation-2",
                       cudaMalloc((void **)&fresh_ptr, sizeof(host_value)),
                       cudaSuccess);
    failures += expect("copy-h2d-generation-2",
                       cudaMemcpy(fresh_ptr,
                                  &host_value,
                                  sizeof(host_value),
                                  cudaMemcpyHostToDevice),
                       cudaSuccess);
    failures += expect("copy-d2h-generation-2",
                       cudaMemcpy(&roundtrip,
                                  fresh_ptr,
                                  sizeof(roundtrip),
                                  cudaMemcpyDeviceToHost),
                       cudaSuccess);
    failures += expect("stream-generation-2",
                       cudaStreamCreate(&fresh_stream),
                       cudaSuccess);
    failures += expect("event-generation-2",
                       cudaEventCreate(&fresh_event),
                       cudaSuccess);

    if (roundtrip != host_value || fresh_ptr == old_ptr ||
        fresh_stream == old_stream || fresh_event == old_event)
        failures++;

    failures += expect("event-destroy-generation-2",
                       cudaEventDestroy(fresh_event),
                       cudaSuccess);
    failures += expect("stream-destroy-generation-2",
                       cudaStreamDestroy(fresh_stream),
                       cudaSuccess);
    failures += expect("free-generation-2", cudaFree(fresh_ptr), cudaSuccess);

    corexRemoteRuntimeShutdown();
    corexRemoteRuntimeShutdown();

    printf("M1_S2_LIFECYCLE_RESULT=%s failures=%d\n",
           failures == 0 ? "PASS" : "FAIL",
           failures);
    return failures == 0 ? 0 : 4;
}
