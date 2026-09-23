#ifndef COREX_PROTOCOL_H
#define COREX_PROTOCOL_H

#include <arpa/inet.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRX_PROTOCOL_MAGIC 0x43525839u /* CRX9 */
#define CRX_PROTOCOL_VERSION 3u

#define CRX_REQUEST_HEADER_WORDS 5u
#define CRX_RESPONSE_HEADER_WORDS 6u
#define CRX_REQUEST_HEADER_BYTES (CRX_REQUEST_HEADER_WORDS * sizeof(uint32_t))
#define CRX_RESPONSE_HEADER_BYTES (CRX_RESPONSE_HEADER_WORDS * sizeof(uint32_t))

typedef uint64_t CorexAllocationId;
typedef uint64_t CorexStreamId;
typedef uint64_t CorexEventId;
typedef uint64_t CorexTransferId;
typedef uint64_t CorexModuleId;
typedef uint64_t CorexKernelId;

typedef enum {
    ST_OK             = 0,
    ST_BAD_REQUEST    = 1,
    ST_NOT_FOUND      = 2,
    ST_CUDA_ERROR     = 3,
    ST_INTERNAL       = 4,
    ST_NO_RESOURCE    = 5,
    ST_ABI_MISMATCH   = 6,
    ST_METADATA_ERROR = 7,
} CorexProtocolStatus;

typedef enum {
    OP_ALLOC               = 1,
    OP_H2D                 = 2,
    OP_LAUNCH              = 3,
    OP_SYNC                = 4,
    OP_D2H                 = 5,
    OP_FREE                = 6,
    OP_CLOSE               = 7,
    OP_UPLOAD_MODULE       = 8,
    OP_GET_KERNEL          = 9,
    OP_UNLOAD_MODULE       = 10,
    OP_LAUNCH_GENERIC      = 11,
    OP_CREATE_STREAM       = 12,
    OP_DESTROY_STREAM      = 13,
    OP_STREAM_QUERY        = 14,
    OP_STREAM_SYNC         = 15,
    OP_CREATE_EVENT        = 16,
    OP_DESTROY_EVENT       = 17,
    OP_EVENT_RECORD        = 18,
    OP_EVENT_QUERY         = 19,
    OP_EVENT_SYNC          = 20,
    OP_STREAM_WAIT_EVENT   = 21,
    OP_H2D_ASYNC_SUBMIT    = 22,
    OP_D2H_ASYNC_SUBMIT    = 23,
    OP_TRANSFER_QUERY      = 24,
    OP_TRANSFER_WAIT       = 25,
    OP_GET_DEVICE_INFO     = 26,
    OP_HELLO               = 27,
    OP_D2D                 = 28,
    OP_MEMSET              = 29,
    OP_MEMSET_ASYNC        = 30,
} CorexProtocolOpcode;

#define CRX_HELLO_SCHEMA_VERSION 1u
#define CRX_HELLO_FIXED_WORDS 11u
#define CRX_HELLO_MAX_CAPABILITIES 32u
#define CRX_HELLO_MAX_BYTES \
    ((CRX_HELLO_FIXED_WORDS + CRX_HELLO_MAX_CAPABILITIES) * sizeof(uint32_t))

typedef enum {
    CRX_BACKEND_COREX = 1,
} CorexBackendId;

typedef enum {
    CRX_DEVICE_PROFILE_UNKNOWN = 0,
    CRX_DEVICE_PROFILE_COREX_GENERIC = 1,
} CorexDeviceProfileId;

/* Stable semantic feature IDs. Unknown future IDs are safe to ignore. */
typedef enum {
    CRX_CAP_DEVICE_INFO = 1,
    CRX_CAP_LINEAR_MEMORY = 2,
    CRX_CAP_COPY_SYNC = 3,
    CRX_CAP_STREAM_EVENT = 4,
    CRX_CAP_COPY_ASYNC = 5,
    CRX_CAP_MODULE_KERNEL = 6,
} CorexCapabilityId;

