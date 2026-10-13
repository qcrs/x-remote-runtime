#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(call)                                                                                \
    do {                                                                                           \
        CUresult r = (call);                                                                       \
        printf("%s=%d\n", #call, r);                                                               \
        fflush(stdout);                                                                            \
        if (r)                                                                                     \
            return 1;                                                                              \
    } while (0)
#define SHOW(call)                                                                                 \
    do {                                                                                           \
        CUresult r = (call);                                                                       \
        printf("%s=%d\n", #call, r);                                                               \
        fflush(stdout);                                                                            \
    } while (0)
static int verify(CUdeviceptr ptr, int want, const char *label) {
    int got = 0;
    CHECK(cuMemcpyDtoH(&got, ptr, 4));
    printf("%s got=%d want=%d\n", label, got, want);
    return got != want;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    void *image = malloc((size_t)len);
    if (!image || fread(image, 1, (size_t)len, f) != (size_t)len)
        return 2;
    fclose(f);
    CUdevice dev;
    CUcontext ctx;
    CUstream stream;
    CUmodule module;
    CUfunction fn;
    CUdeviceptr ptr = 0;
    CHECK(cuInit(0));
    CHECK(cuDeviceGet(&dev, 0));
    CHECK(cuCtxCreate(&ctx, 0, dev));
    CHECK(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));
    CHECK(cuModuleLoadData(&module, image));
    CHECK(cuModuleGetFunction(&fn, module, "m1s2_lifecycle_kernel"));
    CHECK(cuMemAlloc(&ptr, 4));
    CUgraph graph;
    CUgraphNode fill, kernel, record, wait;
    CUgraphExec exec;
    CUevent event;
    CHECK(cuGraphCreate(&graph, 0));
    CUDA_MEMSET_NODE_PARAMS mp = {0};
    mp.dst = ptr;
    mp.elementSize = 4;
    mp.value = 41;
    mp.width = 1;
    mp.height = 1;
    CHECK(cuGraphAddMemsetNode(&fill, graph, NULL, 0, &mp, ctx));
    void *args[] = {&ptr};
    CUDA_KERNEL_NODE_PARAMS kp = {0};
    kp.func = fn;
    kp.gridDimX = kp.gridDimY = kp.gridDimZ = 1;
    kp.blockDimX = kp.blockDimY = kp.blockDimZ = 1;
    kp.kernelParams = args;
    CHECK(cuGraphAddKernelNode(&kernel, graph, &fill, 1, &kp));
    CHECK(cuEventCreate(&event, CU_EVENT_DISABLE_TIMING));
    CHECK(cuGraphAddEventRecordNode(&record, graph, &kernel, 1, event));
    CHECK(cuGraphAddEventWaitNode(&wait, graph, &record, 1, event));
    size_t count = 0;
    CHECK(cuGraphGetEdges(graph, NULL, NULL, &count));
    printf("EDGE_COUNT=%zu\n", count);
    CHECK(cuGraphInstantiate(&exec, graph, NULL, NULL, 0));
    CHECK(cuGraphLaunch(exec, stream));
    CHECK(cuStreamSynchronize(stream));
    if (verify(ptr, 42, "KERNEL_GRAPH_DATA"))
        return 1;
    unsigned enabled = 99;
    SHOW(cuGraphNodeSetEnabled(exec, kernel, 0));
    SHOW(cuGraphNodeGetEnabled(exec, kernel, &enabled));
    printf("ENABLED_READBACK=%u\n", enabled);
    CHECK(cuGraphLaunch(exec, stream));
    CHECK(cuStreamSynchronize(stream));
    printf("DISABLED_DATA_CHECK=%d\n", verify(ptr, 41, "DISABLED_DATA"));
    SHOW(cuGraphNodeSetEnabled(exec, kernel, 1));
    SHOW(cuGraphExecKernelNodeSetParams(exec, kernel, &kp));
    CUgraph clone;
    CHECK(cuGraphClone(&clone, graph));
    CUgraphNode cloned;
    CHECK(cuGraphNodeFindInClone(&cloned, kernel, clone));
    CUgraphNodeType type;
    CHECK(cuGraphNodeGetType(cloned, &type));
    printf("CLONE_KERNEL_TYPE=%d\n", type);
    CHECK(cuGraphDestroy(clone));
    SHOW(cuGraphRemoveDependencies(graph, &fill, &kernel, 1));
    SHOW(cuGraphAddDependencies(graph, &fill, &kernel, 1));
    CHECK(cuGraphExecDestroy(exec));
    CHECK(cuGraphDestroy(graph));
    CHECK(cuMemsetD32(ptr, 10, 1));
    CHECK(cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL));
    CHECK(cuMemsetD32Async(ptr, 41, 1, stream));
    CHECK(cuLaunchKernel(fn, 1, 1, 1, 1, 1, 1, 0, stream, args, NULL));
    CHECK(cuStreamEndCapture(stream, &graph));
    if (verify(ptr, 10, "CAPTURE_DEFERRED_DATA"))
        return 1;
    CHECK(cuGraphInstantiate(&exec, graph, NULL, NULL, 0));
    CHECK(cuGraphLaunch(exec, stream));
    CHECK(cuStreamSynchronize(stream));
    if (verify(ptr, 42, "CAPTURE_REPLAY_DATA"))
        return 1;
    CHECK(cuGraphExecDestroy(exec));
    CHECK(cuGraphDestroy(graph));
    CHECK(cuEventDestroy(event));
    CHECK(cuMemFree(ptr));
    CHECK(cuModuleUnload(module));
    CHECK(cuStreamDestroy(stream));
    CHECK(cuCtxDestroy(ctx));
    free(image);
    puts("GRAPH_EXECUTION_PROBE=PASS");
    return 0;
}
