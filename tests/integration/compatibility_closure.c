#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(call) do { cudaError_t result = (call); if (result) { \
    fprintf(stderr, "line=%d %s result=%d\n", __LINE__, #call, result); return 1; } } while (0)
#define EXPECT(call, expected) do { if ((call) != (expected)) return 1; } while (0)

static long locked_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long value = -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmLck: %ld kB", &value) == 1) break;
    fclose(f); return value;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int peer = 99;
    CHECK(cudaDeviceCanAccessPeer(&peer, 0, 0));
    if (peer) return 1;
    EXPECT(cudaDeviceCanAccessPeer(&peer, 0, 1), cudaErrorInvalidDevice);
    EXPECT(cudaDeviceCanAccessPeer(NULL, 0, 0), cudaErrorInvalidValue);
    struct cudaDeviceProp prop = {0}; int chosen = -1;
    prop.totalGlobalMem = (size_t)-1; prop.major = 99;
    CHECK(cudaChooseDevice(&chosen, &prop));
    if (chosen) return 1;
    puts("DEVICE_SELECTION_SELF_PEER=PASS");

    long before = locked_kb(); size_t page = (size_t)sysconf(_SC_PAGESIZE);
    char *host = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (host == MAP_FAILED || before < 0) return 1;
    CHECK(cudaHostRegister(host + 1, 64, 0));
    CHECK(cudaHostRegister(host + 128, 64, cudaHostRegisterPortable));
    unsigned flags = 99;
    CHECK(cudaHostGetFlags(&flags, host + 130));
    if (flags != cudaHostRegisterPortable || locked_kb() != before + (long)page / 1024) return 1;
    EXPECT(cudaHostRegister(host + 32, 64, 0), cudaErrorHostMemoryAlreadyRegistered);
    CHECK(cudaHostUnregister(host + 1));
    if (locked_kb() != before + (long)page / 1024) return 1;
    /* Cross-page range shares the still-owned first page. */
    CHECK(cudaHostRegister(host + page - 16, 32, 0));
    CHECK(cudaHostUnregister(host + 128));
    if (locked_kb() != before + (long)page * 2 / 1024) return 1;
    void *device = NULL; CHECK(cudaMalloc(&device, 32));
    memset(host + page - 16, 0x6d, 32);
    CHECK(cudaMemcpyAsync(device, host + page - 16, 32, cudaMemcpyHostToDevice, NULL));
    memset(host + page - 16, 0, 32);
    CHECK(cudaMemcpyAsync(host + page - 16, device, 32, cudaMemcpyDeviceToHost, NULL));
    CHECK(cudaHostUnregister(host + page - 16));
    for (int i = 0; i < 32; ++i) if (host[page - 16 + i] != 0x6d) return 1;
    if (locked_kb() != before) return 1;
    CHECK(cudaFree(device)); munmap(host, page * 2);
    puts("UNALIGNED_SHARED_PAGE_REGISTRATION_AND_RETIREMENT=PASS");

    FILE *f = fopen(argv[1], "r"); if (!f) return 1;
    char line[2048]; if (!fgets(line, sizeof(line), f)) return 1;
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *value = strtok(line, "\t"), *name = strtok(NULL, "\t");
        char *native_name = strtok(NULL, "\t"), *description = strtok(NULL, "\n");
        if (!value || !name || !native_name || !description) return 1;
        cudaError_t error = (cudaError_t)atoi(value);
        if (strcmp(cudaGetErrorName(error), name) || strcmp(cudaGetErrorString(error), description)) {
            fprintf(stderr, "error-catalog mismatch value=%s\n", value); return 1;
        }
        ++count;
    }
    fclose(f);
    if (count != 112 || strcmp(cudaGetErrorName((cudaError_t)123456), "unrecognized error code") ||
        strcmp(cudaGetErrorString((cudaError_t)123456), "internal error")) return 1;
    printf("ERROR_ENUM_NAMES_AND_NATIVE_DESCRIPTIONS=PASS count=%d\n", count);
    puts("COMPATIBILITY_CLOSURE_REMOTE=PASS");
    return 0;
}
