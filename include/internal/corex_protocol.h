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
} CorexProtocolOpcode;

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
