#define _GNU_SOURCE

#include "corex_remote_cuda.h"
#include "corex_fatbin_runtime.h"
#include "corex_metadata.h"
#include "corex_device_info.h"
#include "corex_runtime_context.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

/* Remote device-info wire semantics. */
#define MAGIC   0x43525839u  /* CRX9 */
#define VERSION 3u

#ifndef COREX_REMOTE_SERVER_HOST
#define COREX_REMOTE_SERVER_HOST "127.0.0.1"
#endif

#ifndef COREX_REMOTE_SERVER_PORT
#define COREX_REMOTE_SERVER_PORT 50051
#endif

static const char *remote_server_host(void)
{
    const char *value = getenv("COREX_REMOTE_HOST");
    return value && *value ? value : COREX_REMOTE_SERVER_HOST;
}

static int remote_server_port(void)
{
    const char *value = getenv("COREX_REMOTE_PORT");
    if (!value || !*value)
        return COREX_REMOTE_SERVER_PORT;

    char *end = NULL;
    errno = 0;
    long port = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || port < 1 || port > 65535)
        return COREX_REMOTE_SERVER_PORT;

    return (int)port;
}

/*
 * One PROT_NONE arena gives CUDA-style pointer arithmetic without exposing a
 * remote CUdeviceptr. Freed spans are never reused within the process/session.
 */
#define G6A_VA_ARENA_BYTES   (64ULL * 1024ULL * 1024ULL * 1024ULL)
#define G6A_VA_ALIGNMENT     (64ULL * 1024ULL)
#define G6A_VA_GUARD_BYTES   (64ULL * 1024ULL)
#define G7C_REG_COOKIE 0x4737435245473031ULL /* G7CREG01 */
#define G6A_COPY_CHUNK       (16ULL * 1024ULL * 1024ULL)

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
    OP_ALLOC             = 1,
    OP_H2D               = 2,
    OP_SYNC              = 4,
    OP_D2H               = 5,
    OP_FREE              = 6,
    OP_CLOSE             = 7,
    OP_UPLOAD_MODULE     = 8,
    OP_GET_KERNEL        = 9,
    OP_UNLOAD_MODULE     = 10,
    OP_LAUNCH_GENERIC    = 11,
    OP_CREATE_STREAM     = 12,
    OP_DESTROY_STREAM    = 13,
    OP_STREAM_QUERY      = 14,
    OP_STREAM_SYNC       = 15,
    OP_CREATE_EVENT      = 16,
    OP_DESTROY_EVENT     = 17,
    OP_EVENT_RECORD      = 18,
    OP_EVENT_QUERY       = 19,
    OP_EVENT_SYNC          = 20,
    OP_STREAM_WAIT_EVENT   = 21,
    OP_H2D_ASYNC_SUBMIT    = 22,
    OP_D2H_ASYNC_SUBMIT    = 23,
    OP_TRANSFER_QUERY      = 24,
    OP_TRANSFER_WAIT       = 25,
    OP_GET_DEVICE_INFO     = 26,
};


enum {
    ARG_REMOTE_PTR = 1,
    ARG_I32        = 2,
    ARG_U64        = 3,
    ARG_F32        = 4,
    ARG_RAW_VALUE  = 5,
};

enum {
    STREAM_STATE_READY   = 0,
    STREAM_STATE_PENDING = 1,
};

typedef enum {
    RESOLVE_OK = 0,
    RESOLVE_INVALID,
    RESOLVE_OOB,
} ResolveResult;

typedef struct {
    uint64_t allocation_id;
    uint64_t byte_offset;
} ResolvedRemotePtr;

#define G6C_STREAM_COOKIE 0x5354524d47364331ULL /* STRMG6C1 */
#define G6C_EVENT_COOKIE  0x45564e5447364331ULL /* EVNTG6C1 */
#define G6D_MODULE_COOKIE 0x4d4f444c47364431ULL /* MODLG6D1 */
#define G6D_KERNEL_COOKIE 0x4b45524e47364431ULL /* KERNG6D1 */

static _Thread_local int g_current_device = 0;
static _Thread_local cudaError_t g_last_error = cudaSuccess;
static _Thread_local uint32_t g_last_remote_status = ST_OK;
static _Thread_local int g_last_rpc_transport_error = 0;

/* M1-S1 aliases: all non-TLS client state is owned by the singleton context. */
#define g_fd                         (corex_runtime_context_get()->fd)
#define g_next_req_id                (corex_runtime_context_get()->next_req_id)
#define g_va_arena                   (corex_runtime_context_get()->va_arena)
#define g_va_next                    (corex_runtime_context_get()->va_next)
#define g_allocs                     (corex_runtime_context_get()->allocs)
#define g_stream_handles             (corex_runtime_context_get()->stream_handles)
#define g_event_handles              (corex_runtime_context_get()->event_handles)
#define g_hidden_transfers           (corex_runtime_context_get()->hidden_transfers)
#define g_module_handles             (corex_runtime_context_get()->module_handles)
#define g_kernel_handles             (corex_runtime_context_get()->kernel_handles)
#define g_g7c_registrations          (corex_runtime_context_get()->registrations)
#define g_stream_handle_next         (corex_runtime_context_get()->stream_handle_next)
#define g_event_handle_next          (corex_runtime_context_get()->event_handle_next)
#define g_module_handle_next         (corex_runtime_context_get()->module_handle_next)
#define g_kernel_handle_next         (corex_runtime_context_get()->kernel_handle_next)
#define g_g7c_registration_next      (corex_runtime_context_get()->registration_next)
#define g_g7c_generation_next        (corex_runtime_context_get()->registration_generation_next)
#define g_transfer_submit_order      (corex_runtime_context_get()->transfer_submit_order)
#define g_default_frontier           (corex_runtime_context_get()->default_frontier)
#define g_default_next_transfer_seq  (corex_runtime_context_get()->default_next_transfer_seq)
#define g_shutdown_registered        (corex_runtime_context_get()->shutdown_registered)

static cudaError_t ensure_runtime_locked(void);
static cudaError_t map_last_rpc_error(cudaError_t fallback);

/* All callers of this helper are inside a locked RuntimeContext entry path. */
#define ensure_runtime ensure_runtime_locked

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
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)ntohl((uint32_t)(v & 0xffffffffULL)) << 32) |
           ntohl((uint32_t)(v >> 32));
#else
    return v;
#endif
}

static int g8c_take_u32(
    const unsigned char *buf,
    uint32_t len,
    size_t *pos,
    uint32_t *value_out)
{
    if (!buf || !pos || !value_out ||
        *pos > len || len - *pos < sizeof(uint32_t))
        return -1;

    uint32_t wire = 0;
    memcpy(&wire, buf + *pos, sizeof(wire));
    *pos += sizeof(wire);
    *value_out = ntohl(wire);
    return 0;
}

static int g8c_take_u64(
    const unsigned char *buf,
    uint32_t len,
    size_t *pos,
    uint64_t *value_out)
{
    if (!buf || !pos || !value_out ||
        *pos > len || len - *pos < sizeof(uint64_t))
        return -1;

    uint64_t wire = 0;
    memcpy(&wire, buf + *pos, sizeof(wire));
    *pos += sizeof(wire);
    *value_out = from_be64(wire);
    return 0;
}

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

static int connect_server(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    const char *host = remote_server_host();
    int port = remote_server_port();
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/*
 * Gate 6C still defers full server-status-to-CUDA error mapping to Gate 6E.
 * READY/PENDING query states are mapped explicitly in this slice.
 */
static int rpc(
    int fd,
    uint32_t opcode,
    const void *payload,
    uint32_t payload_len,
    unsigned char **response_out,
    uint32_t *response_len_out)
{
    g_last_remote_status = ST_OK;
    g_last_rpc_transport_error = 0;
    uint32_t req_id = g_next_req_id++;
    uint32_t h[5];

    h[0] = htonl(MAGIC);
    h[1] = htonl(VERSION);
    h[2] = htonl(opcode);
    h[3] = htonl(req_id);
    h[4] = htonl(payload_len);

    if (send_all(fd, h, sizeof(h)) != 0) {
        g_last_rpc_transport_error = 1;
        return -1;
    }

    if (payload_len > 0) {
        if (!payload || send_all(fd, payload, payload_len) != 0) {
            g_last_rpc_transport_error = 1;
            return -1;
        }
    }

    uint32_t rh[6];
    if (recv_all(fd, rh, sizeof(rh)) != 0) {
        g_last_rpc_transport_error = 1;
        return -1;
    }

    uint32_t magic = ntohl(rh[0]);
    uint32_t version = ntohl(rh[1]);
    uint32_t returned_opcode = ntohl(rh[2]);
    uint32_t returned_req_id = ntohl(rh[3]);
    uint32_t status = ntohl(rh[4]);
    uint32_t response_len = ntohl(rh[5]);

    if (magic != MAGIC ||
        version != VERSION ||
        returned_opcode != opcode ||
        returned_req_id != req_id) {
        fprintf(stderr,
                "G6E_RPC_PROTOCOL_MISMATCH opcode=%u request=%u\n",
                opcode,
                req_id);
        g_last_rpc_transport_error = 1;
        return -1;
    }

    unsigned char *response = NULL;
    if (response_len > 0) {
        response = (unsigned char *)malloc(response_len);
        if (!response) {
            g_last_rpc_transport_error = 1;
            return -1;
        }
        if (recv_all(fd, response, response_len) != 0) {
            free(response);
            g_last_rpc_transport_error = 1;
            return -1;
        }
    }

    if (status != ST_OK) {
        g_last_remote_status = status;
        fprintf(stderr,
                "G6E_RPC_REMOTE_ERROR opcode=%u request=%u status=%u\n",
                opcode,
                req_id,
                status);
        free(response);
        return -1;
    }

    if (response_out)
        *response_out = response;
    else
        free(response);

    if (response_len_out)
        *response_len_out = response_len;

    return 0;
}

static cudaError_t record_error(cudaError_t error)
{
    if (error != cudaSuccess)
        g_last_error = error;
    return error;
}

#define G6E_RETURN(error_expr) return record_error((error_expr))

static cudaError_t map_last_rpc_error(cudaError_t fallback)
{
    if (g_last_rpc_transport_error)
        return fallback;

    switch (g_last_remote_status) {
    case ST_BAD_REQUEST:
    case ST_ABI_MISMATCH:
    case ST_METADATA_ERROR:
        return cudaErrorInvalidValue;
    case ST_NOT_FOUND:
        return cudaErrorInvalidResourceHandle;
    case ST_NO_RESOURCE:
        return cudaErrorMemoryAllocation;
    case ST_CUDA_ERROR:
    case ST_INTERNAL:
        return fallback;
    case ST_OK:
    default:
        return fallback;
    }
}

static void put_u32(unsigned char *buf, size_t *pos, uint32_t value)
{
    uint32_t wire = htonl(value);
    memcpy(buf + *pos, &wire, sizeof(wire));
    *pos += sizeof(wire);
}

static void put_u64(unsigned char *buf, size_t *pos, uint64_t value)
{
    uint64_t wire = to_be64(value);
    memcpy(buf + *pos, &wire, sizeof(wire));
    *pos += sizeof(wire);
}


static unsigned char *read_file_bytes(const char *path, size_t *size_out)
{
    if (!path || !size_out)
        return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long end = ftell(fp);
    if (end <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    unsigned char *buf = (unsigned char *)malloc((size_t)end);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)end, fp) != (size_t)end) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (size_t)end;
    return buf;
}

static int remote_alloc(uint64_t bytes, uint64_t *allocation_id_out)
{
    uint64_t wire_bytes = to_be64(bytes);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd,
            OP_ALLOC,
            &wire_bytes,
            sizeof(wire_bytes),
            &resp,
            &resp_len) != 0)
        return -1;

    if (resp_len != 8) {
        free(resp);
        return -1;
    }

    uint64_t wire_id;
    memcpy(&wire_id, resp, sizeof(wire_id));
    free(resp);

    *allocation_id_out = from_be64(wire_id);
    return 0;
}

static int remote_free(uint64_t allocation_id)
{
    uint64_t wire_id = to_be64(allocation_id);
    return rpc(g_fd, OP_FREE, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_sync(void)
{
    return rpc(g_fd, OP_SYNC, NULL, 0, NULL, NULL);
}

static int remote_h2d(
    uint64_t allocation_id,
    uint64_t offset,
    const void *data,
    uint64_t bytes)
{
    if (bytes > UINT32_MAX - 24ULL)
        return -1;

    uint32_t payload_len = (uint32_t)(24ULL + bytes);
    unsigned char *payload = (unsigned char *)malloc(payload_len);
    if (!payload)
        return -1;

    size_t pos = 0;
    put_u64(payload, &pos, allocation_id);
    put_u64(payload, &pos, offset);
    put_u64(payload, &pos, bytes);
    memcpy(payload + pos, data, (size_t)bytes);
    pos += (size_t)bytes;

    int rc = -1;
    if (pos == payload_len)
        rc = rpc(g_fd, OP_H2D, payload, payload_len, NULL, NULL);

    free(payload);
    return rc;
}

static int remote_d2h(
    uint64_t allocation_id,
    uint64_t offset,
    void *data_out,
    uint64_t bytes)
{
    unsigned char payload[24];
    size_t pos = 0;

    put_u64(payload, &pos, allocation_id);
    put_u64(payload, &pos, offset);
    put_u64(payload, &pos, bytes);

    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd,
            OP_D2H,
            payload,
            sizeof(payload),
            &resp,
            &resp_len) != 0)
        return -1;

    if (resp_len != bytes) {
        free(resp);
        return -1;
    }

    if (bytes > 0)
        memcpy(data_out, resp, (size_t)bytes);

    free(resp);
    return 0;
}