typedef struct {
    uint32_t server_major;
    uint32_t server_minor;
    uint32_t server_patch;
    uint32_t backend_id;
    uint32_t backend_version;
    uint32_t device_count;
    uint32_t device_profile_id;
    uint32_t capability_count;
    uint32_t capabilities[CRX_HELLO_MAX_CAPABILITIES];
} CorexHello;

typedef enum {
    ARG_REMOTE_PTR = 1,
    ARG_I32        = 2,
    ARG_U64        = 3,
    ARG_F32        = 4,
    ARG_RAW_VALUE  = 5,
} CorexProtocolArgumentKind;

typedef struct {
    uint32_t opcode;
    uint32_t request_id;
    uint32_t payload_length;
} CorexProtocolRequestHeader;

typedef struct {
    uint32_t opcode;
    uint32_t request_id;
    uint32_t status;
    uint32_t payload_length;
} CorexProtocolResponseHeader;

static inline uint64_t corex_protocol_to_be64(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(value & 0xffffffffULL)) << 32) |
           htonl((uint32_t)(value >> 32));
#else
    return value;
#endif
}

static inline uint64_t corex_protocol_from_be64(uint64_t value)
{
    return corex_protocol_to_be64(value);
}

static inline uint32_t corex_protocol_read_u32(const unsigned char *data)
{
    uint32_t wire = 0;
    memcpy(&wire, data, sizeof(wire));
    return ntohl(wire);
}

static inline uint64_t corex_protocol_read_u64(const unsigned char *data)
{
    uint64_t wire = 0;
    memcpy(&wire, data, sizeof(wire));
    return corex_protocol_from_be64(wire);
}

static inline void corex_protocol_write_u32(
    unsigned char *data,
    size_t *position,
    uint32_t value)
{
    uint32_t wire = htonl(value);
    memcpy(data + *position, &wire, sizeof(wire));
    *position += sizeof(wire);
}

static inline void corex_protocol_write_u64(
    unsigned char *data,
    size_t *position,
    uint64_t value)
{
    uint64_t wire = corex_protocol_to_be64(value);
    memcpy(data + *position, &wire, sizeof(wire));
    *position += sizeof(wire);
}

static inline int corex_protocol_take_u32(
    const unsigned char *data,
    size_t length,
    size_t *position,
    uint32_t *value_out)
{
    if (!data || !position || !value_out ||
        *position > length || length - *position < sizeof(uint32_t))
        return -1;
    *value_out = corex_protocol_read_u32(data + *position);
    *position += sizeof(uint32_t);
    return 0;
}

static inline int corex_protocol_take_u64(
    const unsigned char *data,
    size_t length,
    size_t *position,
    uint64_t *value_out)
{
    if (!data || !position || !value_out ||
        *position > length || length - *position < sizeof(uint64_t))
        return -1;
    *value_out = corex_protocol_read_u64(data + *position);
    *position += sizeof(uint64_t);
    return 0;
}

/* HELLO is a bounded sequence of network-order u32 values, never a native ABI. */
static inline int corex_protocol_encode_hello(
    unsigned char output[CRX_HELLO_MAX_BYTES],
    const CorexHello *hello,
    uint32_t *length_out)
{
    if (!output || !hello || !length_out ||
        hello->capability_count > CRX_HELLO_MAX_CAPABILITIES)
        return -1;
    size_t position = 0;
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_MAGIC);
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_VERSION);
    corex_protocol_write_u32(output, &position, CRX_HELLO_SCHEMA_VERSION);
    corex_protocol_write_u32(output, &position, hello->server_major);
    corex_protocol_write_u32(output, &position, hello->server_minor);
    corex_protocol_write_u32(output, &position, hello->server_patch);
    corex_protocol_write_u32(output, &position, hello->backend_id);
    corex_protocol_write_u32(output, &position, hello->backend_version);
    corex_protocol_write_u32(output, &position, hello->device_count);
    corex_protocol_write_u32(output, &position, hello->device_profile_id);
    corex_protocol_write_u32(output, &position, hello->capability_count);
    for (uint32_t i = 0; i < hello->capability_count; ++i)
        corex_protocol_write_u32(output, &position, hello->capabilities[i]);
    *length_out = (uint32_t)position;
    return 0;
}

