#include <arpa/inet.h>
#include <cuda.h>
#include "corex_backend.h"
#include "corex_api_schema.h"
#include "corex_metadata.h"
#include "corex_protocol.h"

#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
    unsigned int flags;
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

typedef struct {
    Allocation allocations[MAX_ALLOCS];
    ModuleEntry modules[MAX_MODULES];
    KernelEntry kernels[MAX_KERNELS];
    StreamEntry streams[MAX_STREAMS];
    EventEntry events[MAX_EVENTS];
    TransferEntry transfers[MAX_TRANSFERS];

    uint64_t next_alloc_id;
    uint64_t next_module_id;
    uint64_t next_kernel_id;
    uint64_t next_stream_id;
    uint64_t next_event_id;
    uint64_t next_transfer_id;
} ServerSession;

typedef union {
    CUdeviceptr ptr;
    int32_t i32;
    uint64_t u64;
    float f32;
    double force_alignment;
} KernelArgStorage;

static CUcontext g_ctx = NULL;
static CUdevice g_device = 0;
static uint32_t g_backend_version = 0;
static int send_response(int fd, uint32_t opcode, uint32_t req_id,
                         uint32_t status, const void *payload, uint32_t payload_len);

static int handle_hello(int fd, uint32_t req_id, uint32_t payload_len)
{
    if (payload_len != 0)
        return send_response(fd, OP_HELLO, req_id, ST_BAD_REQUEST, NULL, 0);

    CorexHello hello = {
        .server_major = 1,
        .server_minor = 1,
        .server_patch = 0,
        .backend_id = CRX_BACKEND_COREX,
        .backend_version = g_backend_version,
        /* The V3 server exposes logical device zero only. */
        .device_count = 1,
        .device_profile_id = CRX_DEVICE_PROFILE_COREX_GENERIC,
        .capability_count = 6,
        .capabilities = {
            CRX_CAP_DEVICE_INFO,
            CRX_CAP_LINEAR_MEMORY,
            CRX_CAP_COPY_SYNC,
            CRX_CAP_STREAM_EVENT,
            CRX_CAP_COPY_ASYNC,
            CRX_CAP_MODULE_KERNEL,
        },
    };
    unsigned char response[CRX_HELLO_MAX_BYTES];
    uint32_t length = 0;
    if (corex_protocol_encode_hello(response, &hello, &length) != 0)
        return send_response(fd, OP_HELLO, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_HELLO, req_id, ST_OK, response, length);
}

static void server_session_init(ServerSession *session)
{
    memset(session, 0, sizeof(*session));
    session->next_alloc_id = 1;
    session->next_module_id = 1;
    session->next_kernel_id = 1;
    session->next_stream_id = 1;
    session->next_event_id = 1;
    session->next_transfer_id = 1;
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
    unsigned char h[CRX_RESPONSE_HEADER_BYTES];
    corex_protocol_encode_response_header(
        h,
        opcode,
        req_id,
        status,
        payload_len);

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

static Allocation *find_allocation(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (session->allocations[i].used && session->allocations[i].id == id)
            return &session->allocations[i];
    }

    return NULL;
}

static Allocation *new_allocation_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (!session->allocations[i].used) {
            memset(&session->allocations[i], 0, sizeof(session->allocations[i]));
            session->allocations[i].used = 1;
            session->allocations[i].id = session->next_alloc_id++;
            return &session->allocations[i];
        }
    }

    return NULL;
}

static ModuleEntry *find_module(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (session->modules[i].used && session->modules[i].id == id)
            return &session->modules[i];
    }

    return NULL;
}

static ModuleEntry *new_module_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_MODULES; ++i) {
        if (!session->modules[i].used) {
            memset(&session->modules[i], 0, sizeof(session->modules[i]));
            session->modules[i].used = 1;
            session->modules[i].id = session->next_module_id++;
            return &session->modules[i];
        }
    }

    return NULL;
}

static KernelEntry *find_kernel(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (session->kernels[i].used && session->kernels[i].id == id)
            return &session->kernels[i];
    }

    return NULL;
}

static KernelEntry *new_kernel_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!session->kernels[i].used) {
            memset(&session->kernels[i], 0, sizeof(session->kernels[i]));
            session->kernels[i].used = 1;
            session->kernels[i].id = session->next_kernel_id++;
            return &session->kernels[i];
        }
    }

    return NULL;
}

static StreamEntry *find_stream(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (session->streams[i].used && session->streams[i].id == id)
            return &session->streams[i];
    }

    return NULL;
}

static StreamEntry *new_stream_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!session->streams[i].used) {
            memset(&session->streams[i], 0, sizeof(session->streams[i]));
            session->streams[i].used = 1;
            session->streams[i].id = session->next_stream_id++;
            return &session->streams[i];
        }
    }

    return NULL;
}


static EventEntry *find_event(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (session->events[i].used && session->events[i].id == id)
            return &session->events[i];
    }

    return NULL;
}

static TransferEntry *find_transfer(ServerSession *session, uint64_t id)
{
    for (int i = 0; i < MAX_TRANSFERS; ++i) {
        if (session->transfers[i].used &&
            session->transfers[i].id == id)
            return &session->transfers[i];
    }

    return NULL;
}

static TransferEntry *new_transfer_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_TRANSFERS; ++i) {
        if (!session->transfers[i].used) {
            memset(&session->transfers[i], 0, sizeof(session->transfers[i]));
            session->transfers[i].used = 1;
            session->transfers[i].id = session->next_transfer_id++;
            return &session->transfers[i];
        }
    }

    return NULL;
}

static void release_transfer(TransferEntry *entry)
{
    if (!entry)
        return;

    if (entry->done_event)
        corex_backend_event_destroy(entry->done_event);

    if (entry->host_buffer)
        corex_backend_host_free(entry->host_buffer);

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
        corex_backend_context_sync();

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
        corex_backend_stream_sync(
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





static EventEntry *new_event_slot(ServerSession *session)
{
    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!session->events[i].used) {
            memset(&session->events[i], 0, sizeof(session->events[i]));
            session->events[i].used = 1;
            session->events[i].id = session->next_event_id++;
            return &session->events[i];
        }
    }

    return NULL;
}

static void invalidate_module_kernels(
    ServerSession *session,
    uint64_t module_id)
{
    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!session->kernels[i].used || session->kernels[i].module_id != module_id)
            continue;

        printf(
            "invalidate kernel_id=%llu module_id=%llu name=%s\n",
            (unsigned long long)session->kernels[i].id,
            (unsigned long long)module_id,
            session->kernels[i].name);

        memset(&session->kernels[i], 0, sizeof(session->kernels[i]));
    }
}

/* ============================================================
 * Session cleanup
 * ============================================================
 */

static void cleanup_session(ServerSession *session)
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
        if (!session->streams[i].used)
            continue;

        CUresult sr = corex_backend_stream_sync(session->streams[i].stream);

        printf(
            "cleanup stream_id=%llu sync_rc=%d\n",
            (unsigned long long)session->streams[i].id,
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
        if (!session->transfers[i].used)
            continue;

        CUresult tr =
            corex_backend_event_sync(
                session->transfers[i].done_event);

        printf(
            "cleanup transfer_id=%llu "
            "kind=%s sync_rc=%d bytes=%zu\n",
            (unsigned long long)session->transfers[i].id,
            session->transfers[i].kind == TRANSFER_KIND_H2D
                ? "H2D"
                : "D2H",
            (int)tr,
            session->transfers[i].bytes);

        release_transfer(
            &session->transfers[i]);
    }

    for (int i = 0; i < MAX_EVENTS; ++i) {
        if (!session->events[i].used)
            continue;

        CUresult er = corex_backend_event_destroy(session->events[i].event);

        printf(
            "cleanup event_id=%llu destroy_rc=%d\n",
            (unsigned long long)session->events[i].id,
            (int)er);

        memset(&session->events[i], 0, sizeof(session->events[i]));
    }

    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!session->streams[i].used)
            continue;

        CUresult dr = corex_backend_stream_destroy(session->streams[i].stream);

        printf(
            "cleanup stream_id=%llu destroy_rc=%d\n",
            (unsigned long long)session->streams[i].id,
            (int)dr);

        memset(&session->streams[i], 0, sizeof(session->streams[i]));
    }

    if (g_ctx) {
        CUresult r = corex_backend_context_sync();
        printf("cleanup_sync rc=%d\n", (int)r);
    }

    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (!session->allocations[i].used)
            continue;

        printf(
            "cleanup allocation_id=%llu\n",
            (unsigned long long)session->allocations[i].id);

        if (session->allocations[i].ptr)
            corex_backend_mem_free(session->allocations[i].ptr);

        memset(&session->allocations[i], 0, sizeof(session->allocations[i]));
    }

    for (int i = 0; i < MAX_KERNELS; ++i) {
        if (!session->kernels[i].used)
            continue;

        printf(
            "cleanup kernel_id=%llu module_id=%llu name=%s\n",
            (unsigned long long)session->kernels[i].id,
            (unsigned long long)session->kernels[i].module_id,
            session->kernels[i].name);

        memset(&session->kernels[i], 0, sizeof(session->kernels[i]));
    }

    for (int i = 0; i < MAX_MODULES; ++i) {
        if (!session->modules[i].used)
            continue;

        printf(
            "cleanup module_id=%llu name=%s\n",
            (unsigned long long)session->modules[i].id,
            session->modules[i].name);

        if (session->modules[i].module)
            corex_backend_module_unload(session->modules[i].module);

        free(session->modules[i].image);

        memset(&session->modules[i], 0, sizeof(session->modules[i]));
    }

    server_session_init(session);
    printf("SESSION_CLEANUP_EMPTY=YES\n");
}