static int remote_upload_module_bytes(
    const char *wire_name,
    const void *image,
    size_t image_size,
    uint64_t *module_id_out)
{
    if (!wire_name || !image || image_size == 0 || !module_id_out)
        return -1;

    size_t name_len = strlen(wire_name);
    if (name_len == 0 || name_len >= 128 || image_size > UINT32_MAX)
        return -1;

    uint64_t total64 = 4ULL + name_len + 8ULL + image_size;
    if (total64 > UINT32_MAX)
        return -1;

    uint32_t payload_len = (uint32_t)total64;
    unsigned char *payload = (unsigned char *)malloc(payload_len);
    if (!payload)
        return -1;

    size_t pos = 0;
    put_u32(payload, &pos, (uint32_t)name_len);
    memcpy(payload + pos, wire_name, name_len);
    pos += name_len;
    put_u64(payload, &pos, image_size);
    memcpy(payload + pos, image, image_size);
    pos += image_size;

    unsigned char *resp = NULL;
    uint32_t resp_len = 0;
    int rc = -1;

    if (pos == payload_len &&
        rpc(g_fd,
            OP_UPLOAD_MODULE,
            payload,
            payload_len,
            &resp,
            &resp_len) == 0 &&
        resp_len == 8) {
        uint64_t wire_id;
        memcpy(&wire_id, resp, sizeof(wire_id));
        *module_id_out = from_be64(wire_id);
        rc = 0;
    }

    free(resp);
    free(payload);
    return rc;
}

static int remote_get_kernel(
    uint64_t module_id,
    const char *name,
    uint64_t *kernel_id_out)
{
    if (!name || !kernel_id_out)
        return -1;

    size_t name_len = strlen(name);
    if (name_len == 0 || name_len >= 128)
        return -1;

    uint64_t total64 = 12ULL + name_len;
    if (total64 > UINT32_MAX)
        return -1;

    uint32_t payload_len = (uint32_t)total64;
    unsigned char *payload = (unsigned char *)malloc(payload_len);
    if (!payload)
        return -1;

    size_t pos = 0;
    put_u64(payload, &pos, module_id);
    put_u32(payload, &pos, (uint32_t)name_len);
    memcpy(payload + pos, name, name_len);
    pos += name_len;

    unsigned char *resp = NULL;
    uint32_t resp_len = 0;
    int rc = -1;
    if (pos == payload_len &&
        rpc(g_fd, OP_GET_KERNEL, payload, payload_len, &resp, &resp_len) == 0 &&
        resp_len == 8) {
        uint64_t wire_id;
        memcpy(&wire_id, resp, sizeof(wire_id));
        *kernel_id_out = from_be64(wire_id);
        rc = 0;
    }

    free(resp);
    free(payload);
    return rc;
}

static int remote_unload_module(uint64_t module_id)
{
    uint64_t wire_id = to_be64(module_id);
    return rpc(g_fd, OP_UNLOAD_MODULE, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_create_stream(uint64_t *stream_id_out)
{
    uint32_t flags = htonl(0);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd, OP_CREATE_STREAM, &flags, sizeof(flags), &resp, &resp_len) != 0)
        return -1;
    if (resp_len != 8) {
        free(resp);
        return -1;
    }

    uint64_t wire_id;
    memcpy(&wire_id, resp, sizeof(wire_id));
    free(resp);
    *stream_id_out = from_be64(wire_id);
    return 0;
}

static int remote_destroy_stream(uint64_t stream_id)
{
    uint64_t wire_id = to_be64(stream_id);
    return rpc(g_fd, OP_DESTROY_STREAM, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_stream_sync(uint64_t stream_id)
{
    uint64_t wire_id = to_be64(stream_id);
    return rpc(g_fd, OP_STREAM_SYNC, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_stream_query(uint64_t stream_id, uint32_t *state_out)
{
    uint64_t wire_id = to_be64(stream_id);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd, OP_STREAM_QUERY, &wire_id, sizeof(wire_id), &resp, &resp_len) != 0)
        return -1;
    if (resp_len != 4) {
        free(resp);
        return -1;
    }

    uint32_t wire_state;
    memcpy(&wire_state, resp, sizeof(wire_state));
    free(resp);
    *state_out = ntohl(wire_state);
    return 0;
}

static int remote_create_event(uint64_t *event_id_out)
{
    uint32_t flags = htonl(0);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd, OP_CREATE_EVENT, &flags, sizeof(flags), &resp, &resp_len) != 0)
        return -1;
    if (resp_len != 8) {
        free(resp);
        return -1;
    }

    uint64_t wire_id;
    memcpy(&wire_id, resp, sizeof(wire_id));
    free(resp);
    *event_id_out = from_be64(wire_id);
    return 0;
}

static int remote_destroy_event(uint64_t event_id)
{
    uint64_t wire_id = to_be64(event_id);
    return rpc(g_fd, OP_DESTROY_EVENT, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_event_record(uint64_t event_id, uint64_t stream_id)
{
    unsigned char payload[16];
    size_t pos = 0;
    put_u64(payload, &pos, event_id);
    put_u64(payload, &pos, stream_id);
    return rpc(g_fd, OP_EVENT_RECORD, payload, sizeof(payload), NULL, NULL);
}

static int remote_event_sync(uint64_t event_id)
{
    uint64_t wire_id = to_be64(event_id);
    return rpc(g_fd, OP_EVENT_SYNC, &wire_id, sizeof(wire_id), NULL, NULL);
}

static int remote_event_query(uint64_t event_id, uint32_t *state_out)
{
    uint64_t wire_id = to_be64(event_id);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd, OP_EVENT_QUERY, &wire_id, sizeof(wire_id), &resp, &resp_len) != 0)
        return -1;
    if (resp_len != 4) {
        free(resp);
        return -1;
    }

    uint32_t wire_state;
    memcpy(&wire_state, resp, sizeof(wire_state));
    free(resp);
    *state_out = ntohl(wire_state);
    return 0;
}

static int remote_stream_wait_event(uint64_t stream_id, uint64_t event_id)
{
    unsigned char payload[20];
    size_t pos = 0;
    put_u64(payload, &pos, stream_id);
    put_u64(payload, &pos, event_id);
    put_u32(payload, &pos, 0);
    return rpc(g_fd, OP_STREAM_WAIT_EVENT, payload, sizeof(payload), NULL, NULL);
}

static int remote_h2d_async_submit(
    uint64_t allocation_id,
    uint64_t offset,
    uint64_t stream_id,
    const void *data,
    uint64_t bytes,
    uint64_t *transfer_id_out)
{
    if (!data || !transfer_id_out || bytes == 0 || bytes > UINT32_MAX - 32ULL)
        return -1;

    uint32_t payload_len = (uint32_t)(32ULL + bytes);
    unsigned char *payload = (unsigned char *)malloc(payload_len);
    if (!payload)
        return -1;

    size_t pos = 0;
    put_u64(payload, &pos, allocation_id);
    put_u64(payload, &pos, offset);
    put_u64(payload, &pos, stream_id);
    put_u64(payload, &pos, bytes);
    memcpy(payload + pos, data, (size_t)bytes);
    pos += (size_t)bytes;

    unsigned char *resp = NULL;
    uint32_t resp_len = 0;
    int rc = -1;

    if (pos == payload_len &&
        rpc(g_fd,
            OP_H2D_ASYNC_SUBMIT,
            payload,
            payload_len,
            &resp,
            &resp_len) == 0 &&
        resp_len == 8) {
        uint64_t wire_id;
        memcpy(&wire_id, resp, sizeof(wire_id));
        *transfer_id_out = from_be64(wire_id);
        rc = 0;
    }

    free(resp);
    free(payload);
    return rc;
}

static int remote_d2h_async_submit(
    uint64_t allocation_id,
    uint64_t offset,
    uint64_t stream_id,
    uint64_t bytes,
    uint64_t *transfer_id_out)
{
    if (!transfer_id_out || bytes == 0 || bytes > UINT32_MAX)
        return -1;

    unsigned char payload[32];
    size_t pos = 0;
    put_u64(payload, &pos, allocation_id);
    put_u64(payload, &pos, offset);
    put_u64(payload, &pos, stream_id);
    put_u64(payload, &pos, bytes);

    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd,
            OP_D2H_ASYNC_SUBMIT,
            payload,
            sizeof(payload),
            &resp,
            &resp_len) != 0)
        return -1;

    if (resp_len != 8) {
        free(resp);
        return -1;
    }

    uint64_t wire_id;
    memcpy(&wire_id, resp, sizeof(wire_id));
    free(resp);
    *transfer_id_out = from_be64(wire_id);
    return 0;
}

static int remote_transfer_query(uint64_t transfer_id, uint32_t *state_out)
{
    if (!state_out)
        return -1;

    uint64_t wire_id = to_be64(transfer_id);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd,
            OP_TRANSFER_QUERY,
            &wire_id,
            sizeof(wire_id),
            &resp,
            &resp_len) != 0)
        return -1;

    if (resp_len != 4) {
        free(resp);
        return -1;
    }

    uint32_t wire_state;
    memcpy(&wire_state, resp, sizeof(wire_state));
    free(resp);
    *state_out = ntohl(wire_state);
    return 0;
}

static int remote_transfer_wait(
    uint64_t transfer_id,
    void *data_out,
    uint32_t expected_bytes)
{
    uint64_t wire_id = to_be64(transfer_id);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(g_fd,
            OP_TRANSFER_WAIT,
            &wire_id,
            sizeof(wire_id),
            &resp,
            &resp_len) != 0)
        return -1;

    if (resp_len != expected_bytes) {
        free(resp);
        return -1;
    }

    if (expected_bytes > 0) {
        if (!data_out) {
            free(resp);
            return -1;
        }
        memcpy(data_out, resp, expected_bytes);
    }

    free(resp);
    return 0;
}


static int remote_get_device_info(
    uint32_t logical_device,
    CorexRemoteDeviceInfo *info_out)
{
    if (!info_out)
        return -1;

    uint32_t wire_device = htonl(logical_device);
    unsigned char *resp = NULL;
    uint32_t resp_len = 0;

    if (rpc(
            g_fd,
            OP_GET_DEVICE_INFO,
            &wire_device,
            sizeof(wire_device),
            &resp,
            &resp_len) != 0)
        return -1;

    memset(info_out, 0, sizeof(*info_out));

    size_t pos = 0;
    uint32_t dto_version = 0;
    uint32_t returned_device = 0;

#define TAKE32(dst) \
    do { \
        if (g8c_take_u32(resp, resp_len, &pos, &(dst)) != 0) \
            goto parse_fail; \
    } while (0)

#define TAKE64(dst) \
    do { \
        if (g8c_take_u64(resp, resp_len, &pos, &(dst)) != 0) \
            goto parse_fail; \
    } while (0)

    TAKE32(dto_version);
    TAKE32(returned_device);

    if (dto_version != COREX_REMOTE_DEVICE_INFO_DTO_VERSION ||
        returned_device != logical_device)
        goto parse_fail;

    if (pos > resp_len ||
        resp_len - pos < COREX_REMOTE_DEVICE_NAME_BYTES)
        goto parse_fail;

    memcpy(
        info_out->name,
        resp + pos,
        COREX_REMOTE_DEVICE_NAME_BYTES);
    info_out->name[COREX_REMOTE_DEVICE_NAME_BYTES - 1] = '\0';
    pos += COREX_REMOTE_DEVICE_NAME_BYTES;

    TAKE64(info_out->total_global_mem);
    TAKE64(info_out->free_mem);
    TAKE64(info_out->shared_mem_per_block);

    TAKE32(info_out->regs_per_block);
    TAKE32(info_out->warp_size);
    TAKE64(info_out->mem_pitch);

    TAKE32(info_out->max_threads_per_block);
    for (size_t i = 0; i < 3; ++i)
        TAKE32(info_out->max_threads_dim[i]);
    for (size_t i = 0; i < 3; ++i)
        TAKE32(info_out->max_grid_size[i]);

    TAKE32(info_out->clock_rate);
    TAKE64(info_out->total_const_mem);

    TAKE32(info_out->major);
    TAKE32(info_out->minor);

    TAKE64(info_out->texture_alignment);

    TAKE32(info_out->device_overlap);
    TAKE32(info_out->multi_processor_count);
    TAKE32(info_out->kernel_exec_timeout_enabled);
    TAKE32(info_out->integrated);
    TAKE32(info_out->can_map_host_memory);
    TAKE32(info_out->compute_mode);
    TAKE32(info_out->concurrent_kernels);
    TAKE32(info_out->ecc_enabled);
    TAKE32(info_out->pci_bus_id);
    TAKE32(info_out->pci_device_id);
    TAKE32(info_out->tcc_driver);
    TAKE32(info_out->memory_clock_rate);
    TAKE32(info_out->memory_bus_width);
    TAKE32(info_out->l2_cache_size);
    TAKE32(info_out->max_threads_per_multiprocessor);
    TAKE32(info_out->async_engine_count);
    TAKE32(info_out->unified_addressing);

#undef TAKE32
#undef TAKE64

    if (pos != resp_len)
        goto parse_fail_no_macros;

    free(resp);
    return 0;

parse_fail:
#undef TAKE32
#undef TAKE64
parse_fail_no_macros:
    free(resp);
    return -1;
}

