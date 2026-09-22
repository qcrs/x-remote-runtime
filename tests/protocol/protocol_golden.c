#include "corex_protocol.h"

#include <stdio.h>
#include <string.h>

_Static_assert(CRX_REQUEST_HEADER_BYTES == 20, "V3 request header changed");
_Static_assert(CRX_RESPONSE_HEADER_BYTES == 24, "V3 response header changed");
_Static_assert(OP_ALLOC == 1 && OP_H2D == 2 && OP_LAUNCH == 3 &&
               OP_SYNC == 4 && OP_D2H == 5 && OP_FREE == 6 &&
               OP_CLOSE == 7 && OP_UPLOAD_MODULE == 8 &&
               OP_GET_KERNEL == 9 && OP_UNLOAD_MODULE == 10 &&
               OP_LAUNCH_GENERIC == 11 && OP_CREATE_STREAM == 12 &&
               OP_DESTROY_STREAM == 13 && OP_STREAM_QUERY == 14 &&
               OP_STREAM_SYNC == 15 && OP_CREATE_EVENT == 16 &&
               OP_DESTROY_EVENT == 17 && OP_EVENT_RECORD == 18 &&
               OP_EVENT_QUERY == 19 && OP_EVENT_SYNC == 20 &&
               OP_STREAM_WAIT_EVENT == 21 && OP_H2D_ASYNC_SUBMIT == 22 &&
               OP_D2H_ASYNC_SUBMIT == 23 && OP_TRANSFER_QUERY == 24 &&
               OP_TRANSFER_WAIT == 25 && OP_GET_DEVICE_INFO == 26,
               "V3 opcode assignment changed");

int main(void)
{
    static const unsigned char expected_request[] = {
        0x43, 0x52, 0x58, 0x39, 0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x08,
    };
    static const unsigned char expected_response[] = {
        0x43, 0x52, 0x58, 0x39, 0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x1a, 0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x04,
    };
    static const unsigned char expected_scalars[] = {
        0x89, 0xab, 0xcd, 0xef,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };

    unsigned char request[CRX_REQUEST_HEADER_BYTES];
    unsigned char response[CRX_RESPONSE_HEADER_BYTES];
    unsigned char scalars[sizeof(expected_scalars)];
    size_t position = 0;

    corex_protocol_encode_request_header(request, OP_ALLOC, 0x01020304u, 8);
    corex_protocol_encode_response_header(
        response, OP_GET_DEVICE_INFO, 0x01020304u, ST_METADATA_ERROR, 4);
    corex_protocol_write_u32(scalars, &position, 0x89abcdefu);
    corex_protocol_write_u64(scalars, &position, 0x0123456789abcdefULL);

    if (memcmp(request, expected_request, sizeof(request)) != 0 ||
        memcmp(response, expected_response, sizeof(response)) != 0 ||
        memcmp(scalars, expected_scalars, sizeof(scalars)) != 0)
        return 1;

    CorexProtocolRequestHeader request_header;
    CorexProtocolResponseHeader response_header;
    if (corex_protocol_decode_request_header(
            request, sizeof(request), &request_header) != 0 ||
        corex_protocol_decode_response_header(
            response, sizeof(response), &response_header) != 0)
        return 2;
    if (request_header.opcode != OP_ALLOC ||
        request_header.request_id != 0x01020304u ||
        request_header.payload_length != 8 ||
        response_header.opcode != OP_GET_DEVICE_INFO ||
        response_header.status != ST_METADATA_ERROR ||
        response_header.payload_length != 4)
        return 3;

    if (corex_protocol_decode_request_header(
            request, sizeof(request) - 1, &request_header) == 0)
        return 4;
    request[0] = 0;
    if (corex_protocol_decode_request_header(
            request, sizeof(request), &request_header) == 0)
        return 5;

    position = 0;
    uint32_t value32 = 0;
    uint64_t value64 = 0;
    if (corex_protocol_take_u32(
            scalars, sizeof(scalars), &position, &value32) != 0 ||
        corex_protocol_take_u64(
            scalars, sizeof(scalars), &position, &value64) != 0 ||
        value32 != 0x89abcdefu || value64 != 0x0123456789abcdefULL)
        return 6;
    if (corex_protocol_take_u32(
            scalars, sizeof(scalars), &position, &value32) == 0)
        return 7;

    printf("M1_S3_PROTOCOL_GOLDEN=PASS\n");
    printf("M1_S3_PROTOCOL_BOUNDED_DECODE=PASS\n");
    printf("M1_S3_PROTOCOL_OPCODES_1_26=PASS\n");
    return 0;
}