/* ============================================================
 * Gate 2-style allocation/copy handlers
 * ============================================================
 */

static int handle_alloc(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_ALLOC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t size64 = corex_protocol_read_u64(payload);

    if (size64 == 0 || size64 > (uint64_t)SIZE_MAX)
        return send_response(fd, OP_ALLOC, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *slot = new_allocation_slot(session);

    if (!slot)
        return send_response(fd, OP_ALLOC, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = corex_backend_mem_alloc(&slot->ptr, (size_t)size64);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_ALLOC, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    slot->size = (size_t)size64;

    uint64_t wire_id = corex_protocol_to_be64(slot->id);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* id:u64 + offset:u64 + bytes:u64 + data */
    if (len < 24)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = corex_protocol_read_u64(payload + 0);
    uint64_t offset64      = corex_protocol_read_u64(payload + 8);
    uint64_t bytes64       = corex_protocol_read_u64(payload + 16);

    if (bytes64 > UINT32_MAX ||
        24ULL + bytes64 != (uint64_t)len)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *a = find_allocation(session, allocation_id);

    if (!a)
        return send_response(fd, OP_H2D, req_id, ST_NOT_FOUND, NULL, 0);

    if (offset64 > a->size ||
        bytes64 > a->size - (size_t)offset64)
        return send_response(fd, OP_H2D, req_id, ST_BAD_REQUEST, NULL, 0);

    CUresult r = corex_backend_copy_h2d(
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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* id:u64 + offset:u64 + bytes:u64 */
    if (len != 24)
        return send_response(fd, OP_D2H, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = corex_protocol_read_u64(payload + 0);
    uint64_t offset64      = corex_protocol_read_u64(payload + 8);
    uint64_t bytes64       = corex_protocol_read_u64(payload + 16);

    if (bytes64 > UINT32_MAX)
        return send_response(fd, OP_D2H, req_id, ST_BAD_REQUEST, NULL, 0);

    Allocation *a = find_allocation(session, allocation_id);

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

    CUresult r = corex_backend_copy_d2h(
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

static int handle_d2d(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* dst_id:u64 + dst_offset:u64 + src_id:u64 + src_offset:u64 + bytes:u64 */
    if (len != 40)
        return send_response(fd, OP_D2D, req_id, ST_BAD_REQUEST, NULL, 0);
    uint64_t dst_id = corex_protocol_read_u64(payload + 0);
    uint64_t dst_offset = corex_protocol_read_u64(payload + 8);
    uint64_t src_id = corex_protocol_read_u64(payload + 16);
    uint64_t src_offset = corex_protocol_read_u64(payload + 24);
    uint64_t bytes = corex_protocol_read_u64(payload + 32);
    Allocation *dst = find_allocation(session, dst_id);
    Allocation *src = find_allocation(session, src_id);
    if (!dst || !src)
        return send_response(fd, OP_D2D, req_id, ST_NOT_FOUND, NULL, 0);
    if (dst_offset > dst->size || src_offset > src->size ||
        bytes > dst->size - (size_t)dst_offset ||
        bytes > src->size - (size_t)src_offset)
        return send_response(fd, OP_D2D, req_id, ST_BAD_REQUEST, NULL, 0);
    if (dst == src && dst_offset < src_offset + bytes &&
        src_offset < dst_offset + bytes)
        return send_response(fd, OP_D2D, req_id, ST_BAD_REQUEST, NULL, 0);
    CUresult r = corex_backend_copy_d2d(
        dst->ptr + (size_t)dst_offset,
        src->ptr + (size_t)src_offset,
        (size_t)bytes);
    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_D2D, req_id, ST_CUDA_ERROR, NULL, 0);
    printf("D2D request=%u dst_allocation_id=%llu src_allocation_id=%llu bytes=%llu\n",
           req_id, (unsigned long long)dst_id, (unsigned long long)src_id,
           (unsigned long long)bytes);
    return send_response(fd, OP_D2D, req_id, ST_OK, NULL, 0);
}

static int decode_memset_payload(
    ServerSession *session,
    const unsigned char *payload,
    uint32_t len,
    int async,
    Allocation **allocation_out,
    CUstream *stream_out,
    uint64_t *offset_out,
    unsigned char *value_out,
    uint64_t *bytes_out)
{
    uint32_t expected = async ? 36u : 28u;
    if (!payload || len != expected)
        return -1;
    uint64_t allocation_id = corex_protocol_read_u64(payload + 0);
    uint64_t offset = corex_protocol_read_u64(payload + 8);
    uint32_t value = corex_protocol_read_u32(payload + 16);
    uint64_t bytes = corex_protocol_read_u64(payload + 20);
    Allocation *allocation = find_allocation(session, allocation_id);
    if (!allocation || value > 0xffu || offset > allocation->size ||
        bytes > allocation->size - (size_t)offset)
        return -1;
    CUstream stream = NULL;
    if (async) {
        uint64_t stream_id = corex_protocol_read_u64(payload + 28);
        if (stream_id != 0) {
            StreamEntry *entry = find_stream(session, stream_id);
            if (!entry)
                return -1;
            stream = entry->stream;
        }
    }
    *allocation_out = allocation;
    *stream_out = stream;
    *offset_out = offset;
    *value_out = (unsigned char)value;
    *bytes_out = bytes;
    return 0;
}

static int handle_memset(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    Allocation *allocation = NULL;
    CUstream stream = NULL;
    uint64_t offset = 0, bytes = 0;
    unsigned char value = 0;
    if (decode_memset_payload(session, payload, len, 0, &allocation, &stream,
                              &offset, &value, &bytes) != 0)
        return send_response(fd, OP_MEMSET, req_id, ST_BAD_REQUEST, NULL, 0);
    CUresult result = corex_backend_memset_d8(
        allocation->ptr + (size_t)offset, value, (size_t)bytes);
    if (result != CUDA_SUCCESS)
        return send_response(fd, OP_MEMSET, req_id, ST_CUDA_ERROR, NULL, 0);
    printf("MEMSET request=%u allocation_id=%llu offset=%llu value=%u bytes=%llu\n",
           req_id, (unsigned long long)allocation->id,
           (unsigned long long)offset, (unsigned)value,
           (unsigned long long)bytes);
    return send_response(fd, OP_MEMSET, req_id, ST_OK, NULL, 0);
}

static int handle_memset_async(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    Allocation *allocation = NULL;
    CUstream stream = NULL;
    uint64_t offset = 0, bytes = 0;
    unsigned char value = 0;
    if (decode_memset_payload(session, payload, len, 1, &allocation, &stream,
                              &offset, &value, &bytes) != 0)
        return send_response(fd, OP_MEMSET_ASYNC, req_id, ST_BAD_REQUEST, NULL, 0);
    CUresult result = corex_backend_memset_d8_async(
        allocation->ptr + (size_t)offset, value, (size_t)bytes, stream);
    if (result != CUDA_SUCCESS)
        return send_response(fd, OP_MEMSET_ASYNC, req_id, ST_CUDA_ERROR, NULL, 0);
    printf("MEMSET_ASYNC request=%u allocation_id=%llu offset=%llu value=%u bytes=%llu\n",
           req_id, (unsigned long long)allocation->id,
           (unsigned long long)offset, (unsigned)value,
           (unsigned long long)bytes);
    return send_response(fd, OP_MEMSET_ASYNC, req_id, ST_OK, NULL, 0);
}

static int handle_free(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_FREE, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t allocation_id = corex_protocol_read_u64(payload);
    Allocation *a = find_allocation(session, allocation_id);

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
        corex_backend_mem_free(
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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceSynchronizeRequest request;
    if (corex_generated_decode_cudaDeviceSynchronize_request(payload, len, &request) != 0)
        return send_response(fd, OP_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    CUresult r = corex_backend_context_sync();

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
    ServerSession *session,
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

    uint32_t name_len = corex_protocol_read_u32(payload);

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
    uint64_t image_size64 = corex_protocol_read_u64(payload + size_pos);
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
    CUresult r = corex_backend_module_load(&module, image);

    if (r != CUDA_SUCCESS) {
        free(image);
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    ModuleEntry *slot = new_module_slot(session);

    if (!slot) {
        corex_backend_module_unload(module);
        free(image);
        return send_response(fd, OP_UPLOAD_MODULE, req_id, ST_NO_RESOURCE, NULL, 0);
    }

    slot->module = module;
    slot->image = image;
    slot->image_size = image_size;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->metadata = parsed_metadata;
    slot->metadata_valid = 1;

    uint64_t wire_id = corex_protocol_to_be64(slot->id);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    /* u64 module_id + u32 name_len + name */
    if (len < 12)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t module_id = corex_protocol_read_u64(payload);
    uint32_t name_len = corex_protocol_read_u32(payload + 8);

    if (name_len == 0 || name_len >= 128 ||
        12ULL + name_len != len)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_BAD_REQUEST, NULL, 0);

    char name[128];
    memset(name, 0, sizeof(name));
    memcpy(name, payload + 12, name_len);

    ModuleEntry *m = find_module(session, module_id);

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
    CUresult r = corex_backend_module_function(&function, m->module, name);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_GET_KERNEL, req_id, ST_NOT_FOUND, NULL, 0);

    KernelEntry *slot = new_kernel_slot(session);

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

    uint64_t wire_id = corex_protocol_to_be64(slot->id);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_UNLOAD_MODULE, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t module_id = corex_protocol_read_u64(payload);
    ModuleEntry *m = find_module(session, module_id);

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

    invalidate_module_kernels(session, module_id);

    CUresult r = corex_backend_module_unload(m->module);

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

static int resolve_stream_id(ServerSession *session, uint64_t stream_id, CUstream *stream_out);

/* ============================================================
 * Gate 5B Stream handlers
 * ============================================================
 */

static int handle_create_stream(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 4)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    uint32_t flags = corex_protocol_read_u32(payload);

    if (flags != CU_STREAM_DEFAULT && flags != CU_STREAM_NON_BLOCKING)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    StreamEntry *slot = new_stream_slot(session);

    if (!slot)
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = corex_backend_stream_create(&slot->stream, flags);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_CREATE_STREAM, req_id, ST_CUDA_ERROR, NULL, 0);
    }

    uint64_t wire_id = corex_protocol_to_be64(slot->id);

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

static int handle_create_stream_priority(ServerSession *session, int fd,
                                         uint32_t req_id,
                                         const unsigned char *payload, uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_CREATE_STREAM_PRIORITY, req_id, ST_BAD_REQUEST, NULL, 0);
    uint32_t flags = corex_protocol_read_u32(payload);
    int priority = (int32_t)corex_protocol_read_u32(payload + 4);
    if (flags != CU_STREAM_DEFAULT && flags != CU_STREAM_NON_BLOCKING)
        return send_response(fd, OP_CREATE_STREAM_PRIORITY, req_id, ST_BAD_REQUEST, NULL, 0);
    StreamEntry *slot = new_stream_slot(session);
    if (!slot)
        return send_response(fd, OP_CREATE_STREAM_PRIORITY, req_id, ST_NO_RESOURCE, NULL, 0);
    CUresult r = corex_backend_stream_create_priority(&slot->stream, flags, priority);
    if (r != CUDA_SUCCESS) { memset(slot, 0, sizeof(*slot)); return send_response(fd, OP_CREATE_STREAM_PRIORITY, req_id, ST_CUDA_ERROR, NULL, 0); }
    uint64_t wire_id = corex_protocol_to_be64(slot->id);
    return send_response(fd, OP_CREATE_STREAM_PRIORITY, req_id, ST_OK, &wire_id, 8);
}

static int handle_stream_scalar(ServerSession *session, int fd, uint32_t req_id,
                                const unsigned char *payload, uint32_t len,
                                int priority)
{
    if (len != 8)
        return send_response(fd, priority ? OP_STREAM_GET_PRIORITY : OP_STREAM_GET_FLAGS, req_id, ST_BAD_REQUEST, NULL, 0);
    uint64_t id = corex_protocol_read_u64(payload);
    StreamEntry *entry = find_stream(session, id);
    if (!entry)
        return send_response(fd, priority ? OP_STREAM_GET_PRIORITY : OP_STREAM_GET_FLAGS, req_id, ST_NOT_FOUND, NULL, 0);
    unsigned int flags = 0; int value = 0; CUresult r;
    if (priority) r = corex_backend_stream_get_priority(entry->stream, &value);
    else r = corex_backend_stream_get_flags(entry->stream, &flags);
    if (r != CUDA_SUCCESS)
        return send_response(fd, priority ? OP_STREAM_GET_PRIORITY : OP_STREAM_GET_FLAGS, req_id, ST_CUDA_ERROR, NULL, 0);
    unsigned char response[4]; size_t pos = 0;
    corex_protocol_write_u32(response, &pos, priority ? (uint32_t)value : flags);
    return send_response(fd, priority ? OP_STREAM_GET_PRIORITY : OP_STREAM_GET_FLAGS, req_id, ST_OK, response, 4);
}

static int handle_stream_get_flags(ServerSession *session, int fd, uint32_t req_id,
                                   const unsigned char *payload, uint32_t len)
{ return handle_stream_scalar(session, fd, req_id, payload, len, 0); }
static int handle_stream_get_priority(ServerSession *session, int fd, uint32_t req_id,
                                      const unsigned char *payload, uint32_t len)
{ return handle_stream_scalar(session, fd, req_id, payload, len, 1); }

static int handle_stream_query(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    CorexGeneratedcudaStreamQueryRequest request;
    if (corex_generated_decode_cudaStreamQuery_request(payload, len, &request) != 0)
        return send_response(fd, OP_STREAM_QUERY, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = request.stream;
    CUstream stream = 0;
    if (resolve_stream_id(session, stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_QUERY, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_stream_query(stream);
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

    CorexGeneratedcudaStreamQueryResponse response = {.state = state};
    unsigned char wire_state[4];
    size_t wire_length = 0;
    if (corex_generated_encode_cudaStreamQuery_response(
            wire_state, sizeof(wire_state), &wire_length, &response) != 0)
        return send_response(fd, OP_STREAM_QUERY, req_id, ST_INTERNAL, NULL, 0);

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
        wire_state,
        (uint32_t)wire_length);
}

static int handle_stream_sync(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = corex_protocol_read_u64(payload);
    CUstream stream = 0;
    if (resolve_stream_id(session, stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_stream_sync(stream);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_STREAM_SYNC, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "STREAM_SYNC request=%u stream_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)stream_id);

    return send_response(fd, OP_STREAM_SYNC, req_id, ST_OK, NULL, 0);
}

static int handle_destroy_stream(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_DESTROY_STREAM, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = corex_protocol_read_u64(payload);
    StreamEntry *entry = find_stream(session, stream_id);

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
        corex_backend_stream_destroy(
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
    ServerSession *session,
    uint64_t stream_id,
    CUstream *stream_out)
{
    if (stream_id == 0) {
        *stream_out = 0;
        return 0;
    }

    StreamEntry *entry = find_stream(session, stream_id);

    if (!entry)
        return -1;

    *stream_out = entry->stream;
    return 0;
}

static int handle_create_event(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 4)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint32_t flags = corex_protocol_read_u32(payload);

    if (flags != CU_EVENT_DEFAULT && flags != CU_EVENT_BLOCKING_SYNC &&
        flags != CU_EVENT_DISABLE_TIMING)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    EventEntry *slot = new_event_slot(session);

    if (!slot)
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_NO_RESOURCE, NULL, 0);

    CUresult r = corex_backend_event_create(&slot->event, flags);

    if (r != CUDA_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return send_response(fd, OP_CREATE_EVENT, req_id, ST_CUDA_ERROR, NULL, 0);
    }
    slot->flags = flags;

    uint64_t wire_id = corex_protocol_to_be64(slot->id);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 16)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = corex_protocol_read_u64(payload);
    uint64_t stream_id = corex_protocol_read_u64(payload + 8);

    EventEntry *event_entry = find_event(session, event_id);

    if (!event_entry)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_NOT_FOUND, NULL, 0);

    CUstream stream = 0;

    if (resolve_stream_id(session, stream_id, &stream) != 0)
        return send_response(fd, OP_EVENT_RECORD, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_event_record(event_entry->event, stream);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    CorexGeneratedcudaEventQueryRequest request;
    if (corex_generated_decode_cudaEventQuery_request(payload, len, &request) != 0)
        return send_response(fd, OP_EVENT_QUERY, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = request.event;
    EventEntry *entry = find_event(session, event_id);

    if (!entry)
        return send_response(fd, OP_EVENT_QUERY, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_event_query(entry->event);
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

    CorexGeneratedcudaEventQueryResponse response = {.state = state};
    unsigned char wire_state[4];
    size_t wire_length = 0;
    if (corex_generated_encode_cudaEventQuery_response(
            wire_state, sizeof(wire_state), &wire_length, &response) != 0)
        return send_response(fd, OP_EVENT_QUERY, req_id, ST_INTERNAL, NULL, 0);

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
        wire_state,
        (uint32_t)wire_length);
}

static int handle_event_sync(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = corex_protocol_read_u64(payload);
    EventEntry *entry = find_event(session, event_id);

    if (!entry)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_event_sync(entry->event);

    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_EVENT_SYNC, req_id, ST_CUDA_ERROR, NULL, 0);

    printf(
        "EVENT_SYNC request=%u event_id=%llu result=PASS\n",
        req_id,
        (unsigned long long)event_id);

    return send_response(fd, OP_EVENT_SYNC, req_id, ST_OK, NULL, 0);
}

static int handle_event_elapsed(ServerSession *session, int fd, uint32_t req_id,
                                const unsigned char *payload, uint32_t len)
{
    if (len != 16)
        return send_response(fd, OP_EVENT_ELAPSED_TIME, req_id, ST_BAD_REQUEST, NULL, 0);
    EventEntry *start = find_event(session, corex_protocol_read_u64(payload));
    EventEntry *end = find_event(session, corex_protocol_read_u64(payload + 8));
    if (!start || !end)
        return send_response(fd, OP_EVENT_ELAPSED_TIME, req_id, ST_NOT_FOUND, NULL, 0);
    float milliseconds = 0.0f;
    CUresult r = corex_backend_event_elapsed(&milliseconds, start->event, end->event);
    if (r != CUDA_SUCCESS)
        return send_response(fd, OP_EVENT_ELAPSED_TIME, req_id, ST_CUDA_ERROR, NULL, 0);
    uint32_t bits = 0; memcpy(&bits, &milliseconds, 4);
    unsigned char response[4]; size_t pos = 0;
    corex_protocol_write_u32(response, &pos, bits);
    return send_response(fd, OP_EVENT_ELAPSED_TIME, req_id, ST_OK, response, 4);
}

static int handle_version_query(int fd, uint32_t req_id, uint32_t opcode)
{
    if (opcode == OP_GET_DRIVER_VERSION) {
        int version = 0;
        if (corex_backend_driver_version(&version) != CUDA_SUCCESS || version < 0)
            return send_response(fd, opcode, req_id, ST_CUDA_ERROR, NULL, 0);
        unsigned char response[4]; size_t pos = 0;
        corex_protocol_write_u32(response, &pos, (uint32_t)version);
        return send_response(fd, opcode, req_id, ST_OK, response, 4);
    }
    unsigned char response[4]; size_t pos = 0;
    corex_protocol_write_u32(response, &pos, 11000u);
    return send_response(fd, opcode, req_id, ST_OK, response, 4);
}

static int handle_driver_version(ServerSession *session, int fd, uint32_t req_id,
                                  const unsigned char *payload, uint32_t len)
{ (void)session; (void)payload; if (len != 0) return send_response(fd, OP_GET_DRIVER_VERSION, req_id, ST_BAD_REQUEST, NULL, 0); return handle_version_query(fd, req_id, OP_GET_DRIVER_VERSION); }
static int handle_runtime_version(ServerSession *session, int fd, uint32_t req_id,
                                  const unsigned char *payload, uint32_t len)
{ (void)session; (void)payload; if (len != 0) return send_response(fd, OP_GET_RUNTIME_VERSION, req_id, ST_BAD_REQUEST, NULL, 0); return handle_version_query(fd, req_id, OP_GET_RUNTIME_VERSION); }

static int map_runtime_device_attribute(int value, CUdevice_attribute *attribute)
{
    if (!attribute) return -1;
    switch (value) {
    case 1:  *attribute = CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK; return 0;
    case 8:  *attribute = CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK; return 0;
    case 10: *attribute = CU_DEVICE_ATTRIBUTE_WARP_SIZE; return 0;
    case 13: *attribute = CU_DEVICE_ATTRIBUTE_CLOCK_RATE; return 0;
    case 16: *attribute = CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT; return 0;
    case 36: *attribute = CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE; return 0;
    case 37: *attribute = CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH; return 0;
    case 75: *attribute = CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR; return 0;
    case 76: *attribute = CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR; return 0;
    default: return -1;
    }
}

static int map_runtime_limit(int value, CUlimit *limit)
{
    if (!limit) return -1;
    switch (value) {
    case 0: *limit = CU_LIMIT_STACK_SIZE; return 0;
    case 1: *limit = CU_LIMIT_PRINTF_FIFO_SIZE; return 0;
    case 2: *limit = CU_LIMIT_MALLOC_HEAP_SIZE; return 0;
    case 3: *limit = CU_LIMIT_DEV_RUNTIME_SYNC_DEPTH; return 0;
    case 4: *limit = CU_LIMIT_DEV_RUNTIME_PENDING_LAUNCH_COUNT; return 0;
    case 5: *limit = CU_LIMIT_MAX_L2_FETCH_GRANULARITY; return 0;
    case 6: *limit = CU_LIMIT_PERSISTING_L2_CACHE_SIZE; return 0;
    default: return -1;
    }
}

static int map_runtime_cache_config(int value, CUfunc_cache *config)
{
    if (!config) return -1;
    switch (value) {
    case 0: *config = CU_FUNC_CACHE_PREFER_NONE; return 0;
    case 1: *config = CU_FUNC_CACHE_PREFER_SHARED; return 0;
    case 2: *config = CU_FUNC_CACHE_PREFER_L1; return 0;
    case 3: *config = CU_FUNC_CACHE_PREFER_EQUAL; return 0;
    default: return -1;
    }
}

static int map_corex_cache_config(CUfunc_cache config, int *value)
{
    if (!value) return -1;
    switch (config) {
    case CU_FUNC_CACHE_PREFER_NONE: *value = 0; return 0;
    case CU_FUNC_CACHE_PREFER_SHARED: *value = 1; return 0;
    case CU_FUNC_CACHE_PREFER_L1: *value = 2; return 0;
    case CU_FUNC_CACHE_PREFER_EQUAL: *value = 3; return 0;
    default: return -1;
    }
}

static int map_corex_shared_mem_config(CUsharedconfig config, int *value)
{
    if (!value) return -1;
    switch (config) {
    case CU_SHARED_MEM_CONFIG_DEFAULT_BANK_SIZE: *value = 0; return 0;
    case CU_SHARED_MEM_CONFIG_FOUR_BYTE_BANK_SIZE: *value = 1; return 0;
    case CU_SHARED_MEM_CONFIG_EIGHT_BYTE_BANK_SIZE: *value = 2; return 0;
    default: return -1;
    }
}

static int map_runtime_function_attribute(int value, CUfunction_attribute *attribute)
{
    if (!attribute || (value != 8 && value != 9)) return -1;
    *attribute = (CUfunction_attribute)value;
    return 0;
}

static int handle_device_get_attribute(ServerSession *session, int fd, uint32_t req_id,
                                       const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetAttributeRequest request;
    CorexGeneratedcudaDeviceGetAttributeResponse response = {0};
    CUdevice_attribute attribute;
    int value = 0;
    if (corex_generated_decode_cudaDeviceGetAttribute_request(payload, len, &request) != 0)
        return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_BAD_REQUEST, NULL, 0);
    if (request.device != 0)
        return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_INVALID_DEVICE, NULL, 0);
    if (map_runtime_device_attribute(request.attribute, &attribute) != 0)
        return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_device_attribute(&value, attribute, g_device) != CUDA_SUCCESS)
        return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_CUDA_ERROR, NULL, 0);
    response.value = value;
    unsigned char wire[4];
    size_t wire_length = 0;
    if (corex_generated_encode_cudaDeviceGetAttribute_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_DEVICE_GET_ATTRIBUTE, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_device_get_pci_bus_id(ServerSession *session, int fd,
                                        uint32_t req_id,
                                        const unsigned char *payload,
                                        uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetPCIBusIdRequest request;
    CorexGeneratedcudaDeviceGetPCIBusIdResponse response = {0};
    char pci_bus_id[COREX_GENERATED_CUDADEVICEGETPCIBUSID_RESPONSE_PCI_BUS_ID_MAX_BYTES + 1u] = {0};
    unsigned char wire[4u + COREX_GENERATED_CUDADEVICEGETPCIBUSID_RESPONSE_PCI_BUS_ID_MAX_BYTES];
    size_t wire_length = 0;
    if (corex_generated_decode_cudaDeviceGetPCIBusId_request(
            payload, len, &request) != 0)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_BAD_REQUEST, NULL, 0);
    if (request.device != 0)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_INVALID_DEVICE, NULL, 0);
    CUresult result = corex_backend_device_pci_bus_id(
        pci_bus_id, (int)sizeof(pci_bus_id), g_device);
    if (result == CUDA_ERROR_INVALID_DEVICE)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_INVALID_DEVICE, NULL, 0);
    if (result == CUDA_ERROR_INVALID_VALUE)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_BAD_REQUEST, NULL, 0);
    if (result != CUDA_SUCCESS)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_CUDA_ERROR, NULL, 0);
    size_t length = strnlen(pci_bus_id, sizeof(pci_bus_id));
    if (length == sizeof(pci_bus_id) ||
        length > COREX_GENERATED_CUDADEVICEGETPCIBUSID_RESPONSE_PCI_BUS_ID_MAX_BYTES)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_INTERNAL, NULL, 0);
    response.pci_bus_id_length = (uint32_t)length;
    memcpy(response.pci_bus_id, pci_bus_id, length + 1u);
    if (corex_generated_encode_cudaDeviceGetPCIBusId_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id,
                             ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_DEVICE_GET_PCI_BUS_ID, req_id, ST_OK,
                         wire, (uint32_t)wire_length);
}