static int align_up_size(size_t value, size_t alignment, size_t *out)
{
    if (alignment == 0)
        return -1;

    size_t mask = alignment - 1;
    if ((alignment & mask) != 0)
        return -1;
    if (value > SIZE_MAX - mask)
        return -1;

    *out = (value + mask) & ~mask;
    return 0;
}

static int ensure_arena(void)
{
    if (g_va_arena)
        return 0;

    void *p = mmap(NULL,
                   (size_t)G6A_VA_ARENA_BYTES,
                   PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                   -1,
                   0);

    if (p == MAP_FAILED)
        return -1;

    g_va_arena = (unsigned char *)p;
    g_va_next = 0;

    printf("G6A_VA_ARENA base=%p bytes=%llu protection=PROT_NONE\n",
           (void *)g_va_arena,
           (unsigned long long)G6A_VA_ARENA_BYTES);

    return 0;
}

static VirtualAllocation *find_free_slot(void)
{
    for (size_t i = 0; i < G6A_MAX_ALLOCS; ++i) {
        if (!g_allocs[i].live)
            return &g_allocs[i];
    }
    return NULL;
}

static VirtualAllocation *find_exact_live_base(const void *ptr)
{
    uintptr_t value = (uintptr_t)ptr;

    for (size_t i = 0; i < G6A_MAX_ALLOCS; ++i) {
        if (g_allocs[i].live && g_allocs[i].virtual_base == value)
            return &g_allocs[i];
    }

    return NULL;
}

static ResolveResult resolve_remote_ptr(
    const void *ptr,
    size_t access_bytes,
    ResolvedRemotePtr *resolved)
{
    uintptr_t value = (uintptr_t)ptr;

    for (size_t i = 0; i < G6A_MAX_ALLOCS; ++i) {
        const VirtualAllocation *a = &g_allocs[i];
        if (!a->live)
            continue;

        uintptr_t base = a->virtual_base;
        uintptr_t end = base + a->size;

        if (value < base || value > end)
            continue;

        size_t offset = (size_t)(value - base);
        if (offset > a->size || access_bytes > a->size - offset)
            return RESOLVE_OOB;

        if (access_bytes > 0 && offset == a->size)
            return RESOLVE_OOB;

        resolved->allocation_id = a->allocation_id;
        resolved->byte_offset = (uint64_t)offset;

        printf("G6A_PTR_RESOLVE ptr=%p allocation_id=%llu offset=%llu bytes=%zu\n",
               ptr,
               (unsigned long long)resolved->allocation_id,
               (unsigned long long)resolved->byte_offset,
               access_bytes);

        return RESOLVE_OK;
    }

    return RESOLVE_INVALID;
}

static struct corexCudaStreamHandle *resolve_stream_handle(cudaStream_t stream)
{
    if (!stream)
        return NULL;

    for (size_t i = 0; i < g_stream_handle_next; ++i) {
        struct corexCudaStreamHandle *h = &g_stream_handles[i];
        if ((cudaStream_t)h != stream)
            continue;
        if (h->cookie != G6C_STREAM_COOKIE || !h->live)
            return NULL;
        return h;
    }

    return NULL;
}

static struct corexCudaEventHandle *resolve_event_handle(cudaEvent_t event)
{
    if (!event)
        return NULL;

    for (size_t i = 0; i < g_event_handle_next; ++i) {
        struct corexCudaEventHandle *h = &g_event_handles[i];
        if ((cudaEvent_t)h != event)
            continue;
        if (h->cookie != G6C_EVENT_COOKIE || !h->live)
            return NULL;
        return h;
    }

    return NULL;
}


static struct corexRemoteModuleHandle *resolve_module_handle(corexRemoteModule_t module)
{
    if (!module)
        return NULL;
    for (size_t i = 0; i < g_module_handle_next; ++i) {
        struct corexRemoteModuleHandle *h = &g_module_handles[i];
        if ((corexRemoteModule_t)h != module)
            continue;
        if (h->cookie != G6D_MODULE_COOKIE || !h->live)
            return NULL;
        return h;
    }
    return NULL;
}

static corexRemoteKernelHandle *resolve_kernel_reference(const void *func)
{
    if (!func)
        return NULL;

    for (size_t i = 0; i < g_kernel_handle_next; ++i) {
        corexRemoteKernelHandle *h = &g_kernel_handles[i];

        if (h->cookie != G6D_KERNEL_COOKIE || !h->live)
            continue;

        if ((const void *)h == func)
            return h;

        if (h->origin == G7D_KERNEL_ORIGIN_COMPILER &&
            h->host_fun == func)
            return h;
    }

    return NULL;
}

static corexRemoteKernelHandle *find_live_compiler_kernel_by_host_fun(
    const void *host_fun)
{
    if (!host_fun)
        return NULL;

    for (size_t i = 0; i < g_kernel_handle_next; ++i) {
        corexRemoteKernelHandle *h = &g_kernel_handles[i];

        if (h->cookie != G6D_KERNEL_COOKIE || !h->live)
            continue;
        if (h->origin != G7D_KERNEL_ORIGIN_COMPILER)
            continue;
        if (h->host_fun == host_fun)
            return h;
    }

    return NULL;
}

static void invalidate_local_module_kernels(uint64_t module_id)
{
    for (size_t i = 0; i < g_kernel_handle_next; ++i) {
        corexRemoteKernelHandle *h = &g_kernel_handles[i];
        if (h->live && h->module_id == module_id) {
            printf("G6D_KERNEL_TOKEN_INVALIDATE token=%p host_fun=%p kernel_id=%llu module_id=%llu name=%s origin=%s\n",
                   (void *)h,
                   h->host_fun,
                   (unsigned long long)h->kernel_id,
                   (unsigned long long)module_id,
                   h->name,
                   h->origin == G7D_KERNEL_ORIGIN_COMPILER ? "COMPILER" : "CONTROLLED");
            h->live = 0;
            h->kernel_id = 0;
        }
    }
}


static G7CFatbinRegistration *g7c_resolve_registration(void **handle)
{
    if (!handle)
        return NULL;

    for (size_t i = 0; i < g_g7c_registration_next; ++i) {
        G7CFatbinRegistration *reg = &g_g7c_registrations[i];
        if ((void **)reg != handle)
            continue;
        if (reg->cookie != G7C_REG_COOKIE)
            return NULL;
        return reg;
    }

    return NULL;
}

static const char *g7c_registration_state_name(G7CRegistrationState state)
{
    switch (state) {
    case G7C_REG_REGISTERING:
        return "REGISTERING";
    case G7C_REG_REMOTE_READY:
        return "REMOTE_READY";
    case G7C_REG_LIVE:
        return "LIVE";
    case G7C_REG_FAILED:
        return "FAILED";
    case G7C_REG_UNLOAD_FAILED:
        return "UNLOAD_FAILED";
    case G7C_REG_DEAD:
        return "DEAD";
    case G7C_REG_EMPTY:
    default:
        return "EMPTY";
    }
}

static cudaError_t module_load_bytes_after_runtime(
    const char *wire_name,
    const void *image,
    size_t image_size,
    corexRemoteModule_t *module_out)
{
    if (!wire_name || !image || image_size == 0 || !module_out)
        return cudaErrorInvalidValue;

    *module_out = NULL;

    if (g_module_handle_next >= G6D_MAX_MODULE_HANDLES)
        return cudaErrorMemoryAllocation;

    uint64_t module_id = 0;
    if (remote_upload_module_bytes(
            wire_name,
            image,
            image_size,
            &module_id) != 0)
        return map_last_rpc_error(cudaErrorUnknown);

    struct corexRemoteModuleHandle *h =
        &g_module_handles[g_module_handle_next++];
    memset(h, 0, sizeof(*h));
    h->cookie = G6D_MODULE_COOKIE;
    h->module_id = module_id;
    h->live = 1;

    *module_out = (corexRemoteModule_t)h;
    return cudaSuccess;
}

static cudaError_t module_unload_after_runtime(corexRemoteModule_t module)
{
    struct corexRemoteModuleHandle *h = resolve_module_handle(module);
    if (!h)
        return cudaErrorInvalidResourceHandle;

    uint64_t module_id = h->module_id;
    if (remote_unload_module(module_id) != 0)
        return map_last_rpc_error(cudaErrorUnknown);

    invalidate_local_module_kernels(module_id);
    h->live = 0;
    h->module_id = 0;

    printf("G6D_MODULE_UNLOAD handle=%p module_id=%llu result=PASS tombstone=YES\n",
           (void *)module,
           (unsigned long long)module_id);
    return cudaSuccess;
}

typedef struct {
    cudaError_t last_error;
    uint32_t remote_status;
    int rpc_transport_error;
} G7CHiddenErrorSnapshot;

static G7CHiddenErrorSnapshot g7c_snapshot_hidden_error_state(void)
{
    G7CHiddenErrorSnapshot snapshot;
    snapshot.last_error = g_last_error;
    snapshot.remote_status = g_last_remote_status;
    snapshot.rpc_transport_error = g_last_rpc_transport_error;
    return snapshot;
}

static void g7c_restore_hidden_error_state(G7CHiddenErrorSnapshot snapshot)
{
    g_last_error = snapshot.last_error;
    g_last_remote_status = snapshot.remote_status;
    g_last_rpc_transport_error = snapshot.rpc_transport_error;
}

static cudaError_t g7c_hidden_module_load_bytes(
    const char *wire_name,
    const void *image,
    size_t image_size,
    corexRemoteModule_t *module_out)
{
    G7CHiddenErrorSnapshot snapshot =
        g7c_snapshot_hidden_error_state();

    cudaError_t result = ensure_runtime();
    if (result == cudaSuccess) {
        result =
            module_load_bytes_after_runtime(
                wire_name,
                image,
                image_size,
                module_out);
    }

    g7c_restore_hidden_error_state(snapshot);
    return result;
}

static cudaError_t g7c_hidden_module_unload(corexRemoteModule_t module)
{
    G7CHiddenErrorSnapshot snapshot =
        g7c_snapshot_hidden_error_state();

    cudaError_t result = module_unload_after_runtime(module);

    g7c_restore_hidden_error_state(snapshot);
    return result;
}

static HiddenTransfer *find_free_hidden_transfer(void)
{
    for (size_t i = 0; i < G6C_MAX_HIDDEN_TRANSFERS; ++i) {
        if (!g_hidden_transfers[i].live)
            return &g_hidden_transfers[i];
    }
    return NULL;
}

static size_t live_hidden_transfer_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < G6C_MAX_HIDDEN_TRANSFERS; ++i) {
        if (g_hidden_transfers[i].live)
            count++;
    }
    return count;
}

static int transfer_covered_by_frontier(
    const HiddenTransfer *t,
    const uint64_t *frontier)
{
    if (!t || !t->live || !frontier)
        return 0;
    if (t->origin_stream_slot >= G6E_FRONTIER_SLOTS)
        return 0;
    return t->origin_stream_seq <= frontier[t->origin_stream_slot];
}

static HiddenTransfer *oldest_matching_transfer(
    const uint64_t *frontier,
    int match_allocation,
    uint64_t allocation_id)
{
    HiddenTransfer *best = NULL;

    for (size_t i = 0; i < G6C_MAX_HIDDEN_TRANSFERS; ++i) {
        HiddenTransfer *t = &g_hidden_transfers[i];
        if (!t->live)
            continue;
        if (frontier && !transfer_covered_by_frontier(t, frontier))
            continue;
        if (match_allocation && t->allocation_id != allocation_id)
            continue;
        if (!best || t->submit_order < best->submit_order)
            best = t;
    }

    return best;
}

static cudaError_t consume_hidden_transfer(HiddenTransfer *t)
{
    if (!t || !t->live)
        G6E_RETURN(cudaErrorUnknown);

    uint32_t expected =
        t->kind == HIDDEN_TRANSFER_D2H ? (uint32_t)t->bytes : 0u;
    void *dst = t->kind == HIDDEN_TRANSFER_D2H ? t->host_dst : NULL;

    if (remote_transfer_wait(t->transfer_id, dst, expected) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6C_TRANSFER_DRAIN transfer_id=%llu kind=%s origin_stream_slot=%zu seq=%llu bytes=%zu host_visible=%s retire=YES\n",
           (unsigned long long)t->transfer_id,
           t->kind == HIDDEN_TRANSFER_H2D ? "H2D" : "D2H",
           t->origin_stream_slot,
           (unsigned long long)t->origin_stream_seq,
           t->bytes,
           t->kind == HIDDEN_TRANSFER_D2H ? "YES" : "N/A");

    memset(t, 0, sizeof(*t));
    return cudaSuccess;
}

static cudaError_t drain_frontier_blocking(const uint64_t *frontier)
{
    for (;;) {
        HiddenTransfer *t = oldest_matching_transfer(frontier, 0, 0);
        if (!t)
            return cudaSuccess;

        cudaError_t err = consume_hidden_transfer(t);
        if (err != cudaSuccess)
            G6E_RETURN(err);
    }
}

/*
 * Query paths must remain non-blocking. First prove that every covered live
 * TransferID is READY; only then consume them. For an exact causal frontier,
 * a READY Stream/Event implies these transfer events are READY as well.
 */
