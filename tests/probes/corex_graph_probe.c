#include <cuda.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define SHOW(call)                                                                                 \
    do {                                                                                           \
        CUresult r = (call);                                                                       \
        if (r != CUDA_SUCCESS)                                                                     \
            ++failures;                                                                            \
        printf("%s=%d\n", #call, (int)r);                                                          \
        fflush(stdout);                                                                            \
    } while (0)
int main(void) {
    CUdevice dev;
    CUcontext ctx;
    CUstream stream;
    CUgraph graph = NULL, captured = NULL, clone = NULL;
    CUgraphExec exec = NULL;
    CUgraphNode empty = NULL, fill = NULL, copy = NULL;
    CUdeviceptr a = 0, b = 0;
    unsigned char host[64] = {0};
    if (cuInit(0) || cuDeviceGet(&dev, 0) || cuCtxCreate(&ctx, 0, dev) ||
        cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING))
        return 1;
    SHOW(cuMemAlloc(&a, 64));
    SHOW(cuMemAlloc(&b, 64));
    SHOW(cuGraphCreate(&graph, 0));
    printf("GRAPH_PRESENT=%d\n", graph != NULL);
    if (!graph)
        ++failures;
    if (graph) {
        SHOW(cuGraphAddEmptyNode(&empty, graph, NULL, 0));
        CUDA_MEMSET_NODE_PARAMS p = {0};
        p.dst = a;
        p.value = 0x39;
        p.elementSize = 1;
        p.width = 64;
        p.height = 1;
        SHOW(cuGraphAddMemsetNode(&fill, graph, empty ? &empty : NULL, empty ? 1 : 0, &p, ctx));
        if (fill) {
            CUDA_MEMSET_NODE_PARAMS got = {0};
            SHOW(cuGraphMemsetNodeGetParams(fill, &got));
            printf("MEMSET_READBACK=%d\n", got.dst == a && got.value == 0x39 && got.width == 64);
            if (got.dst != a || got.value != 0x39 || got.width != 64)
                ++failures;
        }
        CUDA_MEMCPY3D c = {0};
        c.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        c.srcDevice = a;
        c.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        c.dstDevice = b;
        c.WidthInBytes = 64;
        c.Height = 1;
        c.Depth = 1;
        SHOW(cuGraphAddMemcpyNode(&copy, graph, fill ? &fill : NULL, fill ? 1 : 0, &c, ctx));
        size_t n = 0;
        SHOW(cuGraphGetNodes(graph, NULL, &n));
        printf("NODE_COUNT=%zu\n", n);
        if (n != 3)
            ++failures;
        SHOW(cuGraphClone(&clone, graph));
        if (clone)
            SHOW(cuGraphDestroy(clone));
        CUgraphNode error = NULL;
        char log[256] = {0};
        SHOW(cuGraphInstantiate(&exec, graph, &error, log, sizeof(log)));
        printf("EXEC_PRESENT=%d LOG=%s\n", exec != NULL, log);
        if (exec) {
            for (int i = 0; i < 2; ++i) {
                SHOW(cuGraphLaunch(exec, stream));
                SHOW(cuStreamSynchronize(stream));
                SHOW(cuMemcpyDtoH(host, b, 64));
                int ok = 1;
                for (int j = 0; j < 64; ++j)
                    if (host[j] != 0x39)
                        ok = 0;
                printf("GRAPH_LAUNCH_%d_DATA=%d\n", i, ok);
                if (!ok)
                    ++failures;
            }
            if (fill) {
                p.value = 0x57;
                SHOW(cuGraphExecMemsetNodeSetParams(exec, fill, &p, ctx));
                SHOW(cuGraphLaunch(exec, stream));
                SHOW(cuStreamSynchronize(stream));
                SHOW(cuMemcpyDtoH(host, b, 64));
                int ok = 1;
                for (int j = 0; j < 64; ++j)
                    if (host[j] != 0x57)
                        ok = 0;
                printf("EXEC_UPDATE_DATA=%d\n", ok);
                if (!ok)
                    ++failures;
            }
            CUgraphExecUpdateResult update;
            SHOW(cuGraphExecUpdate(exec, graph, &error, &update));
            printf("UPDATE_RESULT=%d\n", update);
            SHOW(cuGraphLaunch(exec, stream));
            SHOW(cuStreamSynchronize(stream));
            SHOW(cuMemcpyDtoH(host, b, 64));
            int whole_ok = 1;
            for (int i = 0; i < 64; ++i)
                if (host[i] != 0x39)
                    whole_ok = 0;
            printf("WHOLE_EXEC_UPDATE_DATA=%d\n", whole_ok);
            if (!whole_ok)
                ++failures;
            CUgraphNode added = NULL;
            SHOW(cuGraphAddEmptyNode(&added, graph, copy ? &copy : NULL, copy ? 1 : 0));
            p.value = 0x65;
            SHOW(cuGraphMemsetNodeSetParams(fill, &p));
            update = CU_GRAPH_EXEC_UPDATE_ERROR;
            CUresult topology = cuGraphExecUpdate(exec, graph, &error, &update);
            printf("TOPOLOGY_UPDATE_RC=%d\n", topology);
            if ((int)topology != 1000 || update != CU_GRAPH_EXEC_UPDATE_ERROR_TOPOLOGY_CHANGED)
                ++failures;
            printf("TOPOLOGY_UPDATE_RESULT=%d\n", update);
            SHOW(cuGraphLaunch(exec, stream));
            SHOW(cuStreamSynchronize(stream));
            SHOW(cuMemcpyDtoH(host, b, 64));
            int retained = 1;
            for (int i = 0; i < 64; ++i)
                if (host[i] != 0x39)
                    retained = 0;
            printf("FAILED_UPDATE_RETAINS_DATA=%d\n", retained);
            if (!retained)
                ++failures;
            if (added)
                SHOW(cuGraphDestroyNode(added));
            SHOW(cuGraphDestroy(graph));
            graph = NULL;
            SHOW(cuGraphLaunch(exec, stream));
            SHOW(cuStreamSynchronize(stream));
            SHOW(cuGraphExecDestroy(exec));
        }
        if (graph)
            SHOW(cuGraphDestroy(graph));
    }
    /* Disjoint rows within one allocation can have overlapping bounding spans. */
    CUgraph alias = NULL;
    CUgraphNode alias_fill = NULL, alias_copy = NULL;
    CUgraphExec alias_exec = NULL;
    SHOW(cuGraphCreate(&alias, 0));
    CUDA_MEMSET_NODE_PARAMS af = {0};
    af.dst = a;
    af.pitch = 16;
    af.elementSize = 1;
    af.value = 0x43;
    af.width = 8;
    af.height = 4;
    CUDA_MEMCPY3D ac = {0};
    ac.srcMemoryType = ac.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    ac.srcDevice = a;
    ac.dstDevice = a + 8;
    ac.srcPitch = ac.dstPitch = 16;
    ac.srcHeight = ac.dstHeight = 4;
    ac.WidthInBytes = 8;
    ac.Height = 4;
    ac.Depth = 1;
    if (alias) {
        SHOW(cuGraphAddMemsetNode(&alias_fill, alias, NULL, 0, &af, ctx));
        SHOW(cuGraphAddMemcpyNode(&alias_copy, alias, &alias_fill, 1, &ac, ctx));
        SHOW(cuGraphInstantiate(&alias_exec, alias, NULL, NULL, 0));
        if (alias_exec) {
            SHOW(cuGraphLaunch(alias_exec, stream));
            SHOW(cuStreamSynchronize(stream));
            SHOW(cuMemcpyDtoH(host, a, 64));
            int ok = 1;
            for (int i = 0; i < 64; ++i)
                if (host[i] != 0x43)
                    ok = 0;
            printf("SAME_ALLOCATION_DISJOINT_ROWS_DATA=%d\n", ok);
            if (!ok)
                ++failures;
            SHOW(cuGraphExecDestroy(alias_exec));
        }
        SHOW(cuGraphDestroy(alias));
    }
    /* The same native fill capability includes two-byte elements. */
    CUgraph two = NULL;
    CUgraphNode two_fill = NULL;
    CUgraphExec two_exec = NULL;
    SHOW(cuGraphCreate(&two, 0));
    CUDA_MEMSET_NODE_PARAMS two_params = {0};
    two_params.dst = a;
    two_params.elementSize = 2;
    two_params.value = 0x1234;
    two_params.width = 32;
    two_params.height = 1;
    if (two) {
        SHOW(cuGraphAddMemsetNode(&two_fill, two, NULL, 0, &two_params, ctx));
        SHOW(cuGraphInstantiate(&two_exec, two, NULL, NULL, 0));
        if (two_exec) {
            SHOW(cuGraphLaunch(two_exec, stream));
            SHOW(cuStreamSynchronize(stream));
            SHOW(cuMemcpyDtoH(host, a, 64));
            int ok = 1;
            for (int i = 0; i < 64; ++i)
                if (host[i] != (i % 2 ? 0x12 : 0x34))
                    ok = 0;
            printf("TWO_BYTE_MEMSET_DATA=%d\n", ok);
            if (!ok)
                ++failures;
            SHOW(cuGraphExecDestroy(two_exec));
        }
        SHOW(cuGraphDestroy(two));
    }
    CUresult begin = cuStreamBeginCapture(stream, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL);
    printf("cuStreamBeginCapture=%d\n", begin);
    fflush(stdout);
    CUstreamCaptureStatus status = 99;
    SHOW(cuStreamIsCapturing(stream, &status));
    printf("CAPTURE_STATUS=%d\n", status);
    if (status != CU_STREAM_CAPTURE_STATUS_ACTIVE || begin != CUDA_SUCCESS)
        ++failures;
    if (begin == CUDA_SUCCESS) {
        SHOW(cuMemsetD8Async(a, 0x71, 64, stream));
        SHOW(cuStreamEndCapture(stream, &captured));
        printf("CAPTURE_GRAPH_PRESENT=%d\n", captured != NULL);
        if (captured)
            SHOW(cuGraphDestroy(captured));
    }
    if (a)
        SHOW(cuMemFree(a));
    if (b)
        SHOW(cuMemFree(b));
    SHOW(cuStreamDestroy(stream));
    SHOW(cuCtxDestroy(ctx));
    return failures ? 1 : 0;
}