static int handle_device_get_by_pci_bus_id(ServerSession *session, int fd,
                                           uint32_t req_id,
                                           const unsigned char *payload,
                                           uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetByPCIBusIdRequest request;
    CorexGeneratedcudaDeviceGetByPCIBusIdResponse response = {0};
    if (corex_generated_decode_cudaDeviceGetByPCIBusId_request(
            payload, len, &request) != 0)
        return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id,
                             ST_BAD_REQUEST, NULL, 0);
    CUdevice device = -1;
    CUresult result = corex_backend_device_get_by_pci_bus_id(
        &device, request.pci_bus_id);
    if (result == CUDA_ERROR_INVALID_DEVICE)
        return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id,
                             ST_INVALID_DEVICE, NULL, 0);
    if (result == CUDA_ERROR_INVALID_VALUE)
        return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id,
                             ST_BAD_REQUEST, NULL, 0);
    if (result != CUDA_SUCCESS)
        return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id,
                             ST_CUDA_ERROR, NULL, 0);
    response.device = device;
    unsigned char wire[4];
    size_t wire_length = 0;
    if (corex_generated_encode_cudaDeviceGetByPCIBusId_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id,
                             ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_DEVICE_GET_BY_PCI_BUS_ID, req_id, ST_OK,
                         wire, (uint32_t)wire_length);
}