static cudaError_t drain_frontier_if_ready(
    const uint64_t *frontier,
    int *all_ready_out)
{
    if (!all_ready_out)
        G6E_RETURN(cudaErrorInvalidValue);

    *all_ready_out = 1;

    for (size_t i = 0; i < G6C_MAX_HIDDEN_TRANSFERS; ++i) {
        HiddenTransfer *t = &g_hidden_transfers[i];
        if (!transfer_covered_by_frontier(t, frontier))
            continue;

        uint32_t state = STREAM_STATE_READY;
        if (remote_transfer_query(t->transfer_id, &state) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

        if (state == STREAM_STATE_PENDING) {
            *all_ready_out = 0;
            return cudaSuccess;
        }
        if (state != STREAM_STATE_READY)
            G6E_RETURN(cudaErrorUnknown);
    }

    return drain_frontier_blocking(frontier);
}

static cudaError_t drain_all_transfers_blocking(void)
{
    for (;;) {
        HiddenTransfer *t = oldest_matching_transfer(NULL, 0, 0);
        if (!t)
            return cudaSuccess;

        cudaError_t err = consume_hidden_transfer(t);
        if (err != cudaSuccess)
            G6E_RETURN(err);
    }
}

static cudaError_t drain_allocation_transfers_blocking(uint64_t allocation_id)
{
    for (;;) {
        HiddenTransfer *t =
            oldest_matching_transfer(NULL, 1, allocation_id);
        if (!t)
            return cudaSuccess;

        cudaError_t err = consume_hidden_transfer(t);
        if (err != cudaSuccess)
            G6E_RETURN(err);
    }
}

static void frontier_merge(uint64_t *dst, const uint64_t *src)
{
    if (!dst || !src)
        return;

    for (size_t i = 0; i < G6E_FRONTIER_SLOTS; ++i) {
        if (src[i] > dst[i])
            dst[i] = src[i];
    }
}

typedef struct {
    uint64_t stream_id;
    size_t slot_index;
    uint64_t *frontier;
    uint64_t *next_transfer_seq;
    int is_default;
} G6EStreamView;

static int get_stream_view(cudaStream_t stream, G6EStreamView *view)
{
    if (!view)
        return -1;
    memset(view, 0, sizeof(*view));

    if (stream == NULL) {
        if (!g_default_frontier)
            return -1;
        view->stream_id = 0;
        view->slot_index = G6E_DEFAULT_STREAM_SLOT;
        view->frontier = g_default_frontier;
        view->next_transfer_seq = &g_default_next_transfer_seq;
        view->is_default = 1;
        return 0;
    }

    struct corexCudaStreamHandle *h = resolve_stream_handle(stream);
    if (!h)
        return -1;
    view->stream_id = h->stream_id;
    view->slot_index = h->slot_index;
    view->frontier = h->frontier;
    view->next_transfer_seq = &h->next_transfer_seq;
    view->is_default = 0;
    return 0;
}

/*
 * Gate 6E V1 models CUDA legacy default-stream ordering conservatively.
 * All explicit streams are CU_STREAM_DEFAULT (blocking streams).
 */
static void apply_legacy_default_ordering(G6EStreamView *view)
{
    if (!view || !view->frontier || !g_default_frontier)
        return;

    if (view->is_default) {
        for (size_t i = 0; i < g_stream_handle_next; ++i) {
            if (g_stream_handles[i].live && g_stream_handles[i].frontier)
                frontier_merge(g_default_frontier, g_stream_handles[i].frontier);
        }
    } else {
        frontier_merge(view->frontier, g_default_frontier);
    }
}

static void shutdown_atexit(void)
{
    corexRemoteRuntimeShutdown();
}

static cudaError_t ensure_runtime(void)
{
    if (g_fd >= 0)
        return cudaSuccess;

    if (ensure_arena() != 0)
        G6E_RETURN(cudaErrorMemoryAllocation);

    if (!g_default_frontier) {
        g_default_frontier =
            (uint64_t *)calloc(G6E_FRONTIER_SLOTS, sizeof(uint64_t));
        if (!g_default_frontier)
            G6E_RETURN(cudaErrorMemoryAllocation);
    }

    g_fd = connect_server();
    if (g_fd < 0)
        G6E_RETURN(cudaErrorInitializationError);

    if (!g_shutdown_registered) {
        if (atexit(shutdown_atexit) == 0)
            g_shutdown_registered = 1;
    }

    printf("G6E_REMOTE_SESSION=CONNECTED host=%s port=%d protocol=CRX9 version=%u default_stream=LEGACY\n",
           remote_server_host(),
           remote_server_port(),
           VERSION);

    return cudaSuccess;
}

static int corexRemoteGetDeviceInfoInternal_locked(
    uint32_t logical_device,
    CorexRemoteDeviceInfo *info_out)
{
    if (!info_out)
        return (int)record_error(cudaErrorInvalidValue);

    if (logical_device != 0)
        return (int)record_error(cudaErrorInvalidDevice);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        return (int)record_error(init);

    if (remote_get_device_info(logical_device, info_out) != 0)
        return (int)record_error(
            map_last_rpc_error(cudaErrorUnknown));

    return (int)cudaSuccess;
}

int corexRemoteRecordErrorInternal(int error_code)
{
    return (int)record_error((cudaError_t)error_code);
}

static cudaError_t cudaGetDeviceCount_locked(int *count)
{
    if (!count)
        G6E_RETURN(cudaErrorInvalidValue);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    /* Gate 6A contract: one configured remote endpoint/device. */
    *count = 1;
    return cudaSuccess;
}

static cudaError_t cudaGetDevice_locked(int *device)
{
    if (!device)
        G6E_RETURN(cudaErrorInvalidValue);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    *device = g_current_device;
    return cudaSuccess;
}

static cudaError_t cudaSetDevice_locked(int device)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (device != 0)
        G6E_RETURN(cudaErrorInvalidDevice);

    g_current_device = 0;
    return cudaSuccess;
}

static cudaError_t cudaMalloc_locked(void **devPtr, size_t size)
{
    if (!devPtr || size == 0)
        G6E_RETURN(cudaErrorInvalidValue);

    *devPtr = NULL;

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    VirtualAllocation *slot = find_free_slot();
    if (!slot)
        G6E_RETURN(cudaErrorMemoryAllocation);

    size_t aligned_size = 0;
    if (align_up_size(size, (size_t)G6A_VA_ALIGNMENT, &aligned_size) != 0)
        G6E_RETURN(cudaErrorMemoryAllocation);

    if (aligned_size > SIZE_MAX - (size_t)G6A_VA_GUARD_BYTES)
        G6E_RETURN(cudaErrorMemoryAllocation);

    size_t span = aligned_size + (size_t)G6A_VA_GUARD_BYTES;
    if (g_va_next > (size_t)G6A_VA_ARENA_BYTES ||
        span > (size_t)G6A_VA_ARENA_BYTES - g_va_next)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uint64_t allocation_id = 0;
    if (remote_alloc((uint64_t)size, &allocation_id) != 0)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uintptr_t base = (uintptr_t)(g_va_arena + g_va_next);

    /* Commit local identity only after the remote AllocationID exists. */
    memset(slot, 0, sizeof(*slot));
    slot->live = 1;
    slot->virtual_base = base;
    slot->size = size;
    slot->allocation_id = allocation_id;

    g_va_next += span;
    *devPtr = (void *)base;

    printf("G6A_CUDA_MALLOC ptr=%p allocation_id=%llu bytes=%zu span=%zu result=PASS\n",
           *devPtr,
           (unsigned long long)allocation_id,
           size,
           span);

    return cudaSuccess;
}

static cudaError_t cudaFree_locked(void *devPtr)
{
    if (!devPtr)
        return cudaSuccess;

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    VirtualAllocation *slot = find_exact_live_base(devPtr);
    if (!slot)
        G6E_RETURN(cudaErrorInvalidDevicePointer);

    uint64_t allocation_id = slot->allocation_id;

    /*
     * Gate 6C hidden transfers that reference this AllocationID must be
     * consumed before the AllocationID disappears. D2H consumption is also
     * the point where remote pinned staging becomes visible in the caller's
     * host buffer.
     */
    cudaError_t drain_err =
        drain_allocation_transfers_blocking(allocation_id);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);

    /*
     * Important ordering invariant:
     * keep the local mapping live until the Gate 5E server confirms FREE.
     */
    if (remote_free(allocation_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6A_CUDA_FREE ptr=%p allocation_id=%llu result=PASS tombstone=YES\n",
           devPtr,
           (unsigned long long)allocation_id);

    slot->live = 0;
    slot->allocation_id = 0;
    /* virtual_base/size are intentionally not reused by g_va_next. */

    return cudaSuccess;
}

static cudaError_t map_resolve_error(ResolveResult result)
{
    if (result == RESOLVE_OOB)
        G6E_RETURN(cudaErrorInvalidValue);
    G6E_RETURN(cudaErrorInvalidDevicePointer);
}

static cudaError_t cudaMemcpy_locked(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind)
{
    if (count == 0)
        return cudaSuccess;
    if (!dst || !src)
        G6E_RETURN(cudaErrorInvalidValue);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    ResolvedRemotePtr remote;
    ResolveResult rr;

    if (kind == cudaMemcpyHostToDevice) {
        rr = resolve_remote_ptr(dst, count, &remote);
        if (rr != RESOLVE_OK)
            G6E_RETURN(map_resolve_error(rr));

        const unsigned char *host = (const unsigned char *)src;
        size_t done = 0;
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > (size_t)G6A_COPY_CHUNK)
                chunk = (size_t)G6A_COPY_CHUNK;

            if (remote_h2d(remote.allocation_id,
                           remote.byte_offset + done,
                           host + done,
                           chunk) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));
            done += chunk;
        }

        printf("G6A_CUDA_MEMCPY kind=H2D allocation_id=%llu offset=%llu bytes=%zu result=PASS\n",
               (unsigned long long)remote.allocation_id,
               (unsigned long long)remote.byte_offset,
               count);
        return cudaSuccess;
    }

    if (kind == cudaMemcpyDeviceToHost) {
        rr = resolve_remote_ptr(src, count, &remote);
        if (rr != RESOLVE_OK)
            G6E_RETURN(map_resolve_error(rr));

        unsigned char *host = (unsigned char *)dst;
        size_t done = 0;
        while (done < count) {
            size_t chunk = count - done;
            if (chunk > (size_t)G6A_COPY_CHUNK)
                chunk = (size_t)G6A_COPY_CHUNK;

            if (remote_d2h(remote.allocation_id,
                           remote.byte_offset + done,
                           host + done,
                           chunk) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));
            done += chunk;
        }

        printf("G6A_CUDA_MEMCPY kind=D2H allocation_id=%llu offset=%llu bytes=%zu result=PASS\n",
               (unsigned long long)remote.allocation_id,
               (unsigned long long)remote.byte_offset,
               count);
        return cudaSuccess;
    }

    if (kind == cudaMemcpyHostToHost ||
        kind == cudaMemcpyDeviceToDevice ||
        kind == cudaMemcpyDefault)
        G6E_RETURN(cudaErrorNotSupported);

    G6E_RETURN(cudaErrorInvalidMemcpyDirection);
}

