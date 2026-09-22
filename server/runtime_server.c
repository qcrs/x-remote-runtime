#include <arpa/inet.h>
#include <cuda.h>
#include "corex_metadata.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAGIC   0x43525839u  /* CRX9 */
#define VERSION 3u
#define PORT    50051

static int configured_port(void)
{
    const char *value = getenv("COREX_REMOTE_PORT");
    if (!value || !*value)
        return PORT;

    char *end = NULL;
    errno = 0;
    long port = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || port < 1 || port > 65535)
        return PORT;

    return (int)port;
}

#define MAX_ALLOCS   64
#define MAX_MODULES  32
#define MAX_KERNELS  128
#define MAX_STREAMS  64
#define MAX_EVENTS   128
#define MAX_TRANSFERS 128
#define MAX_ARGS     32
#define MAX_PAYLOAD  (64u * 1024u * 1024u)

#define STREAM_STATE_READY   0u
#define STREAM_STATE_PENDING 1u

#define TRANSFER_KIND_H2D 1u
#define TRANSFER_KIND_D2H 2u

enum {
    ST_OK             = 0,
    ST_BAD_REQUEST    = 1,
    ST_NOT_FOUND      = 2,
    ST_CUDA_ERROR     = 3,
    ST_INTERNAL       = 4,
    ST_NO_RESOURCE    = 5,
    ST_ABI_MISMATCH   = 6,
    ST_METADATA_ERROR = 7,
};

enum {
    OP_ALLOC          = 1,
    OP_H2D            = 2,
    OP_LAUNCH         = 3,  /* legacy Gate-2 opcode; not used by Gate 5B */
    OP_SYNC           = 4,
    OP_D2H            = 5,
    OP_FREE           = 6,
    OP_CLOSE          = 7,
    OP_UPLOAD_MODULE  = 8,
    OP_GET_KERNEL     = 9,
    OP_UNLOAD_MODULE  = 10,
    OP_LAUNCH_GENERIC = 11,
    OP_CREATE_STREAM  = 12,
    OP_DESTROY_STREAM = 13,
    OP_STREAM_QUERY   = 14,
    OP_STREAM_SYNC    = 15,
    OP_CREATE_EVENT   = 16,
    OP_DESTROY_EVENT  = 17,
    OP_EVENT_RECORD   = 18,
    OP_EVENT_QUERY    = 19,
    OP_EVENT_SYNC     = 20,
    OP_STREAM_WAIT_EVENT = 21,
    OP_H2D_ASYNC_SUBMIT = 22,
    OP_D2H_ASYNC_SUBMIT = 23,
    OP_TRANSFER_QUERY   = 24,
    OP_TRANSFER_WAIT    = 25,
    OP_GET_DEVICE_INFO  = 26,
};

enum {
    ARG_REMOTE_PTR = 1,
    ARG_I32        = 2,
    ARG_U64        = 3,
    ARG_F32        = 4,
    ARG_RAW_VALUE  = 5,
};

typedef struct {
    int used;
    uint64_t id;
    CUdeviceptr ptr;
    size_t size;
} Allocation;

typedef struct {
    int used;
    uint64_t id;

    CUmodule module;

    unsigned char *image;
    size_t image_size;

    char name[128];

    CorexModuleMeta metadata;
    int metadata_valid;
} ModuleEntry;

typedef struct {
    int used;
    uint64_t id;

    uint64_t module_id;
    CUfunction function;

    char name[128];

    CorexKernelMeta metadata;
    int metadata_valid;
} KernelEntry;

typedef struct {
    int used;
    uint64_t id;
    CUstream stream;
} StreamEntry;

typedef struct {
    int used;
    uint64_t id;
    CUevent event;
} EventEntry;

typedef struct {
    int used;
    uint64_t id;

    uint32_t kind;

    uint64_t allocation_id;
    uint64_t stream_id;

    CUevent done_event;

    void *host_buffer;
    size_t bytes;
} TransferEntry;

typedef union {
    CUdeviceptr ptr;
    int32_t i32;
    uint64_t u64;
    float f32;
    double force_alignment;
} KernelArgStorage;

static Allocation allocations[MAX_ALLOCS];
static ModuleEntry modules[MAX_MODULES];
static KernelEntry kernels[MAX_KERNELS];
static StreamEntry streams[MAX_STREAMS];
static EventEntry events[MAX_EVENTS];
static TransferEntry transfers[MAX_TRANSFERS];

static uint64_t next_alloc_id  = 1;
static uint64_t next_module_id = 1;
static uint64_t next_kernel_id = 1;
static uint64_t next_stream_id = 1;
static uint64_t next_event_id = 1;
static uint64_t next_transfer_id = 1;

static CUcontext g_ctx = NULL;
static CUdevice g_device = 0;

/* ============================================================
 * Byte order
 * ============================================================
 */

static uint64_t to_be64(uint64_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(v & 0xffffffffULL)) << 32) |
           htonl((uint32_t)(v >> 32));
#else
    return v;
#endif
}

static uint64_t from_be64(uint64_t v)
{
    return to_be64(v);
}

static uint32_t read_u32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

static uint64_t read_u64(const unsigned char *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return from_be64(v);
}

static void g8c_put_u32(
    unsigned char *buf,
    size_t *pos,
    uint32_t value)
{
    uint32_t wire = htonl(value);
    memcpy(buf + *pos, &wire, sizeof(wire));
    *pos += sizeof(wire);
}

static void g8c_put_u64(
    unsigned char *buf,
    size_t *pos,
    uint64_t value)
{
    uint64_t wire = to_be64(value);
    memcpy(buf + *pos, &wire, sizeof(wire));
    *pos += sizeof(wire);
}

/* ============================================================
 * Socket helpers
 * ============================================================
 */

static int send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        p += (size_t)n;
        len -= (size_t)n;
    }

    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;

    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        p += (size_t)n;
        len -= (size_t)n;
    }

    return 0;
}

static int send_response(
    int fd,
    uint32_t opcode,
    uint32_t req_id,
    uint32_t status,
    const void *payload,
    uint32_t payload_len)
{
    uint32_t h[6];

    h[0] = htonl(MAGIC);
    h[1] = htonl(VERSION);
    h[2] = htonl(opcode);
    h[3] = htonl(req_id);
    h[4] = htonl(status);
    h[5] = htonl(payload_len);

    if (send_all(fd, h, sizeof(h)) != 0)
        return -1;

    if (payload_len > 0 &&
        send_all(fd, payload, payload_len) != 0)
        return -1;

    return 0;
}

/* ============================================================
 * Resource table helpers
 * ============================================================
 */

static Allocation *find_allocation(uint64_t id)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (allocations[i].used && allocations[i].id == id)
            return &allocations[i];
    }

    return NULL;
}

static Allocation *new_allocation_slot(void)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (!allocations[i].used) {
            memset(&allocations[i], 0, sizeof(allocations[i]));
            allocations[i].used = 1;
            allocations[i].id = next_alloc_id++;
            return &allocations[i];
        }
    }

    return NULL;
}

static ModuleEntry *find_module(uint64_t id)
{
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (modules[i].used && modules[i].id == id)
            return &modules[i];
    }

    return NULL;
}

static ModuleEntry *new_module_slot(void)
{
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (!modules[i].used) {
            memset(&modules[i], 0, sizeof(modules[i]));
            modules[i].used = 1;
            modules[i].id = next_module_id++;
            return &modules[i];
        }
    }

    return NULL;
}

static KernelEntry *find_kernel(uint64_t id)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (kernels[i].used && kernels[i].id == id)
            return &kernels[i];
    }

    return NULL;
}

static KernelEntry *new_kernel_slot(void)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!kernels[i].used) {
            memset(&kernels[i], 0, sizeof(kernels[i]));
            kernels[i].used = 1;
            kernels[i].id = next_kernel_id++;
            return &kernels[i];
        }
    }

    return NULL;
}

static StreamEntry *find_stream(uint64_t id)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (streams[i].used && streams[i].id == id)
            return &streams[i];
    }

    return NULL;
}

static StreamEntry *new_stream_slot(void)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!streams[i].used) {
            memset(&streams[i], 0, sizeof(streams[i]));
            streams[i].used = 1;
            streams[i].id = next_stream_id++;
            return &streams[i];
        }
    }

    return NULL;
}


static EventEntry *find_event(uint64_t id)
{
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (events[i].used && events[i].id == id)
            return &events[i];
    }

    return NULL;
}

static TransferEntry *find_transfer(uint64_t id)
{
    for (int i = 0; i < MAX_TRANSFERS; ++i) {
        if (transfers[i].used &&
            transfers[i].id == id)
            return &transfers[i];
    }

    return NULL;
}

