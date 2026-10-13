#include <cuda.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define R(call) report(#call, (call))
static CUresult report(const char *name, CUresult result)
{
    printf("%s result=%d\n", name, result);
    return result;
}
struct exports {
    int memory, event;
    CUipcMemHandle mem;
    CUipcEventHandle ev;
};

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int handles[2], done[2];
    if (pipe(handles) || pipe(done)) return 1;
    /* Fork before initializing the Driver: each process creates its own context. */
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) {
        close(handles[1]); close(done[0]);
        struct exports e = {0};
        if (read(handles[0], &e, sizeof(e)) != sizeof(e)) _exit(1);
        CUdevice device; CUcontext context;
        if (R(cuInit(0)) || R(cuDeviceGet(&device, 0)) ||
            R(cuCtxCreate(&context, 0, device))) _exit(1);
        if (e.memory) {
            CUdeviceptr p = 0;
            if (!R(cuIpcOpenMemHandle(&p, e.mem, CU_IPC_MEM_LAZY_ENABLE_PEER_ACCESS))) {
                unsigned value = 0;
                if (R(cuMemcpyDtoH(&value, p, sizeof(value))) || value != 0x12345678) _exit(1);
                puts("IPC_MEMORY_DATA=PASS");
                if (R(cuIpcCloseMemHandle(p))) _exit(1);
            }
        } else puts("IPC_MEMORY_IMPORT=BLOCKED_BY_EXPORT");
        if (e.event) {
            CUevent event;
            if (!R(cuIpcOpenEventHandle(&event, e.ev))) {
                R(cuEventSynchronize(event)); R(cuEventDestroy(event));
            }
        } else puts("IPC_EVENT_IMPORT=BLOCKED_BY_EXPORT");
        R(cuCtxDestroy(context));
        char ok = 1;
        if (write(done[1], &ok, 1) != 1) _exit(1);
        _exit(0);
    }
    close(handles[0]); close(done[1]);
    CUdevice device; CUcontext context;
    if (R(cuInit(0)) || R(cuDeviceGet(&device, 0)) ||
        R(cuCtxCreate(&context, 0, device))) return 1;
    int count = -1, value = -1;
    if (R(cuDeviceGetCount(&count))) return 1;
    printf("PHYSICAL_DEVICE_COUNT=%d\n", count);
    R(cuDeviceCanAccessPeer(&value, device, device));
    printf("SAME_DEVICE_CAN_ACCESS=%d\n", value);
    R(cuDeviceGetP2PAttribute(&value, CU_DEVICE_P2P_ATTRIBUTE_ACCESS_SUPPORTED, device, device));
    R(cuDeviceGetAttribute(&value, CU_DEVICE_ATTRIBUTE_COOPERATIVE_MULTI_DEVICE_LAUNCH, device));
    printf("COOPERATIVE_MULTI_DEVICE_ATTRIBUTE=%d\n", value);
    if (count < 2) puts("DISTINCT_DEVICE_PEER_OPERATIONS=BLOCKED_SINGLE_DEVICE");
    struct exports e = {0};
    CUdeviceptr p = 0; CUevent event = NULL;
    if (R(cuMemAlloc(&p, 4096))) return 1;
    unsigned initial = 0x12345678;
    if (R(cuMemcpyHtoD(p, &initial, sizeof(initial)))) return 1;
    e.memory = R(cuIpcGetMemHandle(&e.mem, p)) == CUDA_SUCCESS;
    if (!R(cuEventCreate(&event, CU_EVENT_DISABLE_TIMING | CU_EVENT_INTERPROCESS))) {
        if (R(cuEventRecord(event, NULL)) || R(cuEventSynchronize(event))) return 1;
        e.event = R(cuIpcGetEventHandle(&e.ev, event)) == CUDA_SUCCESS;
    } else puts("IPC_EVENT_EXPORT=BLOCKED_BY_EVENT_CREATE");
    if (write(handles[1], &e, sizeof(e)) != sizeof(e)) return 1;
    char ok = 0;
    if (read(done[0], &ok, 1) != 1 || !ok) return 1;
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    if (event) R(cuEventDestroy(event));
    R(cuMemFree(p)); R(cuCtxDestroy(context));
    puts("PEER_IPC_PROBE=COMPLETE");
    return 0;
}