static cudaError_t cudaMemcpyAsync_locked(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind,
    cudaStream_t stream)
{
    if (count == 0)
        return cudaSuccess;
    if (!dst || !src)
        G6E_RETURN(cudaErrorInvalidValue);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        G6E_RETURN(cudaErrorInvalidResourceHandle);
    apply_legacy_default_ordering(&sv);

    ResolvedRemotePtr remote;
    ResolveResult rr;

    if (kind == cudaMemcpyHostToDevice) {
        rr = resolve_remote_ptr(dst, count, &remote);
        if (rr != RESOLVE_OK)
            G6E_RETURN(map_resolve_error(rr));

        const unsigned char *host = (const unsigned char *)src;
        size_t done = 0;

        while (done < count) {
            size_t chunk = count - done;
            if (chunk > (size_t)G6A_COPY_CHUNK)
                chunk = (size_t)G6A_COPY_CHUNK;

            HiddenTransfer *slot = find_free_hidden_transfer();
            if (!slot)
                G6E_RETURN(cudaErrorMemoryAllocation);

            uint64_t transfer_id = 0;
            if (remote_h2d_async_submit(
                    remote.allocation_id,
                    remote.byte_offset + done,
                    sv.stream_id,
                    host + done,
                    chunk,
                    &transfer_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

            uint64_t seq = (*sv.next_transfer_seq) + 1;
            memset(slot, 0, sizeof(*slot));
            slot->live = 1;
            slot->transfer_id = transfer_id;
            slot->kind = HIDDEN_TRANSFER_H2D;
            slot->allocation_id = remote.allocation_id;
            slot->origin_stream_slot = sv.slot_index;
            slot->origin_stream_seq = seq;
            slot->submit_order = ++g_transfer_submit_order;
            slot->host_dst = NULL;
            slot->bytes = chunk;

            (*sv.next_transfer_seq) = seq;
            sv.frontier[sv.slot_index] = seq;

            printf("G6E_CUDA_MEMCPY_ASYNC kind=H2D transfer_id=%llu stream_id=%llu stream_slot=%zu seq=%llu allocation_id=%llu offset=%llu bytes=%zu hidden=YES\n",
                   (unsigned long long)transfer_id,
                   (unsigned long long)sv.stream_id,
                   sv.slot_index,
                   (unsigned long long)seq,
                   (unsigned long long)remote.allocation_id,
                   (unsigned long long)(remote.byte_offset + done),
                   chunk);

            done += chunk;
        }

        return cudaSuccess;
    }

    if (kind == cudaMemcpyDeviceToHost) {
        rr = resolve_remote_ptr(src, count, &remote);
        if (rr != RESOLVE_OK)
            G6E_RETURN(map_resolve_error(rr));

        unsigned char *host = (unsigned char *)dst;
        size_t done = 0;

        while (done < count) {
            size_t chunk = count - done;
            if (chunk > (size_t)G6A_COPY_CHUNK)
                chunk = (size_t)G6A_COPY_CHUNK;

            HiddenTransfer *slot = find_free_hidden_transfer();
            if (!slot)
                G6E_RETURN(cudaErrorMemoryAllocation);

            uint64_t transfer_id = 0;
            if (remote_d2h_async_submit(
                    remote.allocation_id,
                    remote.byte_offset + done,
                    sv.stream_id,
                    chunk,
                    &transfer_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

            uint64_t seq = (*sv.next_transfer_seq) + 1;
            memset(slot, 0, sizeof(*slot));
            slot->live = 1;
            slot->transfer_id = transfer_id;
            slot->kind = HIDDEN_TRANSFER_D2H;
            slot->allocation_id = remote.allocation_id;
            slot->origin_stream_slot = sv.slot_index;
            slot->origin_stream_seq = seq;
            slot->submit_order = ++g_transfer_submit_order;
            slot->host_dst = host + done;
            slot->bytes = chunk;

            (*sv.next_transfer_seq) = seq;
            sv.frontier[sv.slot_index] = seq;

            printf("G6E_CUDA_MEMCPY_ASYNC kind=D2H transfer_id=%llu stream_id=%llu stream_slot=%zu seq=%llu allocation_id=%llu offset=%llu bytes=%zu host_dst=%p hidden=YES\n",
                   (unsigned long long)transfer_id,
                   (unsigned long long)sv.stream_id,
                   sv.slot_index,
                   (unsigned long long)seq,
                   (unsigned long long)remote.allocation_id,
                   (unsigned long long)(remote.byte_offset + done),
                   chunk,
                   (void *)(host + done));

            done += chunk;
        }

        return cudaSuccess;
    }

    if (kind == cudaMemcpyHostToHost ||
        kind == cudaMemcpyDeviceToDevice ||
        kind == cudaMemcpyDefault)
        G6E_RETURN(cudaErrorNotSupported);

    G6E_RETURN(cudaErrorInvalidMemcpyDirection);
}

static cudaError_t cudaDeviceSynchronize_locked(void)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (remote_sync() != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    cudaError_t drain_err = drain_all_transfers_blocking();
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);

    printf("G6E_CUDA_DEVICE_SYNCHRONIZE hidden_transfers=0 result=PASS\n");
    return cudaSuccess;
}

static cudaError_t cudaStreamCreate_locked(cudaStream_t *pStream)
{
    if (!pStream)
        G6E_RETURN(cudaErrorInvalidValue);
    *pStream = NULL;

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (g_stream_handle_next >= G6C_MAX_STREAM_HANDLES)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uint64_t *frontier =
        (uint64_t *)calloc(G6E_FRONTIER_SLOTS, sizeof(uint64_t));
    if (!frontier)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uint64_t stream_id = 0;
    if (remote_create_stream(&stream_id) != 0) {
        free(frontier);
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));
    }

    struct corexCudaStreamHandle *h = &g_stream_handles[g_stream_handle_next];
    memset(h, 0, sizeof(*h));
    h->cookie = G6C_STREAM_COOKIE;
    h->stream_id = stream_id;
    h->live = 1;
    h->slot_index = g_stream_handle_next;
    h->next_transfer_seq = 0;
    h->frontier = frontier;
    g_stream_handle_next++;
    *pStream = (cudaStream_t)h;

    printf("G6C_CUDA_STREAM_CREATE handle=%p stream_id=%llu result=PASS\n",
           (void *)*pStream,
           (unsigned long long)stream_id);
    return cudaSuccess;
}

static cudaError_t cudaStreamDestroy_locked(cudaStream_t stream)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaStreamHandle *h = resolve_stream_handle(stream);
    if (!h)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    uint64_t stream_id = h->stream_id;

    /*
     * Gate 6C V1 keeps destruction conservative: establish completion and
     * host visibility before destroying the remote StreamID. This avoids a
     * local-live/remote-dead split if transfer retirement fails.
     */
    if (remote_stream_sync(stream_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    cudaError_t drain_err = drain_frontier_blocking(h->frontier);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);

    if (remote_destroy_stream(stream_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6C_CUDA_STREAM_DESTROY handle=%p stream_id=%llu hidden_transfers_drained=YES result=PASS tombstone=YES\n",
           (void *)stream,
           (unsigned long long)stream_id);

    h->live = 0;
    h->stream_id = 0;
    free(h->frontier);
    h->frontier = NULL;
    return cudaSuccess;
}

static cudaError_t cudaStreamSynchronize_locked(cudaStream_t stream)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (stream == NULL) {
        /* Correctness-first V1: context-wide barrier for legacy default stream. */
        if (remote_sync() != 0)
            G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));
        cudaError_t err = drain_all_transfers_blocking();
        if (err != cudaSuccess)
            G6E_RETURN(err);
        printf("G6E_CUDA_STREAM_SYNC handle=NULL stream_id=0 mode=LEGACY_CONTEXT_BARRIER host_visible=YES result=PASS\n");
        return cudaSuccess;
    }

    struct corexCudaStreamHandle *h = resolve_stream_handle(stream);
    if (!h)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    if (remote_stream_sync(h->stream_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    cudaError_t drain_err = drain_frontier_blocking(h->frontier);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);

    printf("G6E_CUDA_STREAM_SYNC handle=%p stream_id=%llu host_visible=YES result=PASS\n",
           (void *)stream,
           (unsigned long long)h->stream_id);
    return cudaSuccess;
}

static cudaError_t cudaStreamQuery_locked(cudaStream_t stream)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        G6E_RETURN(cudaErrorInvalidResourceHandle);
    apply_legacy_default_ordering(&sv);

    uint32_t state = STREAM_STATE_READY;
    if (remote_stream_query(sv.stream_id, &state) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6E_CUDA_STREAM_QUERY handle=%p stream_id=%llu state=%s\n",
           (void *)stream,
           (unsigned long long)sv.stream_id,
           state == STREAM_STATE_READY ? "READY" : "PENDING");

    if (state == STREAM_STATE_PENDING)
        G6E_RETURN(cudaErrorNotReady);
    if (state != STREAM_STATE_READY)
        G6E_RETURN(cudaErrorUnknown);

    int all_ready = 0;
    cudaError_t drain_err =
        drain_frontier_if_ready(sv.frontier, &all_ready);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);
    if (!all_ready)
        G6E_RETURN(cudaErrorNotReady);

    return cudaSuccess;
}

static cudaError_t cudaEventCreate_locked(cudaEvent_t *event)
{
    if (!event)
        G6E_RETURN(cudaErrorInvalidValue);
    *event = NULL;

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (g_event_handle_next >= G6C_MAX_EVENT_HANDLES)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uint64_t *frontier =
        (uint64_t *)calloc(G6E_FRONTIER_SLOTS, sizeof(uint64_t));
    if (!frontier)
        G6E_RETURN(cudaErrorMemoryAllocation);

    uint64_t event_id = 0;
    if (remote_create_event(&event_id) != 0) {
        free(frontier);
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));
    }

    struct corexCudaEventHandle *h = &g_event_handles[g_event_handle_next++];
    memset(h, 0, sizeof(*h));
    h->cookie = G6C_EVENT_COOKIE;
    h->event_id = event_id;
    h->live = 1;
    h->recorded = 0;
    h->frontier = frontier;
    *event = (cudaEvent_t)h;

    printf("G6C_CUDA_EVENT_CREATE handle=%p event_id=%llu result=PASS\n",
           (void *)*event,
           (unsigned long long)event_id);
    return cudaSuccess;
}