static TransferEntry *new_transfer_slot(void)
{
    for (int i = 0; i < MAX_TRANSFERS; ++i) {
        if (!transfers[i].used) {
            memset(&transfers[i], 0, sizeof(transfers[i]));
            transfers[i].used = 1;
            transfers[i].id = next_transfer_id++;
            return &transfers[i];
        }
    }

    return NULL;
}

static void release_transfer(TransferEntry *entry)
{
    if (!entry)
        return;

    if (entry->done_event)
        cuEventDestroy(entry->done_event);

    if (entry->host_buffer)
        cuMemFreeHost(entry->host_buffer);

    memset(entry, 0, sizeof(*entry));
}

/* ============================================================
 * Gate 5E — Conservative Blocking Destruction
 * ============================================================
 *
 * Correctness baseline:
 *
 *   Allocation FREE
 *       -> context barrier
 *
 *   Module UNLOAD
 *       -> context barrier
 *
 *   Stream DESTROY
 *       -> stream barrier
 *
 *   Event DESTROY
 *       -> context barrier
 *
 * This is intentionally conservative. Gate 5E freezes safe
 * lifetime semantics first; narrower refcount/deferred-release
 * optimization can be layered on later without changing the
 * externally visible ownership invariants.
 */

static int lifetime_context_barrier(
    const char *operation,
    const char *resource_type,
    uint64_t resource_id)
{
    CUresult r =
        cuCtxSynchronize();

    printf(
        "LIFETIME_BARRIER op=%s "
        "resource=%s id=%llu "
        "scope=CONTEXT rc=%d result=%s\n",
        operation,
        resource_type,
        (unsigned long long)resource_id,
        (int)r,
        r == CUDA_SUCCESS
            ? "PASS"
            : "FAIL");

    return r == CUDA_SUCCESS
        ? 0
        : -1;
}


static int lifetime_stream_barrier(
    const char *operation,
    uint64_t stream_id,
    CUstream stream)
{
    CUresult r =
        cuStreamSynchronize(
            stream);

    printf(
        "LIFETIME_BARRIER op=%s "
        "resource=STREAM id=%llu "
        "scope=STREAM rc=%d result=%s\n",
        operation,
        (unsigned long long)stream_id,
        (int)r,
        r == CUDA_SUCCESS
            ? "PASS"
            : "FAIL");

    return r == CUDA_SUCCESS
        ? 0
        : -1;
}





static EventEntry *new_event_slot(void)
{
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!events[i].used) {
            memset(&events[i], 0, sizeof(events[i]));
            events[i].used = 1;
            events[i].id = next_event_id++;
            return &events[i];
        }
    }

    return NULL;
}

static void invalidate_module_kernels(uint64_t module_id)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!kernels[i].used || kernels[i].module_id != module_id)
            continue;

        printf(
            "invalidate kernel_id=%llu module_id=%llu name=%s\n",
            (unsigned long long)kernels[i].id,
            (unsigned long long)module_id,
            kernels[i].name);

        memset(&kernels[i], 0, sizeof(kernels[i]));
    }
}

/* ============================================================
 * Session cleanup
 * ============================================================
 */

static void cleanup_session(void)
{
    /*
     * Gate 5E cleanup order:
     *
     * 1. Synchronize all explicit streams so event records,
     *    waits, and kernel work have completed.
     * 2. Destroy Event objects.
     * 3. Destroy Stream objects.
     * 4. Synchronize the context to catch default-stream work.
     * 5. Release allocations/kernels/modules.
     */
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!streams[i].used)
            continue;

        CUresult sr = cuStreamSynchronize(streams[i].stream);

        printf(
            "cleanup stream_id=%llu sync_rc=%d\n",
            (unsigned long long)streams[i].id,
            (int)sr);
    }

    /*
     * Gate 5D:
     * Stream synchronization above establishes completion for
     * every async transfer submitted to explicit streams.
     * Retire internal transfer events and pinned staging before
     * destroying user Event/Stream objects.
     */
    for (int i = 0; i < MAX_TRANSFERS; ++i) {
        if (!transfers[i].used)
            continue;

        CUresult tr =
            cuEventSynchronize(
                transfers[i].done_event);

        printf(
            "cleanup transfer_id=%llu "
            "kind=%s sync_rc=%d bytes=%zu\n",
            (unsigned long long)transfers[i].id,
            transfers[i].kind == TRANSFER_KIND_H2D
                ? "H2D"
                : "D2H",
            (int)tr,
            transfers[i].bytes);

        release_transfer(
            &transfers[i]);
    }

    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!events[i].used)
            continue;

        CUresult er = cuEventDestroy(events[i].event);

        printf(
            "cleanup event_id=%llu destroy_rc=%d\n",
            (unsigned long long)events[i].id,
            (int)er);

        memset(&events[i], 0, sizeof(events[i]));
    }

    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!streams[i].used)
            continue;

        CUresult dr = cuStreamDestroy(streams[i].stream);

        printf(
            "cleanup stream_id=%llu destroy_rc=%d\n",
            (unsigned long long)streams[i].id,
            (int)dr);

        memset(&streams[i], 0, sizeof(streams[i]));
    }

    if (g_ctx) {
        CUresult r = cuCtxSynchronize();
        printf("cleanup_sync rc=%d\n", (int)r);
    }

    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (!allocations[i].used)
            continue;

        printf(
            "cleanup allocation_id=%llu\n",
            (unsigned long long)allocations[i].id);

        if (allocations[i].ptr)
            cuMemFree(allocations[i].ptr);

        memset(&allocations[i], 0, sizeof(allocations[i]));
    }

    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!kernels[i].used)
            continue;

        printf(
            "cleanup kernel_id=%llu module_id=%llu name=%s\n",
            (unsigned long long)kernels[i].id,
            (unsigned long long)kernels[i].module_id,
            kernels[i].name);

        memset(&kernels[i], 0, sizeof(kernels[i]));
    }

    for (int i = 0; i < MAX_MODULES; ++i) {
        if (!modules[i].used)
            continue;

        printf(
            "cleanup module_id=%llu name=%s\n",
            (unsigned long long)modules[i].id,
            modules[i].name);

        if (modules[i].module)
            cuModuleUnload(modules[i].module);

        free(modules[i].image);

        memset(&modules[i], 0, sizeof(modules[i]));
    }
}

/* ============================================================
 * Gate 2-style allocation/copy handlers
 * ============================================================
 */

