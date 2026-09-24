#include <cuda.h>
#include <stdio.h>

int main(void)
{
    CUdevice device;
    CUcontext context;
    CUstream stream;
    CUevent start, end, disabled;
    int flags = -1, priority = 12345;
    float elapsed = -1.0f;
    if (cuInit(0) != CUDA_SUCCESS || cuDeviceGet(&device, 0) != CUDA_SUCCESS ||
        cuCtxCreate(&context, 0, device) != CUDA_SUCCESS)
        return 1;
    if (cuStreamCreateWithPriority(&stream, CU_STREAM_NON_BLOCKING, 0) != CUDA_SUCCESS ||
        cuStreamGetFlags(stream, (unsigned int *)&flags) != CUDA_SUCCESS ||
        cuStreamGetPriority(stream, &priority) != CUDA_SUCCESS)
        return 2;
    if (flags != CU_STREAM_NON_BLOCKING)
        return 3;
    if (cuEventCreate(&start, CU_EVENT_DEFAULT) != CUDA_SUCCESS ||
        cuEventCreate(&end, CU_EVENT_DEFAULT) != CUDA_SUCCESS ||
        cuEventCreate(&disabled, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS)
        return 4;
    if (cuEventRecord(start, stream) != CUDA_SUCCESS ||
        cuEventRecord(end, stream) != CUDA_SUCCESS ||
        cuEventSynchronize(end) != CUDA_SUCCESS ||
        cuEventElapsedTime(&elapsed, start, end) != CUDA_SUCCESS || elapsed < 0.0f)
        return 5;
    if (cuEventElapsedTime(&elapsed, disabled, end) == CUDA_SUCCESS)
        return 6;
    printf("M3_S3_COREX_PROBE=PASS flags=%d priority=%d elapsed_ms=%f disable_timing_rejected=YES\n",
           flags, priority, elapsed);
    cuEventDestroy(disabled);
    cuEventDestroy(end);
    cuEventDestroy(start);
    cuStreamDestroy(stream);
    cuCtxDestroy(context);
    return 0;
}