static cudaError_t cudaEventDestroy_locked(cudaEvent_t event)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaEventHandle *h = resolve_event_handle(event);
    if (!h)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    uint64_t event_id = h->event_id;

    if (h->recorded) {
        if (remote_event_sync(event_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

        cudaError_t drain_err = drain_frontier_blocking(h->frontier);
        if (drain_err != cudaSuccess)
            G6E_RETURN(drain_err);
    }

    if (remote_destroy_event(event_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6C_CUDA_EVENT_DESTROY handle=%p event_id=%llu covered_transfers_drained=%s result=PASS tombstone=YES\n",
           (void *)event,
           (unsigned long long)event_id,
           h->recorded ? "YES" : "N/A");

    h->live = 0;
    h->event_id = 0;
    h->recorded = 0;
    free(h->frontier);
    h->frontier = NULL;
    return cudaSuccess;
}

static cudaError_t cudaEventRecord_locked(cudaEvent_t event, cudaStream_t stream)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaEventHandle *eh = resolve_event_handle(event);
    if (!eh)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        G6E_RETURN(cudaErrorInvalidResourceHandle);
    apply_legacy_default_ordering(&sv);

    if (remote_event_record(eh->event_id, sv.stream_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    memcpy(eh->frontier,
           sv.frontier,
           G6E_FRONTIER_SLOTS * sizeof(uint64_t));
    eh->recorded = 1;

    printf("G6E_CUDA_EVENT_RECORD event_handle=%p event_id=%llu stream_handle=%p stream_id=%llu frontier_snapshot=YES result=PASS\n",
           (void *)event,
           (unsigned long long)eh->event_id,
           (void *)stream,
           (unsigned long long)sv.stream_id);
    return cudaSuccess;
}

static cudaError_t cudaEventSynchronize_locked(cudaEvent_t event)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaEventHandle *h = resolve_event_handle(event);
    if (!h)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    if (remote_event_sync(h->event_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    cudaError_t drain_err = drain_frontier_blocking(h->frontier);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);

    printf("G6E_CUDA_EVENT_SYNC handle=%p event_id=%llu host_visible=YES result=PASS\n",
           (void *)event,
           (unsigned long long)h->event_id);
    return cudaSuccess;
}

static cudaError_t cudaEventQuery_locked(cudaEvent_t event)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaEventHandle *h = resolve_event_handle(event);
    if (!h)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    uint32_t state = STREAM_STATE_READY;
    if (remote_event_query(h->event_id, &state) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    printf("G6E_CUDA_EVENT_QUERY handle=%p event_id=%llu state=%s\n",
           (void *)event,
           (unsigned long long)h->event_id,
           state == STREAM_STATE_READY ? "READY" : "PENDING");

    if (state == STREAM_STATE_PENDING)
        G6E_RETURN(cudaErrorNotReady);
    if (state != STREAM_STATE_READY)
        G6E_RETURN(cudaErrorUnknown);

    int all_ready = 0;
    cudaError_t drain_err =
        drain_frontier_if_ready(h->frontier, &all_ready);
    if (drain_err != cudaSuccess)
        G6E_RETURN(drain_err);
    if (!all_ready)
        G6E_RETURN(cudaErrorNotReady);

    return cudaSuccess;
}

static cudaError_t cudaStreamWaitEvent_locked(
    cudaStream_t stream,
    cudaEvent_t event,
    unsigned int flags)
{
    if (flags != 0)
        G6E_RETURN(cudaErrorInvalidValue);

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexCudaEventHandle *eh = resolve_event_handle(event);
    if (!eh)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        G6E_RETURN(cudaErrorInvalidResourceHandle);
    apply_legacy_default_ordering(&sv);

    if (remote_stream_wait_event(sv.stream_id, eh->event_id) != 0)
        G6E_RETURN(map_last_rpc_error(cudaErrorUnknown));

    if (eh->recorded)
        frontier_merge(sv.frontier, eh->frontier);

    printf("G6E_CUDA_STREAM_WAIT_EVENT stream_handle=%p stream_id=%llu event_handle=%p event_id=%llu causal_frontier_merged=%s result=PASS\n",
           (void *)stream,
           (unsigned long long)sv.stream_id,
           (void *)event,
           (unsigned long long)eh->event_id,
           eh->recorded ? "YES" : "NO");
    return cudaSuccess;
}


static cudaError_t corexRemoteModuleLoad_locked(
    const char *cubin_path,
    corexRemoteModule_t *module_out)
{
    if (!cubin_path || !module_out)
        G6E_RETURN(cudaErrorInvalidValue);
    *module_out = NULL;

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    if (g_module_handle_next >= G6D_MAX_MODULE_HANDLES)
        G6E_RETURN(cudaErrorMemoryAllocation);

    size_t image_size = 0;
    unsigned char *image = read_file_bytes(cubin_path, &image_size);
    if (!image)
        G6E_RETURN(cudaErrorUnknown);

    const char *base = strrchr(cubin_path, '/');
    base = base ? base + 1 : cubin_path;

    cudaError_t result =
        module_load_bytes_after_runtime(
            base,
            image,
            image_size,
            module_out);

    free(image);

    if (result != cudaSuccess)
        G6E_RETURN(result);

    struct corexRemoteModuleHandle *h =
        resolve_module_handle(*module_out);
    if (!h)
        G6E_RETURN(cudaErrorUnknown);

    printf("G6D_MODULE_LOAD handle=%p module_id=%llu path=%s result=PASS\n",
           (void *)*module_out,
           (unsigned long long)h->module_id,
           cubin_path);
    return cudaSuccess;
}

static cudaError_t corexRemoteModuleUnload_locked(corexRemoteModule_t module)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    G6E_RETURN(module_unload_after_runtime(module));
}


static cudaError_t bind_kernel_identity_after_runtime(
    struct corexRemoteModuleHandle *mh,
    const char *kernel_name,
    G7DKernelOrigin origin,
    const void *host_fun,
    uint64_t registration_generation,
    corexRemoteKernelHandle **kernel_out)
{
    if (!mh || !kernel_name || !kernel_out)
        return cudaErrorInvalidValue;

    *kernel_out = NULL;

    size_t name_len = strlen(kernel_name);
    if (name_len == 0 || name_len >= sizeof(g_kernel_handles[0].name))
        return cudaErrorInvalidValue;

    if (origin == G7D_KERNEL_ORIGIN_COMPILER) {
        if (!host_fun)
            return cudaErrorInvalidValue;

        corexRemoteKernelHandle *existing =
            find_live_compiler_kernel_by_host_fun(host_fun);

        if (existing) {
            if (existing->module_id == mh->module_id &&
                strcmp(existing->name, kernel_name) == 0 &&
                existing->registration_generation == registration_generation) {
                *kernel_out = existing;
                printf("G7D_FUNCTION_BIND_IDEMPOTENT host_fun=%p kernel_id=%llu module_id=%llu generation=%llu name=%s abi=%s\n",
                       host_fun,
                       (unsigned long long)existing->kernel_id,
                       (unsigned long long)existing->module_id,
                       (unsigned long long)existing->registration_generation,
                       existing->name,
                       existing->abi_state == G7D_ABI_READY ? "READY" : "UNKNOWN");
                return cudaSuccess;
            }

            fprintf(stderr,
                    "G7D_FUNCTION_BIND_CONFLICT host_fun=%p existing_module_id=%llu existing_generation=%llu existing_name=%s requested_module_id=%llu requested_generation=%llu requested_name=%s\n",
                    host_fun,
                    (unsigned long long)existing->module_id,
                    (unsigned long long)existing->registration_generation,
                    existing->name,
                    (unsigned long long)mh->module_id,
                    (unsigned long long)registration_generation,
                    kernel_name);
            return cudaErrorInvalidValue;
        }
    }

    if (g_kernel_handle_next >= G6D_MAX_KERNEL_HANDLES)
        return cudaErrorMemoryAllocation;

    uint64_t kernel_id = 0;
    if (remote_get_kernel(mh->module_id, kernel_name, &kernel_id) != 0)
        return map_last_rpc_error(cudaErrorUnknown);

    corexRemoteKernelHandle *kh = &g_kernel_handles[g_kernel_handle_next++];
    memset(kh, 0, sizeof(*kh));
    kh->cookie = G6D_KERNEL_COOKIE;
    kh->kernel_id = kernel_id;
    kh->module_id = mh->module_id;
    kh->live = 1;
    kh->origin = origin;
    kh->abi_state = G7D_ABI_UNKNOWN;
    kh->host_fun = host_fun;
    kh->registration_generation = registration_generation;
    snprintf(kh->name, sizeof(kh->name), "%s", kernel_name);

    *kernel_out = kh;
    return cudaSuccess;
}

static cudaError_t corexRemoteRegisterKernel_locked(
    corexRemoteModule_t module,
    const char *kernel_name,
    const corexRemoteKernelArgDesc *args,
    size_t argc,
    const void **func_out)
{
    if (!kernel_name || !func_out || argc > G6D_MAX_KERNEL_ARGS)
        G6E_RETURN(cudaErrorInvalidValue);
    if (argc > 0 && !args)
        G6E_RETURN(cudaErrorInvalidValue);
    *func_out = NULL;

    /*
     * Validate the explicit ABI before creating a remote KernelID so a bad
     * controlled descriptor cannot leave a half-registered client object.
     */
    for (size_t i = 0; i < argc; ++i) {
        if (args[i].kind == COREX_REMOTE_KERNEL_ARG_DEVICE_PTR) {
            if (args[i].size != sizeof(void *))
                G6E_RETURN(cudaErrorInvalidValue);
        } else if (args[i].kind == COREX_REMOTE_KERNEL_ARG_BY_VALUE) {
            if (args[i].size == 0)
                G6E_RETURN(cudaErrorInvalidValue);
        } else {
            G6E_RETURN(cudaErrorInvalidValue);
        }
    }

    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    struct corexRemoteModuleHandle *mh = resolve_module_handle(module);
    if (!mh)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    corexRemoteKernelHandle *kh = NULL;
    cudaError_t bind_rc = bind_kernel_identity_after_runtime(
        mh,
        kernel_name,
        G7D_KERNEL_ORIGIN_CONTROLLED,
        NULL,
        0,
        &kh);
    if (bind_rc != cudaSuccess)
        G6E_RETURN(bind_rc);

    kh->argc = argc;
    if (argc > 0)
        memcpy(kh->args, args, argc * sizeof(args[0]));
    kh->abi_state = G7D_ABI_READY;

    *func_out = (const void *)kh;
    printf("G6D_KERNEL_REGISTER token=%p kernel_id=%llu module_id=%llu name=%s argc=%zu abi=READY origin=CONTROLLED result=PASS\n",
           *func_out,
           (unsigned long long)kh->kernel_id,
           (unsigned long long)kh->module_id,
           kh->name,
           argc);
    return cudaSuccess;
}


static const char *g7e_abi_state_name(G7DAbiState state)
{
    switch (state) {
    case G7D_ABI_UNKNOWN:
        return "UNKNOWN";
    case G7D_ABI_READY:
        return "READY";
    case G7D_ABI_FAILED:
        return "FAILED";
    default:
        return "INVALID";
    }
}

static cudaError_t g7e_attach_compiler_abi_from_metadata(
    const G7CFatbinRegistration *reg,
    corexRemoteKernelHandle *kh,
    const char *device_name)
{
    if (!reg || !kh || !device_name)
        return cudaErrorInvalidValue;

    if (!reg->image_ptr || reg->image_size == 0) {
        kh->abi_state = G7D_ABI_FAILED;
        return cudaErrorInvalidValue;
    }

    if (kh->abi_state == G7D_ABI_READY) {
        printf("G7E_ABI_ATTACH_IDEMPOTENT kernel_id=%llu name=%s argc=%zu abi=READY\n",
               (unsigned long long)kh->kernel_id,
               kh->name,
               kh->argc);
        return cudaSuccess;
    }

    CorexModuleMeta *module_meta =
        (CorexModuleMeta *)calloc(1, sizeof(*module_meta));
    if (!module_meta) {
        kh->abi_state = G7D_ABI_FAILED;
        return cudaErrorMemoryAllocation;
    }

    char parse_error[384];
    int parse_rc = corex_parse_metadata(
        reg->image_ptr,
        reg->image_size,
        module_meta,
        parse_error,
        sizeof(parse_error));

    if (parse_rc != 0) {
        fprintf(stderr,
                "G7E_METADATA_PARSE_FAIL generation=%llu module_id=%llu kernel_id=%llu name=%s error=%s\n",
                (unsigned long long)reg->generation,
                (unsigned long long)kh->module_id,
                (unsigned long long)kh->kernel_id,
                device_name,
                parse_error);
        kh->abi_state = G7D_ABI_FAILED;
        free(module_meta);
        return cudaErrorInvalidValue;
    }

    const CorexKernelMeta *meta =
        corex_find_kernel_meta(module_meta, device_name);
    if (!meta) {
        fprintf(stderr,
                "G7E_METADATA_KERNEL_NOT_FOUND generation=%llu module_id=%llu kernel_id=%llu name=%s\n",
                (unsigned long long)reg->generation,
                (unsigned long long)kh->module_id,
                (unsigned long long)kh->kernel_id,
                device_name);
        kh->abi_state = G7D_ABI_FAILED;
        free(module_meta);
        return cudaErrorInvalidValue;
    }

    if (meta->argc > G6D_MAX_KERNEL_ARGS) {
        fprintf(stderr,
                "G7E_ABI_NORMALIZE_FAIL kernel_id=%llu name=%s reason=ARGC argc=%u max=%u\n",
                (unsigned long long)kh->kernel_id,
                device_name,
                meta->argc,
                (unsigned)G6D_MAX_KERNEL_ARGS);
        kh->abi_state = G7D_ABI_FAILED;
        free(module_meta);
        return cudaErrorNotSupported;
    }

    corexRemoteKernelArgDesc normalized[G6D_MAX_KERNEL_ARGS];
    memset(normalized, 0, sizeof(normalized));

    uint64_t prior_end = 0;
    for (uint32_t i = 0; i < meta->argc; ++i) {
        const CorexArgMeta *arg = &meta->args[i];

        if (arg->size == 0) {
            fprintf(stderr,
                    "G7E_ABI_NORMALIZE_FAIL kernel_id=%llu name=%s arg=%u reason=ZERO_SIZE\n",
                    (unsigned long long)kh->kernel_id,
                    device_name,
                    i);
            kh->abi_state = G7D_ABI_FAILED;
            free(module_meta);
            return cudaErrorNotSupported;
        }

        uint64_t arg_end = (uint64_t)arg->offset + (uint64_t)arg->size;
        if (arg_end < arg->offset ||
            arg_end > meta->kernarg_segment_size ||
            (i > 0 && (uint64_t)arg->offset < prior_end)) {
            fprintf(stderr,
                    "G7E_ABI_NORMALIZE_FAIL kernel_id=%llu name=%s arg=%u reason=LAYOUT offset=%u size=%u kernarg=%u prior_end=%llu\n",
                    (unsigned long long)kh->kernel_id,
                    device_name,
                    i,
                    arg->offset,
                    arg->size,
                    meta->kernarg_segment_size,
                    (unsigned long long)prior_end);
            kh->abi_state = G7D_ABI_FAILED;
            free(module_meta);
            return cudaErrorNotSupported;
        }
        prior_end = arg_end;

        if (arg->kind == COREX_META_ARG_GLOBAL_BUFFER) {
            if (arg->size != sizeof(void *) ||
                strcmp(arg->address_space, "global") != 0) {
                fprintf(stderr,
                        "G7E_ABI_NORMALIZE_FAIL kernel_id=%llu name=%s arg=%u reason=GLOBAL_BUFFER_SHAPE size=%u address_space=%s\n",
                        (unsigned long long)kh->kernel_id,
                        device_name,
                        i,
                        arg->size,
                        arg->address_space);
                kh->abi_state = G7D_ABI_FAILED;
                free(module_meta);
                return cudaErrorNotSupported;
            }

            normalized[i].kind =
                COREX_REMOTE_KERNEL_ARG_DEVICE_PTR;
            normalized[i].size = (uint32_t)sizeof(void *);
        } else if (arg->kind == COREX_META_ARG_BY_VALUE) {
            normalized[i].kind =
                COREX_REMOTE_KERNEL_ARG_BY_VALUE;
            normalized[i].size = arg->size;
        } else {
            fprintf(stderr,
                    "G7E_ABI_NORMALIZE_FAIL kernel_id=%llu name=%s arg=%u reason=UNKNOWN_KIND value_kind=%s\n",
                    (unsigned long long)kh->kernel_id,
                    device_name,
                    i,
                    arg->value_kind);
            kh->abi_state = G7D_ABI_FAILED;
            free(module_meta);
            return cudaErrorNotSupported;
        }

        printf("G7E_ABI_ARG kernel_id=%llu name=%s arg=%u meta_kind=%s offset=%u size=%u normalized=%s\n",
               (unsigned long long)kh->kernel_id,
               device_name,
               i,
               corex_meta_arg_kind_name(arg->kind),
               arg->offset,
               arg->size,
               normalized[i].kind == COREX_REMOTE_KERNEL_ARG_DEVICE_PTR
                   ? "DEVICE_PTR"
                   : "BY_VALUE");
    }

    kh->argc = meta->argc;
    if (kh->argc > 0)
        memcpy(kh->args, normalized, kh->argc * sizeof(normalized[0]));
    kh->abi_state = G7D_ABI_READY;

    printf("G7E_ABI_ATTACH generation=%llu module_id=%llu kernel_id=%llu name=%s argc=%zu kernarg_size=%u kernarg_align=%u abi=READY result=PASS\n",
           (unsigned long long)reg->generation,
           (unsigned long long)kh->module_id,
           (unsigned long long)kh->kernel_id,
           device_name,
           kh->argc,
           meta->kernarg_segment_size,
           meta->kernarg_segment_align);

    free(module_meta);
    return cudaSuccess;
}

/*
 * Gate 7E compiler registration hooks.
 *
 * Gate 7D identity binding is preserved:
 *     hostFun + deviceName -> existing GET_KERNEL -> existing KernelID.
 *
 * Gate 7E then parses the exact Gate-7C extracted ELF locally with the canonical
 * CoreX metadata parser and attaches normalized DEVICE_PTR/BY_VALUE descriptors
 * to the same existing kernel handle.
 */
static void **__cudaRegisterFatBinary_locked(void *fatCubin)
{
    if (g_g7c_registration_next >= G7C_MAX_FATBIN_REGISTRATIONS) {
        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_FAIL stage=ALLOC error=NO_REGISTRATION_SLOT\n");
        return NULL;
    }

    G7CFatbinRegistration *reg =
        &g_g7c_registrations[g_g7c_registration_next++];
    memset(reg, 0, sizeof(*reg));

    reg->cookie = G7C_REG_COOKIE;
    reg->generation = g_g7c_generation_next++;
    reg->state = G7C_REG_REGISTERING;
    reg->compiler_wrapper = fatCubin;
    reg->registration_error = cudaSuccess;

    printf("G7C_REGISTER_FATBINARY handle=%p generation=%llu wrapper=%p\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           fatCubin);

    CorexLoadableImage image;
    char extraction_error[384];

    int extraction_rc =
        corex_image_from_compiler_wrapper(
            fatCubin,
            &image,
            extraction_error,
            sizeof(extraction_error));

    if (extraction_rc != 0) {
        reg->state = G7C_REG_FAILED;
        reg->registration_error = cudaErrorInvalidValue;
        reg->extraction_error = extraction_rc;

        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_FAIL handle=%p generation=%llu stage=EXTRACT rc=%d error=%s\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                extraction_rc,
                extraction_error);
        return (void **)reg;
    }

    reg->fatbin_ptr = image.fatbin_bytes;
    reg->fatbin_size = image.fatbin_size;
    reg->image_ptr = image.image_bytes;
    reg->image_size = image.image_size;
    reg->image_offset = image.image_offset;

    printf("G7C_FATBIN_SPAN handle=%p generation=%llu bytes=%zu object=%s\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           reg->fatbin_size,
           image.object_name);

    printf("G7C_IMAGE_EXTRACT handle=%p generation=%llu offset=%zu bytes=%zu result=PASS\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           reg->image_offset,
           reg->image_size);

    char wire_name[96];
    int name_len =
        snprintf(
            wire_name,
            sizeof(wire_name),
            "g7c-auto-%llu.cubin",
            (unsigned long long)reg->generation);

    if (name_len <= 0 || (size_t)name_len >= sizeof(wire_name)) {
        reg->state = G7C_REG_FAILED;
        reg->registration_error = cudaErrorInvalidValue;

        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_FAIL handle=%p generation=%llu stage=NAME\n",
                (void *)reg,
                (unsigned long long)reg->generation);
        return (void **)reg;
    }

    cudaError_t load_result =
        g7c_hidden_module_load_bytes(
            wire_name,
            reg->image_ptr,
            reg->image_size,
            &reg->remote_module);

    if (load_result != cudaSuccess) {
        reg->state = G7C_REG_FAILED;
        reg->registration_error = load_result;

        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_FAIL handle=%p generation=%llu stage=UPLOAD cuda_error=%d(%s)\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                (int)load_result,
                cudaGetErrorString(load_result));
        return (void **)reg;
    }

    struct corexRemoteModuleHandle *module_handle =
        resolve_module_handle(reg->remote_module);
    if (!module_handle) {
        reg->state = G7C_REG_FAILED;
        reg->registration_error = cudaErrorUnknown;

        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_FAIL handle=%p generation=%llu stage=MODULE_HANDLE\n",
                (void *)reg,
                (unsigned long long)reg->generation);
        return (void **)reg;
    }

    reg->module_id_snapshot = module_handle->module_id;
    reg->state = G7C_REG_REMOTE_READY;

    printf("G7C_MODULE_AUTO_LOAD handle=%p generation=%llu module_handle=%p module_id=%llu bytes=%zu result=PASS\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           (void *)reg->remote_module,
           (unsigned long long)reg->module_id_snapshot,
           reg->image_size);

    return (void **)reg;
}