static int handle_alloc(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_ALLOC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t size64 = read_u64(payload);

    if (size64 == 0 || size64 > (uint64_t)SIZE_MAX)
        return send_response(fd, OP_ALLOC, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *slot = new_allocation_slot();

    if (!slot)
        return send_response(fd, OP_ALLOC, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = cuMemAlloc(&slot->ptr, (size_t)size64);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_ALLOC, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    slot->size = (size_t)size64;

    uint64_t wire_id = to_be64(slot->id);

    printf(
        "ALLOC request=%u allocation_id=%llu size=%zu\n",
        req_id,
        (unsigned long long)slot->id,
        slot->size);

    return send_response(
        fd,
        OP_ALLOC,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}

static int handle_h2d(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* id:u64 + offset:u64 + bytes:u64 + data */
    if (len < 24)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = read_u64(payload + 0);
    uint64_t offset64      = read_u64(payload + 8);
    uint64_t bytes64       = read_u64(payload + 16);

    if (bytes64 > UINT32_MAX ||
        24ULL + bytes64 != (uint64_t)len)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *a = find_allocation(allocation_id);

    if (!a)
        return send_response(fd, OP_H2D, req_id, ST_NOT_FOUND, NULL, 0);

    if (offset64 > a->size ||
        bytes64 > a->size - (size_t)offset64)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    CUresult r = cuMemcpyHtoD(
        a->ptr + (size_t)offset64,
        payload + 24,
        (size_t)bytes64);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_H2D, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "H2D request=%u allocation_id=%llu offset=%llu bytes=%llu\n",
        req_id,
        (unsigned long long)allocation_id,
        (unsigned long long)offset64,
        (unsigned long long)bytes64);

    return send_response(fd, OP_H2D, req_id, ST_OK, NULL, 0);
}

static int handle_d2h(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* id:u64 + offset:u64 + bytes:u64 */
    if (len != 24)
        return send_response(fd, OP_D2H, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = read_u64(payload + 0);
    uint64_t offset64      = read_u64(payload + 8);
    uint64_t bytes64       = read_u64(payload + 16);

    if (bytes64 > UINT32_MAX)
        return send_response(fd, OP_D2H, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *a = find_allocation(allocation_id);

    if (!a)
        return send_response(fd, OP_D2H, req_id, ST_NOT_FOUND, NULL, 0);

    if (offset64 > a->size ||
        bytes64 > a->size - (size_t)offset64)
        return send_response(fd, OP_D2H, req_id, ST_BAD_REQUEST, NULL, 0);

    unsigned char *out = NULL;

    if (bytes64 > 0) {
        out = (unsigned char *)malloc((size_t)bytes64);
        if (!out)
            return send_response(fd, OP_D2H, req_id, ST_INTERNAL, NULL, 0);
    }

    CUresult r = cuMemcpyDtoH(
        out,
        a->ptr + (size_t)offset64,
        (size_t)bytes64);

    if (r != CUDA_SUCCESS) {
        free(out);
        return send_response(fd, OP_D2H, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    printf(
        "D2H request=%u allocation_id=%llu offset=%llu bytes=%llu\n",
        req_id,
        (unsigned long long)allocation_id,
        (unsigned long long)offset64,
        (unsigned long long)bytes64);

    int rc = send_response(
        fd,
        OP_D2H,
        req_id,
        ST_OK,
        out,
        (uint32_t)bytes64);

    free(out);
    return rc;
}

static int handle_free(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_FREE, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = read_u64(payload);
    Allocation *a = find_allocation(allocation_id);

    if (!a)
        return send_response(fd, OP_FREE, req_id, ST_NOT_FOUND, NULL, 0);

    /*
     * Gate 5E invariant:
     * an AllocationID is never physically freed until all
     * previously submitted GPU work in the context is complete.
     *
     * This covers:
     *   - in-flight kernel reads/writes
     *   - H2D/D2H async copies
     *   - cross-stream/event dependencies
     */
    if (lifetime_context_barrier(
            "FREE",
            "ALLOCATION",
            allocation_id) != 0) {

        return send_response(
            fd,
            OP_FREE,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    CUresult r =
        cuMemFree(
            a->ptr);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_FREE, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "FREE request=%u allocation_id=%llu\n",
        req_id,
        (unsigned long long)allocation_id);

    memset(a, 0, sizeof(*a));

    return send_response(fd, OP_FREE, req_id, ST_OK, NULL, 0);
}

static int handle_sync(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    (void)payload;

    if (len != 0)
        return send_response(fd, OP_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    CUresult r = cuCtxSynchronize();

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_SYNC, req_id, ST_CUDA_ERROR, NULL, 0);

    printf("SYNC request=%u\n", req_id);

    return send_response(fd, OP_SYNC, req_id, ST_OK, NULL, 0);
}

/* ============================================================
 * Dynamic module / kernel handlers
 * ============================================================
 */

static int handle_upload_module(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /*
     * payload:
     *   u32 name_len
     *   name bytes (no trailing NUL)
     *   u64 image_size
     *   image bytes
     */
    if (len < 12)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    uint32_t name_len = read_u32(payload);

    if (name_len == 0 || name_len >= 128)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    if ((uint64_t)4 + name_len + 8 > len)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    char name[128];
    memset(name, 0, sizeof(name));
    memcpy(name, payload + 4, name_len);

    /* Keep upload names simple: basename only, CoreX image suffix. */
    if (strchr(name, '/') || strstr(name, "..") ||
        name_len < 6 || strcmp(name + name_len - 6, ".cubin") != 0)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    size_t size_pos = 4u + name_len;
    uint64_t image_size64 = read_u64(payload + size_pos);
    size_t image_pos = size_pos + 8;

    if (image_size64 == 0 ||
        image_size64 > SIZE_MAX ||
        image_pos + image_size64 != len)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    size_t image_size = (size_t)image_size64;

    if (image_size < 4 ||
        payload[image_pos + 0] != 0x7f ||
        payload[image_pos + 1] != 'E' ||
        payload[image_pos + 2] != 'L' ||
        payload[image_pos + 3] != 'F')
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    unsigned char *image = (unsigned char *)malloc(image_size);

    if (!image)
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_INTERNAL, NULL, 0);

    memcpy(image, payload + image_pos, image_size);

    CorexModuleMeta parsed_metadata;
    char metadata_error[256];

    if (corex_parse_metadata(
            image,
            image_size,
            &parsed_metadata,
            metadata_error,
            sizeof(metadata_error)) != 0) {

        fprintf(
            stderr,
            "UPLOAD_MODULE metadata_parse=FAIL name=%s error=%s\n",
            name,
            metadata_error);

        free(image);

        return send_response(
            fd,
            OP_UPLOAD_MODULE,
            req_id,
            ST_METADATA_ERROR,
            NULL,
            0);
    }

    printf(
        "UPLOAD_METADATA version=%u.%u kernel_count=%u\n",
        parsed_metadata.version_major,
        parsed_metadata.version_minor,
        parsed_metadata.kernel_count);

    CUmodule module = NULL;
    CUresult r = cuModuleLoadData(&module, image);

    if (r != CUDA_SUCCESS) {
        free(image);
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    ModuleEntry *slot = new_module_slot();

    if (!slot) {
        cuModuleUnload(module);
        free(image);
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_NO_RESOURCE, NULL, 0);
    }

    slot->module = module;
    slot->image = image;
    slot->image_size = image_size;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->metadata = parsed_metadata;
    slot->metadata_valid = 1;

    uint64_t wire_id = to_be64(slot->id);

    printf(
        "UPLOAD_MODULE request=%u module_id=%llu name=%s size=%zu magic=%02x%02x%02x%02x\n",
        req_id,
        (unsigned long long)slot->id,
        slot->name,
        slot->image_size,
        slot->image[0],
        slot->image[1],
        slot->image[2],
        slot->image[3]);

    return send_response(
        fd,
        OP_UPLOAD_MODULE,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}

static int handle_get_kernel(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* u64 module_id + u32 name_len + name */
    if (len < 12)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t module_id = read_u64(payload);
    uint32_t name_len = read_u32(payload + 8);

    if (name_len == 0 || name_len >= 128 ||
        12ULL + name_len != len)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_BAD_REQUEST, NULL, 0);

    char name[128];
    memset(name, 0, sizeof(name));
    memcpy(name, payload + 12, name_len);

    ModuleEntry *m = find_module(module_id);

    if (!m)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_NOT_FOUND, NULL, 0);

    if (!m->metadata_valid)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_METADATA_ERROR, NULL, 0);

    const CorexKernelMeta *kernel_meta =
        corex_find_kernel_meta(&m->metadata, name);

    if (!kernel_meta) {
        fprintf(
            stderr,
            "GET_KERNEL metadata kernel not found module_id=%llu name=%s\n",
            (unsigned long long)module_id,
            name);

        return send_response(fd, OP_GET_KERNEL, req_id, ST_METADATA_ERROR, NULL, 0);
    }

    CUfunction function = NULL;
    CUresult r = cuModuleGetFunction(&function, m->module, name);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_NOT_FOUND, NULL, 0);

    KernelEntry *slot = new_kernel_slot();

    if (!slot)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_NO_RESOURCE, NULL, 0);

    slot->module_id = module_id;
    slot->function = function;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->metadata = *kernel_meta;
    slot->metadata_valid = 1;

    printf(
        "GET_KERNEL_METADATA name=%s argc=%u kernarg_size=%u kernarg_align=%u\n",
        kernel_meta->name,
        kernel_meta->argc,
        kernel_meta->kernarg_segment_size,
        kernel_meta->kernarg_segment_align);

    uint64_t wire_id = to_be64(slot->id);

    printf(
        "GET_KERNEL request=%u module_id=%llu kernel_id=%llu name=%s\n",
        req_id,
        (unsigned long long)module_id,
        (unsigned long long)slot->id,
        slot->name);

    return send_response(
        fd,
        OP_GET_KERNEL,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}

static int handle_unload_module(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_UNLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t module_id = read_u64(payload);
    ModuleEntry *m = find_module(module_id);

    if (!m)
        return send_response(fd, OP_UNLOAD_MODULE, req_id, ST_NOT_FOUND, NULL, 0);

    /*
     * Gate 5E:
     * CUmodule cannot be unloaded while a submitted kernel may
     * still execute code from that module.
     */
    if (lifetime_context_barrier(
            "UNLOAD_MODULE",
            "MODULE",
            module_id) != 0) {

        return send_response(
            fd,
            OP_UNLOAD_MODULE,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    invalidate_module_kernels(module_id);

    CUresult r = cuModuleUnload(m->module);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_UNLOAD_MODULE, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "UNLOAD_MODULE request=%u module_id=%llu name=%s\n",
        req_id,
        (unsigned long long)module_id,
        m->name);

    free(m->image);
    memset(m, 0, sizeof(*m));

    return send_response(fd, OP_UNLOAD_MODULE, req_id, ST_OK, NULL, 0);
}

static int resolve_stream_id(uint64_t stream_id, CUstream *stream_out);

/* ============================================================
 * Gate 5B Stream handlers
 * ============================================================
 */

static int handle_create_stream(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 4)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    uint32_t flags = read_u32(payload);

    if (flags != CU_STREAM_DEFAULT)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    StreamEntry *slot = new_stream_slot();

    if (!slot)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = cuStreamCreate(&slot->stream, flags);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    uint64_t wire_id = to_be64(slot->id);

    printf(
        "CREATE_STREAM request=%u stream_id=%llu flags=%u\n",
        req_id,
        (unsigned long long)slot->id,
        flags);

    return send_response(
        fd,
        OP_CREATE_STREAM,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}

static int handle_stream_query(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_STREAM_QUERY, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = read_u64(payload);
    CUstream stream = 0;
    if (resolve_stream_id(stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_QUERY, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuStreamQuery(stream);
    uint32_t state;

    if (r == CUDA_SUCCESS) {
        state = STREAM_STATE_READY;
    } else if (r == CUDA_ERROR_NOT_READY) {
        state = STREAM_STATE_PENDING;
    } else {
        fprintf(
            stderr,
            "STREAM_QUERY stream_id=%llu rc=%d\n",
            (unsigned long long)stream_id,
            (int)r);

        return send_response(fd, OP_STREAM_QUERY, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    uint32_t wire_state = htonl(state);

    printf(
        "STREAM_QUERY request=%u stream_id=%llu state=%s\n",
        req_id,
        (unsigned long long)stream_id,
        state == STREAM_STATE_READY ? "READY" : "PENDING");

    return send_response(
        fd,
        OP_STREAM_QUERY,
        req_id,
        ST_OK,
        &wire_state,
        sizeof(wire_state));
}

static int handle_stream_sync(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = read_u64(payload);
    CUstream stream = 0;
    if (resolve_stream_id(stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuStreamSynchronize(stream);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "STREAM_SYNC request=%u stream_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)stream_id);

    return send_response(fd, OP_STREAM_SYNC, req_id, ST_OK, NULL, 0);
}

static int handle_destroy_stream(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_DESTROY_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = read_u64(payload);
    StreamEntry *entry = find_stream(stream_id);

    if (!entry)
        return send_response(fd, OP_DESTROY_STREAM, req_id, ST_NOT_FOUND, NULL, 0);

    /*
     * Gate 5E:
     * Stream destruction is a per-stream blocking barrier.
     * TransferID objects are intentionally NOT retired here;
     * after stream completion their internal events/pinned
     * staging remain valid until TRANSFER_WAIT or session
     * cleanup retires them.
     */
    if (lifetime_stream_barrier(
            "DESTROY_STREAM",
            stream_id,
            entry->stream) != 0) {

        return send_response(
            fd,
            OP_DESTROY_STREAM,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    CUresult r =
        cuStreamDestroy(
            entry->stream);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_DESTROY_STREAM, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "DESTROY_STREAM request=%u stream_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)stream_id);

    memset(entry, 0, sizeof(*entry));

    return send_response(fd, OP_DESTROY_STREAM, req_id, ST_OK, NULL, 0);
}

/* ============================================================
 * Gate 5C Event handlers
 * ============================================================
 */

static int resolve_stream_id(
    uint64_t stream_id,
    CUstream *stream_out)
{
    if (stream_id == 0) {
        *stream_out = 0;
        return 0;
    }

    StreamEntry *entry = find_stream(stream_id);

    if (!entry)
        return -1;

    *stream_out = entry->stream;
    return 0;
}

static int handle_create_event(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 4)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint32_t flags = read_u32(payload);

    if (flags != CU_EVENT_DEFAULT)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    EventEntry *slot = new_event_slot();

    if (!slot)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = cuEventCreate(&slot->event, flags);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    uint64_t wire_id = to_be64(slot->id);

    printf(
        "CREATE_EVENT request=%u event_id=%llu flags=%u\n",
        req_id,
        (unsigned long long)slot->id,
        flags);

    return send_response(
        fd,
        OP_CREATE_EVENT,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}

static int handle_event_record(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 16)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = read_u64(payload);
    uint64_t stream_id = read_u64(payload + 8);

    EventEntry *event_entry = find_event(event_id);

    if (!event_entry)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_NOT_FOUND, NULL, 0);

    CUstream stream = 0;

    if (resolve_stream_id(stream_id, &stream) != 0)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuEventRecord(event_entry->event, stream);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "EVENT_RECORD request=%u event_id=%llu stream_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)event_id,
        (unsigned long long)stream_id);

    return send_response(fd, OP_EVENT_RECORD, req_id, ST_OK, NULL, 0);
}

static int handle_event_query(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_EVENT_QUERY, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = read_u64(payload);
    EventEntry *entry = find_event(event_id);

    if (!entry)
        return send_response(fd, OP_EVENT_QUERY, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuEventQuery(entry->event);
    uint32_t state;

    if (r == CUDA_SUCCESS) {
        state = STREAM_STATE_READY;
    } else if (r == CUDA_ERROR_NOT_READY) {
        state = STREAM_STATE_PENDING;
    } else {
        fprintf(
            stderr,
            "EVENT_QUERY event_id=%llu rc=%d\n",
            (unsigned long long)event_id,
            (int)r);

        return send_response(fd, OP_EVENT_QUERY, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    uint32_t wire_state = htonl(state);

    printf(
        "EVENT_QUERY request=%u event_id=%llu state=%s\n",
        req_id,
        (unsigned long long)event_id,
        state == STREAM_STATE_READY ? "READY" : "PENDING");

    return send_response(
        fd,
        OP_EVENT_QUERY,
        req_id,
        ST_OK,
        &wire_state,
        sizeof(wire_state));
}

static int handle_event_sync(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = read_u64(payload);
    EventEntry *entry = find_event(event_id);

    if (!entry)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuEventSynchronize(entry->event);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "EVENT_SYNC request=%u event_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)event_id);

    return send_response(fd, OP_EVENT_SYNC, req_id, ST_OK, NULL, 0);
}

static int handle_stream_wait_event(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 20)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = read_u64(payload);
    uint64_t event_id = read_u64(payload + 8);
    uint32_t flags = read_u32(payload + 16);

    if (flags != 0)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    CUstream stream = 0;

    if (resolve_stream_id(stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    EventEntry *event_entry = find_event(event_id);

    if (!event_entry)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = cuStreamWaitEvent(stream, event_entry->event, flags);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "STREAM_WAIT_EVENT request=%u stream_id=%llu event_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)stream_id,
        (unsigned long long)event_id);

    return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_OK, NULL, 0);
}

static int handle_destroy_event(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_DESTROY_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = read_u64(payload);
    EventEntry *entry = find_event(event_id);

    if (!entry)
        return send_response(fd, OP_DESTROY_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    /*
     * Gate 5E:
     *
     * Event lifetime is stronger than merely waiting for the
     * producer event to become complete. Another stream may
     * already contain cuStreamWaitEvent(event) followed by
     * downstream work.
     *
     * The V1 correctness policy therefore uses a context
     * barrier before physical Event destruction. This proves
     * all producer + waiter + downstream work is complete.
     */
    if (lifetime_context_barrier(
            "DESTROY_EVENT",
            "EVENT",
            event_id) != 0) {

        return send_response(
            fd,
            OP_DESTROY_EVENT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    CUresult r =
        cuEventDestroy(
            entry->event);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_DESTROY_EVENT, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "DESTROY_EVENT request=%u event_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)event_id);

    memset(entry, 0, sizeof(*entry));

    return send_response(fd, OP_DESTROY_EVENT, req_id, ST_OK, NULL, 0);
}

/* ============================================================
 * Generic argument ABI helpers
 * ============================================================
 */

static const char *wire_arg_kind_name(uint32_t kind)
{
    switch (kind) {
    case ARG_REMOTE_PTR:
        return "REMOTE_PTR";
    case ARG_I32:
        return "I32";
    case ARG_U64:
        return "U64";
    case ARG_F32:
        return "F32";
    case ARG_RAW_VALUE:
        return "RAW_VALUE";
    default:
        return "UNKNOWN";
    }
}

static uint32_t wire_arg_payload_size(uint32_t kind)
{
    switch (kind) {
    case ARG_REMOTE_PTR:
        return 16;
    case ARG_I32:
        return 4;
    case ARG_U64:
        return 8;
    case ARG_F32:
        return 4;
    case ARG_RAW_VALUE:
        return 0; /* variable; validated against metadata size */
    default:
        return 0;
    }
}

static uint32_t wire_arg_abi_size(uint32_t kind, uint32_t payload_size)
{
    switch (kind) {
    case ARG_REMOTE_PTR:
        return (uint32_t)sizeof(CUdeviceptr);
    case ARG_I32:
        return 4;
    case ARG_U64:
        return 8;
    case ARG_F32:
        return 4;
    case ARG_RAW_VALUE:
        return payload_size;
    default:
        return 0;
    }
}

static CorexMetaArgKind wire_arg_expected_meta_kind(uint32_t kind)
{
    switch (kind) {
    case ARG_REMOTE_PTR:
        return COREX_META_ARG_GLOBAL_BUFFER;
    case ARG_I32:
    case ARG_U64:
    case ARG_F32:
    case ARG_RAW_VALUE:
        return COREX_META_ARG_BY_VALUE;
    default:
        return COREX_META_ARG_UNKNOWN;
    }
}

static void release_raw_kernel_args(void **raw_args, uint32_t argc)
{
    for (uint32_t i = 0; i < argc; ++i) {
        free(raw_args[i]);
        raw_args[i] = NULL;
    }
}

/* ============================================================
 * Gate 6D generic launch: Gate-4 typed args + RAW_VALUE extension
 * ============================================================
 */

static int handle_launch_generic(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len < 48)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_BAD_REQUEST, NULL, 0);

    size_t pos = 0;

    uint64_t kernel_id = read_u64(payload + pos); pos += 8;
    uint64_t stream_id = read_u64(payload + pos); pos += 8;

    uint32_t grid_x = read_u32(payload + pos); pos += 4;
    uint32_t grid_y = read_u32(payload + pos); pos += 4;
    uint32_t grid_z = read_u32(payload + pos); pos += 4;

    uint32_t block_x = read_u32(payload + pos); pos += 4;
    uint32_t block_y = read_u32(payload + pos); pos += 4;
    uint32_t block_z = read_u32(payload + pos); pos += 4;

    uint32_t shared_mem = read_u32(payload + pos); pos += 4;
    uint32_t argc       = read_u32(payload + pos); pos += 4;

    if (argc > MAX_ARGS)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_BAD_REQUEST, NULL, 0);

    KernelEntry *k = find_kernel(kernel_id);
    if (!k)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_NOT_FOUND, NULL, 0);
    if (!k->metadata_valid)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_METADATA_ERROR, NULL, 0);

    if (argc != k->metadata.argc) {
        fprintf(stderr,
                "ABI_MISMATCH argc kernel=%s client=%u metadata=%u\n",
                k->name,
                argc,
                k->metadata.argc);
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_ABI_MISMATCH, NULL, 0);
    }

    CUstream launch_stream = 0;
    if (stream_id != 0) {
        StreamEntry *stream_entry = find_stream(stream_id);
        if (!stream_entry) {
            fprintf(stderr,
                    "LAUNCH_GENERIC stream_not_found stream_id=%llu\n",
                    (unsigned long long)stream_id);
            return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_NOT_FOUND, NULL, 0);
        }
        launch_stream = stream_entry->stream;
    }

    printf("LAUNCH_STREAM_RESOLVE request=%u kernel_id=%llu stream_id=%llu mode=%s\n",
           req_id,
           (unsigned long long)kernel_id,
           (unsigned long long)stream_id,
           stream_id == 0 ? "DEFAULT" : "EXPLICIT");

    printf("LAUNCH_GENERIC request=%u kernel_id=%llu kernel=%s stream_id=%llu "
           "grid=(%u,%u,%u) block=(%u,%u,%u) shared=%u argc=%u\n",
           req_id,
           (unsigned long long)kernel_id,
           k->name,
           (unsigned long long)stream_id,
           grid_x, grid_y, grid_z,
           block_x, block_y, block_z,
           shared_mem,
           argc);

    KernelArgStorage storage[MAX_ARGS];
    void *kernel_params[MAX_ARGS];
    void *raw_args[MAX_ARGS];

    memset(storage, 0, sizeof(storage));
    memset(kernel_params, 0, sizeof(kernel_params));
    memset(raw_args, 0, sizeof(raw_args));

    uint32_t fail_status = ST_OK;

    for (uint32_t i = 0; i < argc; ++i) {
        if (pos + 8 > len) {
            fail_status = ST_BAD_REQUEST;
            goto fail;
        }

        uint32_t kind = read_u32(payload + pos); pos += 4;
        uint32_t arg_size = read_u32(payload + pos); pos += 4;

        if ((uint64_t)pos + arg_size > len) {
            fail_status = ST_BAD_REQUEST;
            goto fail;
        }

        if (kind != ARG_RAW_VALUE) {
            uint32_t expected_wire_size = wire_arg_payload_size(kind);
            if (expected_wire_size == 0 || arg_size != expected_wire_size) {
                fprintf(stderr,
                        "ARG_WIRE_INVALID arg=%u kind=%u payload=%u expected=%u\n",
                        i,
                        kind,
                        arg_size,
                        expected_wire_size);
                fail_status = ST_BAD_REQUEST;
                goto fail;
            }
        } else if (arg_size == 0) {
            fail_status = ST_BAD_REQUEST;
            goto fail;
        }

        const CorexArgMeta *meta_arg = &k->metadata.args[i];
        CorexMetaArgKind expected_meta_kind = wire_arg_expected_meta_kind(kind);
        uint32_t abi_size = wire_arg_abi_size(kind, arg_size);

        if (expected_meta_kind == COREX_META_ARG_UNKNOWN ||
            meta_arg->kind != expected_meta_kind ||
            meta_arg->size != abi_size) {

            fprintf(stderr,
                    "ABI_MISMATCH arg=%u client_kind=%s client_abi_size=%u "
                    "meta_kind=%s meta_size=%u meta_offset=%u\n",
                    i,
                    wire_arg_kind_name(kind),
                    abi_size,
                    corex_meta_arg_kind_name(meta_arg->kind),
                    meta_arg->size,
                    meta_arg->offset);

            fail_status = ST_ABI_MISMATCH;
            goto fail;
        }

        printf("  ABI_VALIDATE arg[%u] client=%s wire_payload=%u abi_size=%u "
               "metadata_kind=%s metadata_offset=%u metadata_size=%u result=PASS\n",
               i,
               wire_arg_kind_name(kind),
               arg_size,
               abi_size,
               corex_meta_arg_kind_name(meta_arg->kind),
               meta_arg->offset,
               meta_arg->size);

        switch (kind) {
        case ARG_REMOTE_PTR: {
            uint64_t allocation_id = read_u64(payload + pos);
            uint64_t offset64      = read_u64(payload + pos + 8);
            Allocation *a = find_allocation(allocation_id);

            if (!a) {
                fail_status = ST_NOT_FOUND;
                goto fail;
            }
            if (offset64 >= a->size) {
                fail_status = ST_BAD_REQUEST;
                goto fail;
            }

            storage[i].ptr = a->ptr + (size_t)offset64;
            kernel_params[i] = &storage[i].ptr;

            printf("  arg[%u] REMOTE_PTR allocation_id=%llu offset=%llu\n",
                   i,
                   (unsigned long long)allocation_id,
                   (unsigned long long)offset64);
            break;
        }

        case ARG_I32: {
            uint32_t bits = read_u32(payload + pos);
            storage[i].i32 = (int32_t)bits;
            kernel_params[i] = &storage[i].i32;
            printf("  arg[%u] I32 value=%d\n", i, storage[i].i32);
            break;
        }

        case ARG_U64:
            storage[i].u64 = read_u64(payload + pos);
            kernel_params[i] = &storage[i].u64;
            printf("  arg[%u] U64 value=%llu\n",
                   i,
                   (unsigned long long)storage[i].u64);
            break;

        case ARG_F32: {
            uint32_t bits = read_u32(payload + pos);
            memcpy(&storage[i].f32, &bits, sizeof(bits));
            kernel_params[i] = &storage[i].f32;
            printf("  arg[%u] F32 value=%f\n", i, storage[i].f32);
            break;
        }

        case ARG_RAW_VALUE:
            raw_args[i] = malloc(arg_size);
            if (!raw_args[i]) {
                fail_status = ST_INTERNAL;
                goto fail;
            }
            memcpy(raw_args[i], payload + pos, arg_size);
            kernel_params[i] = raw_args[i];
            printf("  arg[%u] RAW_VALUE bytes=%u\n", i, arg_size);
            break;

        default:
            fail_status = ST_BAD_REQUEST;
            goto fail;
        }

        pos += arg_size;
    }

    if (pos != len) {
        fail_status = ST_BAD_REQUEST;
        goto fail;
    }

    printf("ABI_VALIDATE kernel=%s argc=%u result=PASS\n", k->name, argc);

    CUresult r = cuLaunchKernel(
        k->function,
        grid_x, grid_y, grid_z,
        block_x, block_y, block_z,
        shared_mem,
        launch_stream,
        kernel_params,
        NULL);

    release_raw_kernel_args(raw_args, argc);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "LAUNCH_GENERIC cuLaunchKernel rc=%d\n", (int)r);
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    printf("LAUNCH_GENERIC submit=PASS\n");
    return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_OK, NULL, 0);

fail:
    release_raw_kernel_args(raw_args, argc);
    return send_response(fd, OP_LAUNCH_GENERIC, req_id, fail_status, NULL, 0);
}

/* ============================================================
 * Session RPC loop
 * ============================================================
 */


/* ============================================================
 * Gate 5D — Remote Async Transfer + Pinned Staging
 * ============================================================
 */

/*
 * H2D_ASYNC_SUBMIT
 *
 * request:
 *   u64 allocation_id
 *   u64 offset
 *   u64 stream_id
 *   u64 bytes
 *   byte data[bytes]
 *
 * response:
 *   u64 TransferID
 *
 * Ownership:
 *   Server copies RPC bytes into pinned staging memory.
 *   That staging remains alive until TRANSFER_WAIT retires the
 *   TransferID (or session cleanup runs).
 */
static int handle_h2d_async_submit(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len < 32)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint64_t allocation_id = read_u64(payload + 0);
    uint64_t offset64      = read_u64(payload + 8);
    uint64_t stream_id     = read_u64(payload + 16);
    uint64_t bytes64       = read_u64(payload + 24);

    if (bytes64 == 0 ||
        bytes64 > UINT32_MAX ||
        32ULL + bytes64 != (uint64_t)len)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    Allocation *a =
        find_allocation(
            allocation_id);

    if (!a)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    if (offset64 > a->size ||
        bytes64 > a->size - (size_t)offset64)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    CUstream stream = 0;

    if (resolve_stream_id(
            stream_id,
            &stream) != 0)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    TransferEntry *entry =
        new_transfer_slot();

    if (!entry)
        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_NO_RESOURCE,
            NULL,
            0);

    entry->kind =
        TRANSFER_KIND_H2D;

    entry->allocation_id =
        allocation_id;

    entry->stream_id =
        stream_id;

    entry->bytes =
        (size_t)bytes64;

    CUresult r =
        cuMemAllocHost(
            &entry->host_buffer,
            entry->bytes);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    memcpy(
        entry->host_buffer,
        payload + 32,
        entry->bytes);

    r =
        cuEventCreate(
            &entry->done_event,
            CU_EVENT_DEFAULT);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    r =
        cuMemcpyHtoDAsync(
            a->ptr + (size_t)offset64,
            entry->host_buffer,
            entry->bytes,
            stream);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    r =
        cuEventRecord(
            entry->done_event,
            stream);

    if (r != CUDA_SUCCESS) {
        /*
         * H2D may already be in flight and still reference the
         * pinned staging buffer. Synchronize before freeing it.
         */
        (void)cuStreamSynchronize(stream);
        release_transfer(entry);

        return send_response(
            fd,
            OP_H2D_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    uint64_t transfer_id =
        entry->id;

    uint64_t wire_id =
        to_be64(
            transfer_id);

    printf(
        "H2D_ASYNC_SUBMIT request=%u "
        "transfer_id=%llu "
        "allocation_id=%llu offset=%llu "
        "stream_id=%llu bytes=%llu "
        "staging=PINNED\n",
        req_id,
        (unsigned long long)transfer_id,
        (unsigned long long)allocation_id,
        (unsigned long long)offset64,
        (unsigned long long)stream_id,
        (unsigned long long)bytes64);

    return send_response(
        fd,
        OP_H2D_ASYNC_SUBMIT,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}


/*
 * D2H_ASYNC_SUBMIT
 *
 * request:
 *   u64 allocation_id
 *   u64 offset
 *   u64 stream_id
 *   u64 bytes
 *
 * response:
 *   u64 TransferID
 *
 * No data is returned here. The pinned output buffer remains
 * owned by TransferID until TRANSFER_WAIT.
 */
static int handle_d2h_async_submit(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 32)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint64_t allocation_id = read_u64(payload + 0);
    uint64_t offset64      = read_u64(payload + 8);
    uint64_t stream_id     = read_u64(payload + 16);
    uint64_t bytes64       = read_u64(payload + 24);

    if (bytes64 == 0 ||
        bytes64 > UINT32_MAX ||
        bytes64 > MAX_PAYLOAD)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    Allocation *a =
        find_allocation(
            allocation_id);

    if (!a)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    if (offset64 > a->size ||
        bytes64 > a->size - (size_t)offset64)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    CUstream stream = 0;

    if (resolve_stream_id(
            stream_id,
            &stream) != 0)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    TransferEntry *entry =
        new_transfer_slot();

    if (!entry)
        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_NO_RESOURCE,
            NULL,
            0);

    entry->kind =
        TRANSFER_KIND_D2H;

    entry->allocation_id =
        allocation_id;

    entry->stream_id =
        stream_id;

    entry->bytes =
        (size_t)bytes64;

    CUresult r =
        cuMemAllocHost(
            &entry->host_buffer,
            entry->bytes);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    r =
        cuEventCreate(
            &entry->done_event,
            CU_EVENT_DEFAULT);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    r =
        cuMemcpyDtoHAsync(
            entry->host_buffer,
            a->ptr + (size_t)offset64,
            entry->bytes,
            stream);

    if (r != CUDA_SUCCESS) {
        release_transfer(entry);

        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    r =
        cuEventRecord(
            entry->done_event,
            stream);

    if (r != CUDA_SUCCESS) {
        /*
         * D2H may already be writing the pinned buffer.
         */
        (void)cuStreamSynchronize(stream);
        release_transfer(entry);

        return send_response(
            fd,
            OP_D2H_ASYNC_SUBMIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    uint64_t transfer_id =
        entry->id;

    uint64_t wire_id =
        to_be64(
            transfer_id);

    printf(
        "D2H_ASYNC_SUBMIT request=%u "
        "transfer_id=%llu "
        "allocation_id=%llu offset=%llu "
        "stream_id=%llu bytes=%llu "
        "staging=PINNED\n",
        req_id,
        (unsigned long long)transfer_id,
        (unsigned long long)allocation_id,
        (unsigned long long)offset64,
        (unsigned long long)stream_id,
        (unsigned long long)bytes64);

    return send_response(
        fd,
        OP_D2H_ASYNC_SUBMIT,
        req_id,
        ST_OK,
        &wire_id,
        sizeof(wire_id));
}


/*
 * TRANSFER_QUERY
 *
 * response:
 *   u32 READY/PENDING
 */
static int handle_transfer_query(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(
            fd,
            OP_TRANSFER_QUERY,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint64_t transfer_id =
        read_u64(
            payload);

    TransferEntry *entry =
        find_transfer(
            transfer_id);

    if (!entry)
        return send_response(
            fd,
            OP_TRANSFER_QUERY,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    CUresult r =
        cuEventQuery(
            entry->done_event);

    uint32_t state;

    if (r == CUDA_SUCCESS) {
        state = STREAM_STATE_READY;

    } else if (r == CUDA_ERROR_NOT_READY) {
        state = STREAM_STATE_PENDING;

    } else {
        return send_response(
            fd,
            OP_TRANSFER_QUERY,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);
    }

    uint32_t wire_state =
        htonl(
            state);

    printf(
        "TRANSFER_QUERY request=%u "
        "transfer_id=%llu kind=%s state=%s\n",
        req_id,
        (unsigned long long)transfer_id,
        entry->kind == TRANSFER_KIND_H2D
            ? "H2D"
            : "D2H",
        state == STREAM_STATE_READY
            ? "READY"
            : "PENDING");

    return send_response(
        fd,
        OP_TRANSFER_QUERY,
        req_id,
        ST_OK,
        &wire_state,
        sizeof(wire_state));
}


/*
 * TRANSFER_WAIT
 *
 * Consuming operation:
 *
 *   H2D -> wait, return empty payload, free pinned staging.
 *   D2H -> wait, return copied bytes, free pinned staging.
 *
 * The TransferID becomes stale immediately after this call.
 */
static int handle_transfer_wait(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(
            fd,
            OP_TRANSFER_WAIT,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint64_t transfer_id =
        read_u64(
            payload);

    TransferEntry *entry =
        find_transfer(
            transfer_id);

    if (!entry)
        return send_response(
            fd,
            OP_TRANSFER_WAIT,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    CUresult r =
        cuEventSynchronize(
            entry->done_event);

    if (r != CUDA_SUCCESS)
        return send_response(
            fd,
            OP_TRANSFER_WAIT,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);

    uint32_t kind =
        entry->kind;

    size_t bytes =
        entry->bytes;

    printf(
        "TRANSFER_WAIT request=%u "
        "transfer_id=%llu kind=%s "
        "bytes=%zu result=PASS retire=YES\n",
        req_id,
        (unsigned long long)transfer_id,
        kind == TRANSFER_KIND_H2D
            ? "H2D"
            : "D2H",
        bytes);

    int rc;

    if (kind == TRANSFER_KIND_D2H) {
        rc =
            send_response(
                fd,
                OP_TRANSFER_WAIT,
                req_id,
                ST_OK,
                entry->host_buffer,
                (uint32_t)bytes);
    } else {
        rc =
            send_response(
                fd,
                OP_TRANSFER_WAIT,
                req_id,
                ST_OK,
                NULL,
                0);
    }

    release_transfer(
        entry);

    return rc;
}


static int g8c_query_attr(
    CUdevice_attribute attr,
    uint32_t *value_out)
{
    int value = 0;
    CUresult r = cuDeviceGetAttribute(
        &value,
        attr,
        g_device);

    if (r != CUDA_SUCCESS || value < 0)
        return -1;

    *value_out = (uint32_t)value;
    return 0;
}

static int handle_get_device_info(
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t payload_len)
{
    if (!payload || payload_len != sizeof(uint32_t))
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint32_t logical_device = read_u32(payload);

    if (logical_device != 0)
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_NOT_FOUND,
            NULL,
            0);

    char name[256];
    memset(name, 0, sizeof(name));

    CUresult r = cuDeviceGetName(
        name,
        (int)sizeof(name),
        g_device);
    if (r != CUDA_SUCCESS)
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);

    size_t total_global_mem = 0;
    r = cuDeviceTotalMem(
        &total_global_mem,
        g_device);
    if (r != CUDA_SUCCESS)
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);

    size_t free_mem = 0;
    size_t context_total_mem = 0;
    r = cuMemGetInfo(
        &free_mem,
        &context_total_mem);
    if (r != CUDA_SUCCESS)
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_CUDA_ERROR,
            NULL,
            0);

    uint32_t shared_mem_per_block = 0;
    uint32_t regs_per_block = 0;
    uint32_t warp_size = 0;
    uint32_t mem_pitch = 0;
    uint32_t max_threads_per_block = 0;
    uint32_t max_threads_dim[3] = {0, 0, 0};
    uint32_t max_grid_size[3] = {0, 0, 0};
    uint32_t clock_rate = 0;
    uint32_t total_const_mem = 0;
    uint32_t texture_alignment = 0;
    uint32_t multi_processor_count = 0;
    uint32_t kernel_exec_timeout = 0;
    uint32_t integrated = 0;
    uint32_t can_map_host_memory = 0;
    uint32_t compute_mode = 0;
    uint32_t concurrent_kernels = 0;
    uint32_t ecc_enabled = 0;
    uint32_t pci_bus_id = 0;
    uint32_t pci_device_id = 0;
    uint32_t tcc_driver = 0;
    uint32_t memory_clock_rate = 0;
    uint32_t memory_bus_width = 0;
    uint32_t l2_cache_size = 0;
    uint32_t max_threads_per_mp = 0;
    uint32_t async_engine_count = 0;
    uint32_t unified_addressing = 0;
    uint32_t compute_capability_major = 0;
    uint32_t compute_capability_minor = 0;

#define ATTR(dst, attr) \
    do { \
        if (g8c_query_attr((attr), &(dst)) != 0) { \
            fprintf( \
                stderr, \
                "GET_DEVICE_INFO_ATTR_FAIL request=%u attr=%s\n", \
                req_id, \
                #attr); \
            return send_response( \
                fd, \
                OP_GET_DEVICE_INFO, \
                req_id, \
                ST_CUDA_ERROR, \
                NULL, \
                0); \
        } \
    } while (0)

    ATTR(max_threads_per_block,
         CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
    ATTR(max_threads_dim[0],
         CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X);
    ATTR(max_threads_dim[1],
         CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y);
    ATTR(max_threads_dim[2],
         CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z);
    ATTR(max_grid_size[0],
         CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X);
    ATTR(max_grid_size[1],
         CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y);
    ATTR(max_grid_size[2],
         CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z);
    ATTR(shared_mem_per_block,
         CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK);
    ATTR(total_const_mem,
         CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY);
    ATTR(warp_size,
         CU_DEVICE_ATTRIBUTE_WARP_SIZE);
    ATTR(mem_pitch,
         CU_DEVICE_ATTRIBUTE_MAX_PITCH);
    ATTR(regs_per_block,
         CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK);
    ATTR(clock_rate,
         CU_DEVICE_ATTRIBUTE_CLOCK_RATE);
    ATTR(texture_alignment,
         CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT);
    ATTR(multi_processor_count,
         CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
    ATTR(kernel_exec_timeout,
         CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT);
    ATTR(integrated,
         CU_DEVICE_ATTRIBUTE_INTEGRATED);
    ATTR(can_map_host_memory,
         CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY);
    ATTR(compute_mode,
         CU_DEVICE_ATTRIBUTE_COMPUTE_MODE);
    ATTR(concurrent_kernels,
         CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS);
    ATTR(ecc_enabled,
         CU_DEVICE_ATTRIBUTE_ECC_ENABLED);
    ATTR(pci_bus_id,
         CU_DEVICE_ATTRIBUTE_PCI_BUS_ID);
    ATTR(pci_device_id,
         CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID);
    ATTR(tcc_driver,
         CU_DEVICE_ATTRIBUTE_TCC_DRIVER);
    ATTR(memory_clock_rate,
         CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE);
    ATTR(memory_bus_width,
         CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH);
    ATTR(l2_cache_size,
         CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE);
    ATTR(max_threads_per_mp,
         CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR);
    ATTR(async_engine_count,
         CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT);
    ATTR(unified_addressing,
         CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING);
    ATTR(compute_capability_major,
         CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
    ATTR(compute_capability_minor,
         CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);

#undef ATTR

    int major_i = (int)compute_capability_major;
    int minor_i = (int)compute_capability_minor;

    uint32_t device_overlap =
        async_engine_count > 0 ? 1u : 0u;

    unsigned char response[512];
    memset(response, 0, sizeof(response));
    size_t pos = 0;

    g8c_put_u32(
        response,
        &pos,
        1u); /* DTO version */

    g8c_put_u32(
        response,
        &pos,
        logical_device);

    memcpy(
        response + pos,
        name,
        sizeof(name));
    pos += sizeof(name);

    g8c_put_u64(
        response,
        &pos,
        (uint64_t)total_global_mem);
    g8c_put_u64(
        response,
        &pos,
        (uint64_t)free_mem);
    g8c_put_u64(
        response,
        &pos,
        (uint64_t)shared_mem_per_block);

    g8c_put_u32(response, &pos, regs_per_block);
    g8c_put_u32(response, &pos, warp_size);
    g8c_put_u64(response, &pos, (uint64_t)mem_pitch);

    g8c_put_u32(response, &pos, max_threads_per_block);
    for (size_t i = 0; i < 3; ++i)
        g8c_put_u32(response, &pos, max_threads_dim[i]);
    for (size_t i = 0; i < 3; ++i)
        g8c_put_u32(response, &pos, max_grid_size[i]);

    g8c_put_u32(response, &pos, clock_rate);
    g8c_put_u64(response, &pos, (uint64_t)total_const_mem);

    g8c_put_u32(response, &pos, (uint32_t)major_i);
    g8c_put_u32(response, &pos, (uint32_t)minor_i);

    g8c_put_u64(
        response,
        &pos,
        (uint64_t)texture_alignment);

    g8c_put_u32(response, &pos, device_overlap);
    g8c_put_u32(response, &pos, multi_processor_count);
    g8c_put_u32(response, &pos, kernel_exec_timeout);
    g8c_put_u32(response, &pos, integrated);
    g8c_put_u32(response, &pos, can_map_host_memory);
    g8c_put_u32(response, &pos, compute_mode);
    g8c_put_u32(response, &pos, concurrent_kernels);
    g8c_put_u32(response, &pos, ecc_enabled);
    g8c_put_u32(response, &pos, pci_bus_id);
    g8c_put_u32(response, &pos, pci_device_id);
    g8c_put_u32(response, &pos, tcc_driver);
    g8c_put_u32(response, &pos, memory_clock_rate);
    g8c_put_u32(response, &pos, memory_bus_width);
    g8c_put_u32(response, &pos, l2_cache_size);
    g8c_put_u32(response, &pos, max_threads_per_mp);
    g8c_put_u32(response, &pos, async_engine_count);
    g8c_put_u32(response, &pos, unified_addressing);

    printf(
        "GET_DEVICE_INFO request=%u device=%u "
        "name=%s free=%zu total=%zu "
        "cc=%d.%d sm=%u warp=%u result=PASS\n",
        req_id,
        logical_device,
        name,
        free_mem,
        total_global_mem,
        major_i,
        minor_i,
        multi_processor_count,
        warp_size);

    return send_response(
        fd,
        OP_GET_DEVICE_INFO,
        req_id,
        ST_OK,
        response,
        (uint32_t)pos);
}


static int serve_session(int fd)
{
    for (;;) {
        uint32_t h[5];

        if (recv_all(fd, h, sizeof(h)) != 0)
            return 0;  /* disconnect */

        uint32_t magic       = ntohl(h[0]);
        uint32_t version     = ntohl(h[1]);
        uint32_t opcode      = ntohl(h[2]);
        uint32_t req_id      = ntohl(h[3]);
        uint32_t payload_len = ntohl(h[4]);

        if (magic != MAGIC || version != VERSION) {
            fprintf(
                stderr,
                "PROTOCOL_ERROR magic=0x%08x version=%u\n",
                magic,
                version);
            return -1;
        }

        if (payload_len > MAX_PAYLOAD) {
            fprintf(
                stderr,
                "PROTOCOL_ERROR payload_too_large=%u\n",
                payload_len);
            return -1;
        }

        unsigned char *payload = NULL;

        if (payload_len > 0) {
            payload = (unsigned char *)malloc(payload_len);

            if (!payload) {
                send_response(fd, opcode, req_id, ST_INTERNAL, NULL, 0);
                return -1;
            }

            if (recv_all(fd, payload, payload_len) != 0) {
                free(payload);
                return -1;
            }
        }

        int rc = 0;
        int close_after_response = 0;

        switch (opcode) {
        case OP_ALLOC:
            rc = handle_alloc(fd, req_id, payload, payload_len);
            break;

        case OP_H2D:
            rc = handle_h2d(fd, req_id, payload, payload_len);
            break;

        case OP_LAUNCH:
            /* Legacy fixed-kernel launch is intentionally not part of Gate 5B. */
            rc = send_response(fd, OP_LAUNCH, req_id, ST_BAD_REQUEST, NULL, 0);
            break;

        case OP_SYNC:
            rc = handle_sync(fd, req_id, payload, payload_len);
            break;

        case OP_D2H:
            rc = handle_d2h(fd, req_id, payload, payload_len);
            break;

        case OP_FREE:
            rc = handle_free(fd, req_id, payload, payload_len);
            break;

        case OP_CLOSE:
            if (payload_len != 0) {
                rc = send_response(fd, OP_CLOSE, req_id, ST_BAD_REQUEST, NULL, 0);
            } else {
                rc = send_response(fd, OP_CLOSE, req_id, ST_OK, NULL, 0);
                close_after_response = 1;
            }
            break;

        case OP_UPLOAD_MODULE:
            rc = handle_upload_module(fd, req_id, payload, payload_len);
            break;

        case OP_GET_KERNEL:
            rc = handle_get_kernel(fd, req_id, payload, payload_len);
            break;

        case OP_UNLOAD_MODULE:
            rc = handle_unload_module(fd, req_id, payload, payload_len);
            break;

        case OP_LAUNCH_GENERIC:
            rc = handle_launch_generic(fd, req_id, payload, payload_len);
            break;

        case OP_CREATE_STREAM:
            rc = handle_create_stream(fd, req_id, payload, payload_len);
            break;

        case OP_DESTROY_STREAM:
            rc = handle_destroy_stream(fd, req_id, payload, payload_len);
            break;

        case OP_STREAM_QUERY:
            rc = handle_stream_query(fd, req_id, payload, payload_len);
            break;

        case OP_STREAM_SYNC:
            rc = handle_stream_sync(fd, req_id, payload, payload_len);
            break;

        case OP_CREATE_EVENT:
            rc = handle_create_event(fd, req_id, payload, payload_len);
            break;

        case OP_DESTROY_EVENT:
            rc = handle_destroy_event(fd, req_id, payload, payload_len);
            break;

        case OP_EVENT_RECORD:
            rc = handle_event_record(fd, req_id, payload, payload_len);
            break;

        case OP_EVENT_QUERY:
            rc = handle_event_query(fd, req_id, payload, payload_len);
            break;

        case OP_EVENT_SYNC:
            rc = handle_event_sync(fd, req_id, payload, payload_len);
            break;

        case OP_STREAM_WAIT_EVENT:
            rc = handle_stream_wait_event(fd, req_id, payload, payload_len);
            break;

        case OP_H2D_ASYNC_SUBMIT:
            rc = handle_h2d_async_submit(fd, req_id, payload, payload_len);
            break;

        case OP_D2H_ASYNC_SUBMIT:
            rc = handle_d2h_async_submit(fd, req_id, payload, payload_len);
            break;

        case OP_TRANSFER_QUERY:
            rc = handle_transfer_query(fd, req_id, payload, payload_len);
            break;

        case OP_TRANSFER_WAIT:
            rc = handle_transfer_wait(fd, req_id, payload, payload_len);
            break;

        case OP_GET_DEVICE_INFO:
            rc = handle_get_device_info(
                fd,
                req_id,
                payload,
                payload_len);
            break;

        default:
            rc = send_response(fd, opcode, req_id, ST_BAD_REQUEST, NULL, 0);
            break;
        }

        free(payload);

        if (rc != 0)
            return -1;

        if (close_after_response) {
            printf("SESSION_CLOSE\n");
            return 0;
        }
    }
}

/* ============================================================
 * Main
 * ============================================================
 */

int main(void)
{
    CUdevice device = 0;
    char device_name[256];

    CUresult r = cuInit(0);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuInit failed rc=%d\n", (int)r);
        return 1;
    }

    int device_count = 0;
    r = cuDeviceGetCount(&device_count);

    if (r != CUDA_SUCCESS || device_count < 1) {
        fprintf(stderr, "no CoreX device rc=%d count=%d\n", (int)r, device_count);
        return 1;
    }

    r = cuDeviceGet(&device, 0);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuDeviceGet failed rc=%d\n", (int)r);
        return 1;
    }

    g_device = device;

    memset(device_name, 0, sizeof(device_name));
    r = cuDeviceGetName(device_name, sizeof(device_name), device);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuDeviceGetName failed rc=%d\n", (int)r);
        return 1;
    }

    r = cuCtxCreate(&g_ctx, 0, device);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuCtxCreate failed rc=%d\n", (int)r);
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd < 0) {
        perror("socket");
        cuCtxDestroy(g_ctx);
        return 1;
    }

    int yes = 1;
    setsockopt(
        listen_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &yes,
        sizeof(yes));

    struct sockaddr_in addr;
    int listen_port = configured_port();
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            listen_fd,
            (struct sockaddr *)&addr,
            sizeof(addr)) != 0) {

        perror("bind");
        close(listen_fd);
        cuCtxDestroy(g_ctx);
        return 1;
    }

    if (listen(listen_fd, 8) != 0) {
        perror("listen");
        close(listen_fd);
        cuCtxDestroy(g_ctx);
        return 1;
    }

    printf("===== CoreX Remote Runtime Server =====\n");
    printf("GPU=%s\n", device_name);
    printf("startup_modules=0\n");
    printf("startup_kernels=0\n");
    printf("protocol=CRX9 version=%u default_stream=LEGACY\n", VERSION);
    printf("listen=127.0.0.1:%d\n", listen_port);
    printf("SERVER_READY\n");
    fflush(stdout);

    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);

        if (fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            break;
        }

        printf("SESSION_START\n");
        fflush(stdout);

        (void)serve_session(fd);

        cleanup_session();
        close(fd);
        fflush(stdout);
    }

    close(listen_fd);

    cleanup_session();

    if (g_ctx) {
        cuCtxDestroy(g_ctx);
        g_ctx = NULL;
    }

    return 0;
}