static int handle_get_device_flags(ServerSession *session, int fd, uint32_t req_id,
                                    const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaGetDeviceFlagsRequest request;
    CorexGeneratedcudaGetDeviceFlagsResponse response = {0};
    unsigned char wire[4];
    size_t wire_length = 0;
    if (corex_generated_decode_cudaGetDeviceFlags_request(payload, len, &request) != 0)
        return send_response(fd, OP_GET_DEVICE_FLAGS, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_context_get_flags(&response.flags) != CUDA_SUCCESS)
        return send_response(fd, OP_GET_DEVICE_FLAGS, req_id, ST_CUDA_ERROR, NULL, 0);
    if (corex_generated_encode_cudaGetDeviceFlags_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_GET_DEVICE_FLAGS, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_GET_DEVICE_FLAGS, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_get_priority_range(ServerSession *session, int fd, uint32_t req_id,
                                     const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetStreamPriorityRangeRequest request;
    CorexGeneratedcudaDeviceGetStreamPriorityRangeResponse response = {0};
    unsigned char wire[8];
    size_t wire_length = 0;
    if (corex_generated_decode_cudaDeviceGetStreamPriorityRange_request(
            payload, len, &request) != 0)
        return send_response(fd, OP_GET_PRIORITY_RANGE, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_context_get_stream_priority_range(&response.least_priority,
                                                        &response.greatest_priority) != CUDA_SUCCESS)
        return send_response(fd, OP_GET_PRIORITY_RANGE, req_id, ST_CUDA_ERROR, NULL, 0);
    if (corex_generated_encode_cudaDeviceGetStreamPriorityRange_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_GET_PRIORITY_RANGE, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_GET_PRIORITY_RANGE, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_get_limit(ServerSession *session, int fd, uint32_t req_id,
                            const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetLimitRequest request;
    CorexGeneratedcudaDeviceGetLimitResponse response = {0};
    CUlimit limit;
    size_t wire_length = 0;
    if (corex_generated_decode_cudaDeviceGetLimit_request(payload, len, &request) != 0)
        return send_response(fd, OP_GET_LIMIT, req_id, ST_BAD_REQUEST, NULL, 0);
    if (map_runtime_limit(request.limit, &limit) != 0)
        return send_response(fd, OP_GET_LIMIT, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_context_get_limit(&response.value, limit) != CUDA_SUCCESS)
        return send_response(fd, OP_GET_LIMIT, req_id, ST_CUDA_ERROR, NULL, 0);
    unsigned char wire[8];
    if (corex_generated_encode_cudaDeviceGetLimit_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_GET_LIMIT, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_GET_LIMIT, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_get_cache_config(ServerSession *session, int fd, uint32_t req_id,
                                   const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetCacheConfigRequest request;
    CorexGeneratedcudaDeviceGetCacheConfigResponse response = {0};
    CUfunc_cache config;
    int runtime_config = 0;
    unsigned char wire[4];
    size_t wire_length = 0;
    if (corex_generated_decode_cudaDeviceGetCacheConfig_request(payload, len, &request) != 0)
        return send_response(fd, OP_GET_CACHE_CONFIG, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_context_get_cache_config(&config) != CUDA_SUCCESS)
        return send_response(fd, OP_GET_CACHE_CONFIG, req_id, ST_CUDA_ERROR, NULL, 0);
    if (map_corex_cache_config(config, &runtime_config) != 0)
        return send_response(fd, OP_GET_CACHE_CONFIG, req_id, ST_CUDA_ERROR, NULL, 0);
    response.config = runtime_config;
    if (corex_generated_encode_cudaDeviceGetCacheConfig_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_GET_CACHE_CONFIG, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_GET_CACHE_CONFIG, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_get_shared_mem_config(ServerSession *session, int fd, uint32_t req_id,
                                        const unsigned char *payload, uint32_t len)
{
    (void)session;
    CorexGeneratedcudaDeviceGetSharedMemConfigRequest request;
    CorexGeneratedcudaDeviceGetSharedMemConfigResponse response = {0};
    CUsharedconfig config;
    int runtime_config = 0;
    unsigned char wire[4];
    size_t wire_length = 0;
    if (corex_generated_decode_cudaDeviceGetSharedMemConfig_request(payload, len, &request) != 0)
        return send_response(fd, OP_GET_SHARED_MEM_CONFIG, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_context_get_shared_mem_config(&config) != CUDA_SUCCESS)
        return send_response(fd, OP_GET_SHARED_MEM_CONFIG, req_id, ST_CUDA_ERROR, NULL, 0);
    if (map_corex_shared_mem_config(config, &runtime_config) != 0)
        return send_response(fd, OP_GET_SHARED_MEM_CONFIG, req_id, ST_CUDA_ERROR, NULL, 0);
    response.config = runtime_config;
    if (corex_generated_encode_cudaDeviceGetSharedMemConfig_response(
            wire, sizeof(wire), &wire_length, &response) != 0)
        return send_response(fd, OP_GET_SHARED_MEM_CONFIG, req_id, ST_INTERNAL, NULL, 0);
    return send_response(fd, OP_GET_SHARED_MEM_CONFIG, req_id, ST_OK, wire, (uint32_t)wire_length);
}

static int handle_function_set_attribute(ServerSession *session, int fd, uint32_t req_id,
                                          const unsigned char *payload, uint32_t len)
{
    CorexGeneratedcudaFuncSetAttributeRequest request;
    CUfunction_attribute attribute;
    KernelEntry *kernel;
    if (corex_generated_decode_cudaFuncSetAttribute_request(payload, len, &request) != 0)
        return send_response(fd, OP_FUNCTION_SET_ATTRIBUTE, req_id, ST_BAD_REQUEST, NULL, 0);
    kernel = find_kernel(session, request.kernel);
    if (!kernel)
        return send_response(fd, OP_FUNCTION_SET_ATTRIBUTE, req_id, ST_NOT_FOUND, NULL, 0);
    if (map_runtime_function_attribute(request.attribute, &attribute) != 0)
        return send_response(fd, OP_FUNCTION_SET_ATTRIBUTE, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_function_set_attribute(kernel->function, attribute, request.value) != CUDA_SUCCESS)
        return send_response(fd, OP_FUNCTION_SET_ATTRIBUTE, req_id, ST_CUDA_ERROR, NULL, 0);
    return send_response(fd, OP_FUNCTION_SET_ATTRIBUTE, req_id, ST_OK, NULL, 0);
}

static int handle_function_set_cache_config(ServerSession *session, int fd, uint32_t req_id,
                                             const unsigned char *payload, uint32_t len)
{
    CorexGeneratedcudaFuncSetCacheConfigRequest request;
    CUfunc_cache config;
    KernelEntry *kernel;
    if (corex_generated_decode_cudaFuncSetCacheConfig_request(payload, len, &request) != 0)
        return send_response(fd, OP_FUNCTION_SET_CACHE_CONFIG, req_id, ST_BAD_REQUEST, NULL, 0);
    kernel = find_kernel(session, request.kernel);
    if (!kernel)
        return send_response(fd, OP_FUNCTION_SET_CACHE_CONFIG, req_id, ST_NOT_FOUND, NULL, 0);
    if (map_runtime_cache_config(request.config, &config) != 0)
        return send_response(fd, OP_FUNCTION_SET_CACHE_CONFIG, req_id, ST_BAD_REQUEST, NULL, 0);
    if (corex_backend_function_set_cache_config(kernel->function, config) != CUDA_SUCCESS)
        return send_response(fd, OP_FUNCTION_SET_CACHE_CONFIG, req_id, ST_CUDA_ERROR, NULL, 0);
    return send_response(fd, OP_FUNCTION_SET_CACHE_CONFIG, req_id, ST_OK, NULL, 0);
}

static int handle_function_attributes(ServerSession *session, int fd, uint32_t req_id,
                                      const unsigned char *payload, uint32_t len)
{
    if (len != 8) return send_response(fd, OP_FUNCTION_ATTRIBUTES, req_id, ST_BAD_REQUEST, NULL, 0);
    KernelEntry *kernel = find_kernel(session, corex_protocol_read_u64(payload));
    if (!kernel) return send_response(fd, OP_FUNCTION_ATTRIBUTES, req_id, ST_NOT_FOUND, NULL, 0);
    int values[7];
    CUfunction_attribute attrs[] = {CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES,
        CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES, CU_FUNC_ATTRIBUTE_NUM_REGS, CU_FUNC_ATTRIBUTE_PTX_VERSION,
        CU_FUNC_ATTRIBUTE_BINARY_VERSION, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK};
    for (size_t i = 0; i < 7; ++i)
        if (corex_backend_function_attribute(&values[i], kernel->function, attrs[i]) != CUDA_SUCCESS)
            return send_response(fd, OP_FUNCTION_ATTRIBUTES, req_id, ST_CUDA_ERROR, NULL, 0);
    unsigned char response[28]; size_t pos = 0;
    for (size_t i = 0; i < 7; ++i) corex_protocol_write_u32(response, &pos, (uint32_t)values[i]);
    return send_response(fd, OP_FUNCTION_ATTRIBUTES, req_id, ST_OK, response, 28);
}

static int handle_occupancy(ServerSession *session, int fd, uint32_t req_id,
                            const unsigned char *payload, uint32_t len)
{
    if (len != 20) return send_response(fd, OP_OCCUPANCY, req_id, ST_BAD_REQUEST, NULL, 0);
    KernelEntry *kernel = find_kernel(session, corex_protocol_read_u64(payload));
    int block_size = (int)corex_protocol_read_u32(payload + 8);
    uint64_t dynamic_shared = corex_protocol_read_u64(payload + 12);
    if (!kernel || block_size <= 0 || block_size > 2048 || dynamic_shared > SIZE_MAX)
        return send_response(fd, OP_OCCUPANCY, req_id, ST_BAD_REQUEST, NULL, 0);
    int blocks = 0;
    if (corex_backend_occupancy(&blocks, kernel->function, block_size, (size_t)dynamic_shared) != CUDA_SUCCESS)
        return send_response(fd, OP_OCCUPANCY, req_id, ST_CUDA_ERROR, NULL, 0);
    unsigned char response[4]; size_t pos = 0; corex_protocol_write_u32(response, &pos, (uint32_t)blocks);
    return send_response(fd, OP_OCCUPANCY, req_id, ST_OK, response, 4);
}

static int handle_stream_wait_event(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 20)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t stream_id = corex_protocol_read_u64(payload);
    uint64_t event_id = corex_protocol_read_u64(payload + 8);
    uint32_t flags = corex_protocol_read_u32(payload + 16);

    if (flags != 0)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    CUstream stream = 0;

    if (resolve_stream_id(session, stream_id, &stream) != 0)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    EventEntry *event_entry = find_event(session, event_id);

    if (!event_entry)
        return send_response(fd, OP_STREAM_WAIT_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    CUresult r = corex_backend_stream_wait_event(stream, event_entry->event, flags);

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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len != 8)
        return send_response(fd, OP_DESTROY_EVENT, req_id, ST_BAD_REQUEST, NULL, 0);

    uint64_t event_id = corex_protocol_read_u64(payload);
    EventEntry *entry = find_event(session, event_id);

    if (!entry)
        return send_response(fd, OP_DESTROY_EVENT, req_id, ST_NOT_FOUND, NULL, 0);

    /*
     * Gate 5E:
     *
     * Event lifetime is stronger than merely waiting for the
     * producer event to become complete. Another stream may
     * already contain corex_backend_stream_wait_event(event) followed by
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
        corex_backend_event_destroy(
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
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t len)
{
    if (len < 48)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_BAD_REQUEST, NULL, 0);

    size_t pos = 0;

    uint64_t kernel_id = corex_protocol_read_u64(payload + pos); pos += 8;
    uint64_t stream_id = corex_protocol_read_u64(payload + pos); pos += 8;

    uint32_t grid_x = corex_protocol_read_u32(payload + pos); pos += 4;
    uint32_t grid_y = corex_protocol_read_u32(payload + pos); pos += 4;
    uint32_t grid_z = corex_protocol_read_u32(payload + pos); pos += 4;

    uint32_t block_x = corex_protocol_read_u32(payload + pos); pos += 4;
    uint32_t block_y = corex_protocol_read_u32(payload + pos); pos += 4;
    uint32_t block_z = corex_protocol_read_u32(payload + pos); pos += 4;

    uint32_t shared_mem = corex_protocol_read_u32(payload + pos); pos += 4;
    uint32_t argc       = corex_protocol_read_u32(payload + pos); pos += 4;

    if (argc > MAX_ARGS)
        return send_response(fd, OP_LAUNCH_GENERIC, req_id, ST_BAD_REQUEST, NULL, 0);

    KernelEntry *k = find_kernel(session, kernel_id);
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
        StreamEntry *stream_entry = find_stream(session, stream_id);
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

        uint32_t kind = corex_protocol_read_u32(payload + pos); pos += 4;
        uint32_t arg_size = corex_protocol_read_u32(payload + pos); pos += 4;

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
            uint64_t allocation_id = corex_protocol_read_u64(payload + pos);
            uint64_t offset64      = corex_protocol_read_u64(payload + pos + 8);
            Allocation *a = find_allocation(session, allocation_id);

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
            uint32_t bits = corex_protocol_read_u32(payload + pos);
            storage[i].i32 = (int32_t)bits;
            kernel_params[i] = &storage[i].i32;
            printf("  arg[%u] I32 value=%d\n", i, storage[i].i32);
            break;
        }

        case ARG_U64:
            storage[i].u64 = corex_protocol_read_u64(payload + pos);
            kernel_params[i] = &storage[i].u64;
            printf("  arg[%u] U64 value=%llu\n",
                   i,
                   (unsigned long long)storage[i].u64);
            break;

        case ARG_F32: {
            uint32_t bits = corex_protocol_read_u32(payload + pos);
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

    CUresult r = corex_backend_launch(
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
    ServerSession *session,
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

    uint64_t allocation_id = corex_protocol_read_u64(payload + 0);
    uint64_t offset64      = corex_protocol_read_u64(payload + 8);
    uint64_t stream_id     = corex_protocol_read_u64(payload + 16);
    uint64_t bytes64       = corex_protocol_read_u64(payload + 24);

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
            session,
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
            session,
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
        new_transfer_slot(session);

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
        corex_backend_host_alloc(
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
        corex_backend_event_create(
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
        corex_backend_copy_h2d_async(
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
        corex_backend_event_record(
            entry->done_event,
            stream);

    if (r != CUDA_SUCCESS) {
        /*
         * H2D may already be in flight and still reference the
         * pinned staging buffer. Synchronize before freeing it.
         */
        (void)corex_backend_stream_sync(stream);
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
        corex_protocol_to_be64(
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
    ServerSession *session,
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

    uint64_t allocation_id = corex_protocol_read_u64(payload + 0);
    uint64_t offset64      = corex_protocol_read_u64(payload + 8);
    uint64_t stream_id     = corex_protocol_read_u64(payload + 16);
    uint64_t bytes64       = corex_protocol_read_u64(payload + 24);

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
            session,
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
            session,
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
        new_transfer_slot(session);

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
        corex_backend_host_alloc(
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
        corex_backend_event_create(
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
        corex_backend_copy_d2h_async(
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
        corex_backend_event_record(
            entry->done_event,
            stream);

    if (r != CUDA_SUCCESS) {
        /*
         * D2H may already be writing the pinned buffer.
         */
        (void)corex_backend_stream_sync(stream);
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
        corex_protocol_to_be64(
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
    ServerSession *session,
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
        corex_protocol_read_u64(
            payload);

    TransferEntry *entry =
        find_transfer(
            session,
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
        corex_backend_event_query(
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
    ServerSession *session,
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
        corex_protocol_read_u64(
            payload);

    TransferEntry *entry =
        find_transfer(
            session,
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
        corex_backend_event_sync(
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
    CUresult r = corex_backend_device_attribute(
        &value,
        attr,
        g_device);

    if (r != CUDA_SUCCESS || value < 0)
        return -1;

    *value_out = (uint32_t)value;
    return 0;
}

static int handle_get_device_info(
    ServerSession *session,
    int fd,
    uint32_t req_id,
    const unsigned char *payload,
    uint32_t payload_len)
{
    (void)session;
    if (!payload || payload_len != sizeof(uint32_t))
        return send_response(
            fd,
            OP_GET_DEVICE_INFO,
            req_id,
            ST_BAD_REQUEST,
            NULL,
            0);

    uint32_t logical_device = corex_protocol_read_u32(payload);

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

    CUresult r = corex_backend_device_name(
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
    r = corex_backend_device_total_mem(
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
    r = corex_backend_mem_info(
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

    corex_protocol_write_u32(
        response,
        &pos,
        1u); /* DTO version */

    corex_protocol_write_u32(
        response,
        &pos,
        logical_device);

    memcpy(
        response + pos,
        name,
        sizeof(name));
    pos += sizeof(name);

    corex_protocol_write_u64(
        response,
        &pos,
        (uint64_t)total_global_mem);
    corex_protocol_write_u64(
        response,
        &pos,
        (uint64_t)free_mem);
    corex_protocol_write_u64(
        response,
        &pos,
        (uint64_t)shared_mem_per_block);

    corex_protocol_write_u32(response, &pos, regs_per_block);
    corex_protocol_write_u32(response, &pos, warp_size);
    corex_protocol_write_u64(response, &pos, (uint64_t)mem_pitch);

    corex_protocol_write_u32(response, &pos, max_threads_per_block);
    for (size_t i = 0; i < 3; ++i)
        corex_protocol_write_u32(response, &pos, max_threads_dim[i]);
    for (size_t i = 0; i < 3; ++i)
        corex_protocol_write_u32(response, &pos, max_grid_size[i]);

    corex_protocol_write_u32(response, &pos, clock_rate);
    corex_protocol_write_u64(response, &pos, (uint64_t)total_const_mem);

    corex_protocol_write_u32(response, &pos, (uint32_t)major_i);
    corex_protocol_write_u32(response, &pos, (uint32_t)minor_i);

    corex_protocol_write_u64(
        response,
        &pos,
        (uint64_t)texture_alignment);

    corex_protocol_write_u32(response, &pos, device_overlap);
    corex_protocol_write_u32(response, &pos, multi_processor_count);
    corex_protocol_write_u32(response, &pos, kernel_exec_timeout);
    corex_protocol_write_u32(response, &pos, integrated);
    corex_protocol_write_u32(response, &pos, can_map_host_memory);
    corex_protocol_write_u32(response, &pos, compute_mode);
    corex_protocol_write_u32(response, &pos, concurrent_kernels);
    corex_protocol_write_u32(response, &pos, ecc_enabled);
    corex_protocol_write_u32(response, &pos, pci_bus_id);
    corex_protocol_write_u32(response, &pos, pci_device_id);
    corex_protocol_write_u32(response, &pos, tcc_driver);
    corex_protocol_write_u32(response, &pos, memory_clock_rate);
    corex_protocol_write_u32(response, &pos, memory_bus_width);
    corex_protocol_write_u32(response, &pos, l2_cache_size);
    corex_protocol_write_u32(response, &pos, max_threads_per_mp);
    corex_protocol_write_u32(response, &pos, async_engine_count);
    corex_protocol_write_u32(response, &pos, unified_addressing);

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

typedef int (*ServerHandler)(ServerSession *, int, uint32_t,
                             const unsigned char *, uint32_t);

typedef struct {
    ServerHandler handler;
    uint32_t capability_id;
} ServerHandlerEntry;

static int handle_legacy_launch(ServerSession *session, int fd,
                                uint32_t req_id, const unsigned char *payload,
                                uint32_t length)
{
    (void)session;
    (void)payload;
    (void)length;
    return send_response(fd, OP_LAUNCH, req_id, ST_BAD_REQUEST, NULL, 0);
}

static int handle_close(ServerSession *session, int fd, uint32_t req_id,
                        const unsigned char *payload, uint32_t length)
{
    (void)session;
    (void)payload;
    return send_response(fd, OP_CLOSE, req_id,
                         length == 0 ? ST_OK : ST_BAD_REQUEST, NULL, 0);
}

static int handle_hello_entry(ServerSession *session, int fd, uint32_t req_id,
                              const unsigned char *payload, uint32_t length)
{
    (void)session;
    (void)payload;
    return handle_hello(fd, req_id, length);
}

/* One source of truth for opcode, handler, and optional semantic capability. */
#ifdef COREX_TEST_DUPLICATE_OPCODE
#define SERVER_REGISTRY_DUPLICATE_TEST(X) X(OP_ALLOC, handle_alloc, CRX_CAP_LINEAR_MEMORY)
#else
#define SERVER_REGISTRY_DUPLICATE_TEST(X)
#endif
#define SERVER_HANDLER_REGISTRY(X) \
    X(OP_ALLOC, handle_alloc, CRX_CAP_LINEAR_MEMORY) \
    X(OP_H2D, handle_h2d, CRX_CAP_COPY_SYNC) \
    X(OP_LAUNCH, handle_legacy_launch, 0) \
    X(OP_D2H, handle_d2h, CRX_CAP_COPY_SYNC) \
    X(OP_FREE, handle_free, CRX_CAP_LINEAR_MEMORY) \
    X(OP_CLOSE, handle_close, 0) \
    X(OP_UPLOAD_MODULE, handle_upload_module, CRX_CAP_MODULE_KERNEL) \
    X(OP_GET_KERNEL, handle_get_kernel, CRX_CAP_MODULE_KERNEL) \
    X(OP_UNLOAD_MODULE, handle_unload_module, CRX_CAP_MODULE_KERNEL) \
    X(OP_LAUNCH_GENERIC, handle_launch_generic, CRX_CAP_MODULE_KERNEL) \
    X(OP_CREATE_STREAM, handle_create_stream, CRX_CAP_STREAM_EVENT) \
    X(OP_DESTROY_STREAM, handle_destroy_stream, CRX_CAP_STREAM_EVENT) \
    X(OP_STREAM_SYNC, handle_stream_sync, CRX_CAP_STREAM_EVENT) \
    X(OP_CREATE_EVENT, handle_create_event, CRX_CAP_STREAM_EVENT) \
    X(OP_DESTROY_EVENT, handle_destroy_event, CRX_CAP_STREAM_EVENT) \
    X(OP_EVENT_RECORD, handle_event_record, CRX_CAP_STREAM_EVENT) \
    X(OP_EVENT_SYNC, handle_event_sync, CRX_CAP_STREAM_EVENT) \
    X(OP_STREAM_WAIT_EVENT, handle_stream_wait_event, CRX_CAP_STREAM_EVENT) \
    X(OP_H2D_ASYNC_SUBMIT, handle_h2d_async_submit, CRX_CAP_COPY_ASYNC) \
    X(OP_D2H_ASYNC_SUBMIT, handle_d2h_async_submit, CRX_CAP_COPY_ASYNC) \
    X(OP_TRANSFER_QUERY, handle_transfer_query, CRX_CAP_COPY_ASYNC) \
    X(OP_TRANSFER_WAIT, handle_transfer_wait, CRX_CAP_COPY_ASYNC) \
    X(OP_GET_DEVICE_INFO, handle_get_device_info, CRX_CAP_DEVICE_INFO) \
    X(OP_HELLO, handle_hello_entry, 0) \
    X(OP_D2D, handle_d2d, CRX_CAP_COPY_SYNC) \
    X(OP_MEMSET, handle_memset, CRX_CAP_LINEAR_MEMORY) \
    X(OP_MEMSET_ASYNC, handle_memset_async, CRX_CAP_COPY_ASYNC) \
    X(OP_STREAM_GET_FLAGS, handle_stream_get_flags, CRX_CAP_STREAM_EVENT) \
    X(OP_CREATE_STREAM_PRIORITY, handle_create_stream_priority, CRX_CAP_STREAM_EVENT) \
    X(OP_STREAM_GET_PRIORITY, handle_stream_get_priority, CRX_CAP_STREAM_EVENT) \
    X(OP_EVENT_ELAPSED_TIME, handle_event_elapsed, CRX_CAP_STREAM_EVENT) \
    X(OP_GET_DRIVER_VERSION, handle_driver_version, CRX_CAP_DEVICE_INFO) \
    X(OP_GET_RUNTIME_VERSION, handle_runtime_version, CRX_CAP_DEVICE_INFO) \
    X(OP_FUNCTION_ATTRIBUTES, handle_function_attributes, CRX_CAP_MODULE_KERNEL) \
    X(OP_OCCUPANCY, handle_occupancy, CRX_CAP_MODULE_KERNEL) \
    COREX_GENERATED_SERVER_HANDLER_REGISTRY(X) \
    SERVER_REGISTRY_DUPLICATE_TEST(X)

#define REGISTRY_ENTRY(opcode, function, capability) \
    [opcode] = {function, capability},
static const ServerHandlerEntry g_handlers[OP__COUNT] = {
    SERVER_HANDLER_REGISTRY(REGISTRY_ENTRY)
};
#undef REGISTRY_ENTRY

static int server_registry_validate(void)
{
    /* Duplicate numeric opcodes produce a C duplicate-case compilation error. */
#define UNIQUE_OPCODE_CASE(opcode, function, capability) case opcode: break;
    switch (0) {
        SERVER_HANDLER_REGISTRY(UNIQUE_OPCODE_CASE)
    default: break;
    }
#undef UNIQUE_OPCODE_CASE
    for (uint32_t opcode = OP_ALLOC; opcode < OP__COUNT; ++opcode) {
        if (!g_handlers[opcode].handler ||
            g_handlers[opcode].capability_id > CRX_CAP_MODULE_KERNEL)
            return -1;
    }
    for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i) {
        uint32_t opcode = corex_generated_apis[i].opcode;
        if (opcode >= OP__COUNT ||
            g_handlers[opcode].capability_id != corex_generated_apis[i].capability_id)
            return -1;
    }
    return 0;
}


static int serve_session(ServerSession *session, int fd)
{
    for (;;) {
        unsigned char h[CRX_REQUEST_HEADER_BYTES];

        if (recv_all(fd, h, sizeof(h)) != 0)
            return 0;  /* disconnect */

        CorexProtocolRequestHeader request_header;
        if (corex_protocol_decode_request_header(
                h,
                sizeof(h),
                &request_header) != 0) {
            fprintf(
                stderr,
                "PROTOCOL_ERROR magic=0x%08x version=%u\n",
                corex_protocol_read_u32(h),
                corex_protocol_read_u32(h + sizeof(uint32_t)));
            return -1;
        }

        uint32_t opcode = request_header.opcode;
        uint32_t req_id = request_header.request_id;
        uint32_t payload_len = request_header.payload_length;

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

        int generated_schema_error = 0;
        for (size_t i = 0; i < COREX_GENERATED_API_COUNT; ++i) {
            if (corex_generated_apis[i].opcode == opcode) {
                generated_schema_error =
                    corex_generated_validate_payload(opcode, payload_len) != 0;
                break;
            }
        }
        const ServerHandlerEntry *entry =
            opcode < sizeof(g_handlers) / sizeof(g_handlers[0]) &&
            g_handlers[opcode].handler ? &g_handlers[opcode] : NULL;
        int rc = generated_schema_error
            ? send_response(fd, opcode, req_id, ST_BAD_REQUEST, NULL, 0)
            : entry
            ? entry->handler(session, fd, req_id, payload, payload_len)
            : send_response(fd, opcode, req_id, ST_BAD_REQUEST, NULL, 0);
        int close_after_response = opcode == OP_CLOSE && payload_len == 0;

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
    if (server_registry_validate() != 0) {
        fprintf(stderr, "SERVER_HANDLER_REGISTRY=INVALID\n");
        return 1;
    }
    CUdevice device = 0;
    char device_name[256];

    CUresult r = corex_backend_init();

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuInit failed rc=%d\n", (int)r);
        return 1;
    }

    int device_count = 0;
    r = corex_backend_device_count(&device_count);

    if (r != CUDA_SUCCESS || device_count < 1) {
        fprintf(stderr, "no CoreX device rc=%d count=%d\n", (int)r, device_count);
        return 1;
    }

    r = corex_backend_device_get(&device, 0);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuDeviceGet failed rc=%d\n", (int)r);
        return 1;
    }

    g_device = device;
    int driver_version = 0;
    if (corex_backend_driver_version(&driver_version) == CUDA_SUCCESS && driver_version > 0)
        g_backend_version = (uint32_t)driver_version;

    memset(device_name, 0, sizeof(device_name));
    r = corex_backend_device_name(device_name, sizeof(device_name), device);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuDeviceGetName failed rc=%d\n", (int)r);
        return 1;
    }

    r = corex_backend_context_create(&g_ctx, 0, device);

    if (r != CUDA_SUCCESS) {
        fprintf(stderr, "cuCtxCreate failed rc=%d\n", (int)r);
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd < 0) {
        perror("socket");
        corex_backend_context_destroy(g_ctx);
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
        corex_backend_context_destroy(g_ctx);
        return 1;
    }

    if (listen(listen_fd, 8) != 0) {
        perror("listen");
        close(listen_fd);
        corex_backend_context_destroy(g_ctx);
        return 1;
    }

    printf("===== CoreX Remote Runtime Server =====\n");
    printf("GPU=%s\n", device_name);
    printf("startup_modules=0\n");
    printf("startup_kernels=0\n");
    printf("protocol=CRX9 version=%u default_stream=LEGACY\n", CRX_PROTOCOL_VERSION);
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

        ServerSession *session = (ServerSession *)malloc(sizeof(*session));
        if (!session) {
            fprintf(stderr, "SESSION_ALLOC_FAILED\n");
            close(fd);
            continue;
        }
        server_session_init(session);

        printf("SESSION_START\n");
        printf("SESSION_IDS next_allocation_id=%llu next_stream_id=%llu\n",
               (unsigned long long)session->next_alloc_id,
               (unsigned long long)session->next_stream_id);
        fflush(stdout);

        (void)serve_session(session, fd);

        cleanup_session(session);
        free(session);
        close(fd);
        fflush(stdout);
    }

    close(listen_fd);

    if (g_ctx) {
        corex_backend_context_destroy(g_ctx);
        g_ctx = NULL;
    }

    return 0;
}