static void __cudaRegisterFunction_locked(
    void **fatCubinHandle,
    const char *hostFun,
    char *deviceFun,
    const char *deviceName,
    int thread_limit,
    void *tid,
    void *bid,
    void *bDim,
    void *gDim,
    int *wSize)
{
    (void)deviceFun;
    (void)thread_limit;
    (void)tid;
    (void)bid;
    (void)bDim;
    (void)gDim;
    (void)wSize;

    G7CFatbinRegistration *reg =
        g7c_resolve_registration(fatCubinHandle);

    if (!reg) {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_REJECT reason=INVALID_PARENT handle=%p host_fun=%p name=%s\n",
                (void *)fatCubinHandle,
                (const void *)hostFun,
                deviceName ? deviceName : "<null>");
        return;
    }

    reg->function_registration_calls++;

    if (reg->state == G7C_REG_FAILED ||
        reg->state == G7C_REG_UNLOAD_FAILED ||
        reg->state == G7C_REG_DEAD ||
        !reg->remote_module) {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_REJECT reason=PARENT_NOT_READY handle=%p generation=%llu state=%s host_fun=%p name=%s count=%u\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                g7c_registration_state_name(reg->state),
                (const void *)hostFun,
                deviceName ? deviceName : "<null>",
                reg->function_registration_calls);
        return;
    }

    if (reg->state != G7C_REG_REMOTE_READY &&
        reg->state != G7C_REG_LIVE) {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_REJECT reason=PARENT_STATE handle=%p generation=%llu state=%s host_fun=%p name=%s count=%u\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                g7c_registration_state_name(reg->state),
                (const void *)hostFun,
                deviceName ? deviceName : "<null>",
                reg->function_registration_calls);
        return;
    }

    if (!hostFun || !deviceName || deviceName[0] == '\0') {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_REJECT reason=INVALID_IDENTITY handle=%p generation=%llu host_fun=%p name=%s\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                (const void *)hostFun,
                deviceName ? deviceName : "<null>");
        return;
    }

    struct corexRemoteModuleHandle *mh =
        resolve_module_handle(reg->remote_module);
    if (!mh) {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_REJECT reason=MODULE_HANDLE handle=%p generation=%llu host_fun=%p name=%s\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                (const void *)hostFun,
                deviceName);
        return;
    }

    /*
     * Registration occurs in a compiler-generated static constructor. Preserve
     * the user's TLS Runtime error state exactly like Gate 7C module auto-load.
     */
    G7CHiddenErrorSnapshot snapshot =
        g7c_snapshot_hidden_error_state();

    cudaError_t init = ensure_runtime();
    corexRemoteKernelHandle *kh = NULL;
    cudaError_t bind_rc = init;

    if (init == cudaSuccess) {
        bind_rc = bind_kernel_identity_after_runtime(
            mh,
            deviceName,
            G7D_KERNEL_ORIGIN_COMPILER,
            (const void *)hostFun,
            reg->generation,
            &kh);
    }

    g7c_restore_hidden_error_state(snapshot);

    if (bind_rc != cudaSuccess || !kh) {
        fprintf(stderr,
                "G7D_REGISTER_FUNCTION_FAIL handle=%p generation=%llu module_id=%llu host_fun=%p name=%s rc=%d(%s)\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                (unsigned long long)reg->module_id_snapshot,
                (const void *)hostFun,
                deviceName,
                (int)bind_rc,
                cudaGetErrorString(bind_rc));
        return;
    }

    printf("G7D_FUNCTION_BIND handle=%p generation=%llu module_id=%llu host_fun=%p kernel_handle=%p kernel_id=%llu name=%s abi=%s result=PASS\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           (unsigned long long)kh->module_id,
           (const void *)hostFun,
           (void *)kh,
           (unsigned long long)kh->kernel_id,
           kh->name,
           g7e_abi_state_name(kh->abi_state));

    cudaError_t abi_rc =
        g7e_attach_compiler_abi_from_metadata(reg, kh, deviceName);

    if (abi_rc != cudaSuccess) {
        fprintf(stderr,
                "G7E_REGISTER_FUNCTION_ABI_FAIL handle=%p generation=%llu module_id=%llu host_fun=%p kernel_id=%llu name=%s rc=%d(%s) abi=%s\n",
                (void *)reg,
                (unsigned long long)reg->generation,
                (unsigned long long)kh->module_id,
                (const void *)hostFun,
                (unsigned long long)kh->kernel_id,
                kh->name,
                (int)abi_rc,
                cudaGetErrorString(abi_rc),
                g7e_abi_state_name(kh->abi_state));
        return;
    }
}

static void __cudaRegisterFatBinaryEnd_locked(void **fatCubinHandle)
{
    G7CFatbinRegistration *reg =
        g7c_resolve_registration(fatCubinHandle);

    if (!reg) {
        fprintf(stderr,
                "G7C_REGISTER_FATBINARY_END invalid_handle=%p\n",
                (void *)fatCubinHandle);
        return;
    }

    if (reg->state == G7C_REG_REMOTE_READY)
        reg->state = G7C_REG_LIVE;

    printf("G7C_REGISTER_FATBINARY_END handle=%p generation=%llu state=%s module_id=%llu function_registrations=%u\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           g7c_registration_state_name(reg->state),
           (unsigned long long)reg->module_id_snapshot,
           reg->function_registration_calls);
}

static void __cudaUnregisterFatBinary_locked(void **fatCubinHandle)
{
    G7CFatbinRegistration *reg =
        g7c_resolve_registration(fatCubinHandle);

    if (!reg) {
        fprintf(stderr,
                "G7C_UNREGISTER_FATBINARY invalid_handle=%p\n",
                (void *)fatCubinHandle);
        return;
    }

    printf("G7C_UNREGISTER_FATBINARY handle=%p generation=%llu module_id=%llu state=%s\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           (unsigned long long)reg->module_id_snapshot,
           g7c_registration_state_name(reg->state));

    if ((reg->state == G7C_REG_LIVE ||
         reg->state == G7C_REG_REMOTE_READY ||
         reg->state == G7C_REG_UNLOAD_FAILED) &&
        reg->remote_module) {

        cudaError_t unload_result =
            g7c_hidden_module_unload(reg->remote_module);

        if (unload_result == cudaSuccess) {
            printf("G7C_MODULE_AUTO_UNLOAD handle=%p generation=%llu module_id=%llu result=PASS\n",
                   (void *)reg,
                   (unsigned long long)reg->generation,
                   (unsigned long long)reg->module_id_snapshot);

            reg->remote_module = NULL;
            reg->state = G7C_REG_DEAD;
        } else {
            reg->registration_error = unload_result;
            reg->state = G7C_REG_UNLOAD_FAILED;

            fprintf(stderr,
                    "G7C_MODULE_AUTO_UNLOAD_FAIL handle=%p generation=%llu module_id=%llu cuda_error=%d(%s)\n",
                    (void *)reg,
                    (unsigned long long)reg->generation,
                    (unsigned long long)reg->module_id_snapshot,
                    (int)unload_result,
                    cudaGetErrorString(unload_result));
        }
    } else if (reg->state == G7C_REG_FAILED) {
        reg->state = G7C_REG_DEAD;
    }

    printf("G7C_REGISTRATION_TOMBSTONE handle=%p generation=%llu state=%s\n",
           (void *)reg,
           (unsigned long long)reg->generation,
           g7c_registration_state_name(reg->state));
}

static void g7c_cleanup_registrations_before_runtime_shutdown(void)
{
    for (size_t i = 0; i < g_g7c_registration_next; ++i) {
        G7CFatbinRegistration *reg = &g_g7c_registrations[i];

        if (reg->cookie != G7C_REG_COOKIE)
            continue;

        if ((reg->state == G7C_REG_LIVE ||
             reg->state == G7C_REG_REMOTE_READY ||
             reg->state == G7C_REG_UNLOAD_FAILED) &&
            reg->remote_module) {

            cudaError_t unload_result =
                g7c_hidden_module_unload(reg->remote_module);

            if (unload_result == cudaSuccess) {
                printf("G7C_SHUTDOWN_AUTO_UNLOAD handle=%p generation=%llu module_id=%llu result=PASS\n",
                       (void *)reg,
                       (unsigned long long)reg->generation,
                       (unsigned long long)reg->module_id_snapshot);

                reg->remote_module = NULL;
                reg->state = G7C_REG_DEAD;
            } else {
                reg->registration_error = unload_result;
                reg->state = G7C_REG_UNLOAD_FAILED;

                fprintf(stderr,
                        "G7C_SHUTDOWN_AUTO_UNLOAD_FAIL handle=%p generation=%llu module_id=%llu cuda_error=%d(%s)\n",
                        (void *)reg,
                        (unsigned long long)reg->generation,
                        (unsigned long long)reg->module_id_snapshot,
                        (int)unload_result,
                        cudaGetErrorString(unload_result));
            }
        } else if (reg->state == G7C_REG_FAILED) {
            reg->state = G7C_REG_DEAD;
        }
    }
}

static void g7c_finalize_registrations_after_session_close(void)
{
    for (size_t i = 0; i < g_g7c_registration_next; ++i) {
        G7CFatbinRegistration *reg = &g_g7c_registrations[i];

        if (reg->cookie != G7C_REG_COOKIE)
            continue;

        if (reg->remote_module) {
            printf("G7C_REGISTRATION_SESSION_CLEANUP handle=%p generation=%llu module_id=%llu prior_state=%s\n",
                   (void *)reg,
                   (unsigned long long)reg->generation,
                   (unsigned long long)reg->module_id_snapshot,
                   g7c_registration_state_name(reg->state));

            reg->remote_module = NULL;
            reg->state = G7C_REG_DEAD;
        }
    }
}