static inline int corex_protocol_decode_hello(
    const unsigned char *input,
    size_t length,
    CorexHello *hello_out)
{
    uint32_t magic, version, schema;
    size_t position = 0;
    if (!hello_out || length < CRX_HELLO_FIXED_WORDS * sizeof(uint32_t) ||
        length > CRX_HELLO_MAX_BYTES)
        return -1;
    memset(hello_out, 0, sizeof(*hello_out));
    if (corex_protocol_take_u32(input, length, &position, &magic) != 0 ||
        corex_protocol_take_u32(input, length, &position, &version) != 0 ||
        corex_protocol_take_u32(input, length, &position, &schema) != 0 ||
        magic != CRX_PROTOCOL_MAGIC || version != CRX_PROTOCOL_VERSION ||
        schema != CRX_HELLO_SCHEMA_VERSION ||
        corex_protocol_take_u32(input, length, &position, &hello_out->server_major) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->server_minor) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->server_patch) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->backend_id) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->backend_version) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->device_count) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->device_profile_id) != 0 ||
        corex_protocol_take_u32(input, length, &position, &hello_out->capability_count) != 0 ||
        hello_out->device_count == 0 ||
        hello_out->capability_count > CRX_HELLO_MAX_CAPABILITIES ||
        length != (CRX_HELLO_FIXED_WORDS + hello_out->capability_count) * sizeof(uint32_t))
        return -1;
    for (uint32_t i = 0; i < hello_out->capability_count; ++i) {
        uint32_t capability;
        if (corex_protocol_take_u32(input, length, &position, &capability) != 0 ||
            capability == 0)
            return -1;
        for (uint32_t j = 0; j < i; ++j) {
            if (hello_out->capabilities[j] == capability)
                return -1;
        }
        hello_out->capabilities[i] = capability;
    }
    return position == length ? 0 : -1;
}

static inline void corex_protocol_encode_request_header(
    unsigned char output[CRX_REQUEST_HEADER_BYTES],
    uint32_t opcode,
    uint32_t request_id,
    uint32_t payload_length)
{
    size_t position = 0;
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_MAGIC);
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_VERSION);
    corex_protocol_write_u32(output, &position, opcode);
    corex_protocol_write_u32(output, &position, request_id);
    corex_protocol_write_u32(output, &position, payload_length);
}

static inline int corex_protocol_decode_request_header(
    const unsigned char *input,
    size_t input_length,
    CorexProtocolRequestHeader *header_out)
{
    uint32_t magic = 0;
    uint32_t version = 0;
    size_t position = 0;
    if (!header_out ||
        corex_protocol_take_u32(input, input_length, &position, &magic) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &version) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->opcode) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->request_id) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->payload_length) != 0)
        return -1;
    return magic == CRX_PROTOCOL_MAGIC && version == CRX_PROTOCOL_VERSION ? 0 : -1;
}

static inline void corex_protocol_encode_response_header(
    unsigned char output[CRX_RESPONSE_HEADER_BYTES],
    uint32_t opcode,
    uint32_t request_id,
    uint32_t status,
    uint32_t payload_length)
{
    size_t position = 0;
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_MAGIC);
    corex_protocol_write_u32(output, &position, CRX_PROTOCOL_VERSION);
    corex_protocol_write_u32(output, &position, opcode);
    corex_protocol_write_u32(output, &position, request_id);
    corex_protocol_write_u32(output, &position, status);
    corex_protocol_write_u32(output, &position, payload_length);
}

static inline int corex_protocol_decode_response_header(
    const unsigned char *input,
    size_t input_length,
    CorexProtocolResponseHeader *header_out)
{
    uint32_t magic = 0;
    uint32_t version = 0;
    size_t position = 0;
    if (!header_out ||
        corex_protocol_take_u32(input, input_length, &position, &magic) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &version) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->opcode) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->request_id) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->status) != 0 ||
        corex_protocol_take_u32(input, input_length, &position, &header_out->payload_length) != 0)
        return -1;
    return magic == CRX_PROTOCOL_MAGIC && version == CRX_PROTOCOL_VERSION ? 0 : -1;
}

#ifdef __cplusplus
}
#endif

#endif