static cudaError_t cudaLaunchKernel_locked(
    const void *func,
    dim3 gridDim,
    dim3 blockDim,
    void **args,
    size_t sharedMem,
    cudaStream_t stream)
{
    cudaError_t init = ensure_runtime();
    if (init != cudaSuccess)
        G6E_RETURN(init);

    corexRemoteKernelHandle *kh = resolve_kernel_reference(func);
    if (!kh)
        G6E_RETURN(cudaErrorInvalidResourceHandle);

    if (kh->abi_state != G7D_ABI_READY) {
        printf("G7E_LAUNCH_REJECT_ABI_NOT_READY func=%p kernel_handle=%p kernel_id=%llu module_id=%llu name=%s origin=%s abi=%s result=LOCAL_REJECT\n",
               func,
               (void *)kh,
               (unsigned long long)kh->kernel_id,
               (unsigned long long)kh->module_id,
               kh->name,
               kh->origin == G7D_KERNEL_ORIGIN_COMPILER ? "COMPILER" : "CONTROLLED",
               g7e_abi_state_name(kh->abi_state));
        G6E_RETURN(cudaErrorNotSupported);
    }

    G6EStreamView sv;
    if (get_stream_view(stream, &sv) != 0)
        G6E_RETURN(cudaErrorInvalidResourceHandle);
    apply_legacy_default_ordering(&sv);

    if (gridDim.x == 0 || gridDim.y == 0 || gridDim.z == 0 ||
        blockDim.x == 0 || blockDim.y == 0 || blockDim.z == 0 ||
        sharedMem > UINT32_MAX)
        G6E_RETURN(cudaErrorInvalidValue);
    if (kh->argc > 0 && !args)
        G6E_RETURN(cudaErrorInvalidValue);

    uint64_t total64 = 48;
    for (size_t i = 0; i < kh->argc; ++i) {
        if (!args[i])
            G6E_RETURN(cudaErrorInvalidValue);
        if (kh->args[i].kind == COREX_REMOTE_KERNEL_ARG_DEVICE_PTR)
            total64 += 24;
        else
            total64 += 8ULL + kh->args[i].size;
    }
    if (total64 > UINT32_MAX)
        G6E_RETURN(cudaErrorInvalidValue);

    uint32_t payload_len = (uint32_t)total64;
    unsigned char *payload = (unsigned char *)malloc(payload_len);
    if (!payload)
        G6E_RETURN(cudaErrorMemoryAllocation);

    size_t pos = 0;
    put_u64(payload, &pos, kh->kernel_id);
    put_u64(payload, &pos, sv.stream_id);
    put_u32(payload, &pos, gridDim.x);
    put_u32(payload, &pos, gridDim.y);
    put_u32(payload, &pos, gridDim.z);
    put_u32(payload, &pos, blockDim.x);
    put_u32(payload, &pos, blockDim.y);
    put_u32(payload, &pos, blockDim.z);
    put_u32(payload, &pos, (uint32_t)sharedMem);
    put_u32(payload, &pos, (uint32_t)kh->argc);

    cudaError_t result = cudaSuccess;
    for (size_t i = 0; i < kh->argc; ++i) {
        if (kh->args[i].kind == COREX_REMOTE_KERNEL_ARG_DEVICE_PTR) {
            void *device_ptr = NULL;
            memcpy(&device_ptr, args[i], sizeof(device_ptr));

            ResolvedRemotePtr remote;
            ResolveResult rr = resolve_remote_ptr(device_ptr, 1, &remote);
            if (rr != RESOLVE_OK) {
                result = map_resolve_error(rr);
                goto out;
            }

            put_u32(payload, &pos, ARG_REMOTE_PTR);
            put_u32(payload, &pos, 16);
            put_u64(payload, &pos, remote.allocation_id);
            put_u64(payload, &pos, remote.byte_offset);

            printf("G6D_LAUNCH_ARG arg=%zu kind=REMOTE_PTR allocation_id=%llu offset=%llu\n",
                   i,
                   (unsigned long long)remote.allocation_id,
                   (unsigned long long)remote.byte_offset);
        } else {
            uint32_t raw_size = kh->args[i].size;
            put_u32(payload, &pos, ARG_RAW_VALUE);
            put_u32(payload, &pos, raw_size);
            memcpy(payload + pos, args[i], raw_size);
            pos += raw_size;

            printf("G6D_LAUNCH_ARG arg=%zu kind=RAW_VALUE bytes=%u\n",
                   i,
                   raw_size);
        }
    }

    if (pos != payload_len) {
        result = cudaErrorUnknown;
        goto out;
    }

    if (rpc(g_fd,
            OP_LAUNCH_GENERIC,
            payload,
            payload_len,
            NULL,
            NULL) != 0) {
        result = map_last_rpc_error(cudaErrorUnknown);
        goto out;
    }

    printf("G6E_CUDA_LAUNCH_KERNEL token=%p kernel_id=%llu stream_id=%llu grid=(%u,%u,%u) block=(%u,%u,%u) shared=%zu argc=%zu submit=PASS\n",
           func,
           (unsigned long long)kh->kernel_id,
           (unsigned long long)sv.stream_id,
           gridDim.x, gridDim.y, gridDim.z,
           blockDim.x, blockDim.y, blockDim.z,
           sharedMem,
           kh->argc);

out:
    free(payload);
    return record_error(result);
}

cudaError_t cudaPeekAtLastError(void)
{
    return g_last_error;
}

cudaError_t cudaGetLastError(void)
{
    cudaError_t error = g_last_error;
    g_last_error = cudaSuccess;
    return error;
}

const char *cudaGetErrorString(cudaError_t error)
{
    switch (error) {
    case cudaSuccess:
        return "cudaSuccess";
    case cudaErrorInvalidValue:
        return "cudaErrorInvalidValue";
    case cudaErrorMemoryAllocation:
        return "cudaErrorMemoryAllocation";
    case cudaErrorInitializationError:
        return "cudaErrorInitializationError";
    case cudaErrorInvalidDevice:
        return "cudaErrorInvalidDevice";
    case cudaErrorInvalidDevicePointer:
        return "cudaErrorInvalidDevicePointer";
    case cudaErrorInvalidMemcpyDirection:
        return "cudaErrorInvalidMemcpyDirection";
    case cudaErrorInvalidResourceHandle:
        return "cudaErrorInvalidResourceHandle";
    case cudaErrorNotReady:
        return "cudaErrorNotReady";
    case cudaErrorNotSupported:
        return "cudaErrorNotSupported";
    case cudaErrorUnknown:
        return "cudaErrorUnknown";
    default:
        return "cudaErrorUnrecognized";
    }
}

static size_t corexRemoteDebugLiveTransfers_locked(void)
{
    return live_hidden_transfer_count();
}

static void corexRemoteRuntimeShutdown_locked(void)
{
    /*
     * Compiler registrations normally unregister before this atexit handler.
     * This explicit cleanup also makes a user-invoked RuntimeShutdown safe.
     */
    g7c_cleanup_registrations_before_runtime_shutdown();

    size_t live_before_close = live_hidden_transfer_count();
    if (live_before_close > 0) {
        fprintf(stderr,
                "G6C_SHUTDOWN live_hidden_transfers=%zu action=SERVER_SESSION_CLEANUP\n",
                live_before_close);
    }

    for (size_t i = 0; i < g_stream_handle_next; ++i) {
        free(g_stream_handles[i].frontier);
        g_stream_handles[i].frontier = NULL;
        g_stream_handles[i].live = 0;
        g_stream_handles[i].stream_id = 0;
    }
    for (size_t i = 0; i < g_event_handle_next; ++i) {
        free(g_event_handles[i].frontier);
        g_event_handles[i].frontier = NULL;
        g_event_handles[i].live = 0;
        g_event_handles[i].event_id = 0;
        g_event_handles[i].recorded = 0;
    }

    if (g_fd >= 0) {
        (void)rpc(g_fd, OP_CLOSE, NULL, 0, NULL, NULL);
        close(g_fd);
        g_fd = -1;
    }

    /*
     * OP_CLOSE / socket close is the final server-side session-cleanup barrier.
     * Any registration that could not individually unload is now a local
     * tombstone so a later compiler dtor cannot chase a reset module handle.
     */
    g7c_finalize_registrations_after_session_close();

    if (g_va_arena) {
        munmap(g_va_arena, (size_t)G6A_VA_ARENA_BYTES);
        g_va_arena = NULL;
        g_va_next = 0;
    }
    free(g_default_frontier);
    g_default_frontier = NULL;
    g_default_next_transfer_seq = 0;

    memset(g_allocs, 0, sizeof(g_allocs));
    memset(g_hidden_transfers, 0, sizeof(g_hidden_transfers));
    memset(g_module_handles, 0, sizeof(g_module_handles));
    memset(g_kernel_handles, 0, sizeof(g_kernel_handles));
    g_module_handle_next = 0;
    g_kernel_handle_next = 0;
    g_transfer_submit_order = 0;
}

/*
 * Public/shared entry boundaries.  The implementation bodies above use the
 * `_locked` convention and never acquire the RuntimeContext mutex themselves.
 * This keeps one non-recursive lock acquisition around each operation and the
 * complete RPC/local semantic transaction.
 */

int corexRemoteGetDeviceInfoInternal(
    uint32_t logical_device,
    CorexRemoteDeviceInfo *info_out)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    int result = corexRemoteGetDeviceInfoInternal_locked(
        logical_device,
        info_out);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaGetDeviceCount(int *count)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaGetDeviceCount_locked(count);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaGetDevice(int *device)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaGetDevice_locked(device);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaSetDevice(int device)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaSetDevice_locked(device);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaMalloc(void **devPtr, size_t size)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaMalloc_locked(devPtr, size);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaFree(void *devPtr)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaFree_locked(devPtr);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaMemcpy(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaMemcpy_locked(dst, src, count, kind);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaMemcpyAsync(
    void *dst,
    const void *src,
    size_t count,
    cudaMemcpyKind kind,
    cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaMemcpyAsync_locked(
        dst,
        src,
        count,
        kind,
        stream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaDeviceSynchronize(void)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaDeviceSynchronize_locked();
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaStreamCreate(cudaStream_t *pStream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaStreamCreate_locked(pStream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaStreamDestroy(cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaStreamDestroy_locked(stream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaStreamSynchronize(cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaStreamSynchronize_locked(stream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaStreamQuery(cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaStreamQuery_locked(stream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaEventCreate(cudaEvent_t *event)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaEventCreate_locked(event);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaEventDestroy(cudaEvent_t event)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaEventDestroy_locked(event);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaEventRecord(cudaEvent_t event, cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaEventRecord_locked(event, stream);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaEventSynchronize(cudaEvent_t event)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaEventSynchronize_locked(event);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaEventQuery(cudaEvent_t event)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaEventQuery_locked(event);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t cudaStreamWaitEvent(
    cudaStream_t stream,
    cudaEvent_t event,
    unsigned int flags)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaStreamWaitEvent_locked(
        stream,
        event,
        flags);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t corexRemoteModuleLoad(
    const char *cubin_path,
    corexRemoteModule_t *module_out)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = corexRemoteModuleLoad_locked(cubin_path, module_out);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t corexRemoteModuleUnload(corexRemoteModule_t module)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = corexRemoteModuleUnload_locked(module);
    corex_runtime_context_unlock(context);
    return result;
}

cudaError_t corexRemoteRegisterKernel(
    corexRemoteModule_t module,
    const char *kernel_name,
    const corexRemoteKernelArgDesc *args,
    size_t argc,
    const void **func_out)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = corexRemoteRegisterKernel_locked(
        module,
        kernel_name,
        args,
        argc,
        func_out);
    corex_runtime_context_unlock(context);
    return result;
}

void **__cudaRegisterFatBinary(void *fatCubin)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    void **result = __cudaRegisterFatBinary_locked(fatCubin);
    corex_runtime_context_unlock(context);
    return result;
}

void __cudaRegisterFunction(
    void **fatCubinHandle,
    const char *hostFun,
    char *deviceFun,
    const char *deviceName,
    int thread_limit,
    void *tid,
    void *bid,
    void *bDim,
    void *gDim,
    int *wSize)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    __cudaRegisterFunction_locked(
        fatCubinHandle,
        hostFun,
        deviceFun,
        deviceName,
        thread_limit,
        tid,
        bid,
        bDim,
        gDim,
        wSize);
    corex_runtime_context_unlock(context);
}

void __cudaRegisterFatBinaryEnd(void **fatCubinHandle)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    __cudaRegisterFatBinaryEnd_locked(fatCubinHandle);
    corex_runtime_context_unlock(context);
}

void __cudaUnregisterFatBinary(void **fatCubinHandle)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    __cudaUnregisterFatBinary_locked(fatCubinHandle);
    corex_runtime_context_unlock(context);
}

cudaError_t cudaLaunchKernel(
    const void *func,
    dim3 gridDim,
    dim3 blockDim,
    void **args,
    size_t sharedMem,
    cudaStream_t stream)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    cudaError_t result = cudaLaunchKernel_locked(
        func,
        gridDim,
        blockDim,
        args,
        sharedMem,
        stream);
    corex_runtime_context_unlock(context);
    return result;
}

size_t corexRemoteDebugLiveTransfers(void)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    size_t result = corexRemoteDebugLiveTransfers_locked();
    corex_runtime_context_unlock(context);
    return result;
}

void corexRemoteRuntimeShutdown(void)
{
    CorexRuntimeContext *context = corex_runtime_context_get();
    corex_runtime_context_lock(context);
    corexRemoteRuntimeShutdown_locked();
    corex_runtime_context_unlock(context);
}
