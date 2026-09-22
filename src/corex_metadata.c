#include "corex_metadata.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Basic helpers
 * ============================================================
 */

static uint16_t read_le16(
    const unsigned char *p)
{
    return
        ((uint16_t)p[0]) |
        ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(
    const unsigned char *p)
{
    return
        ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(
    const unsigned char *p)
{
    return
        ((uint64_t)read_le32(p)) |
        ((uint64_t)read_le32(p + 4) << 32);
}

static uint16_t read_be16(
    const unsigned char *p)
{
    return
        ((uint16_t)p[0] << 8) |
        ((uint16_t)p[1]);
}

static uint32_t read_be32(
    const unsigned char *p)
{
    return
        ((uint32_t)p[0] << 24) |
        ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) |
        ((uint32_t)p[3]);
}

static uint64_t read_be64(
    const unsigned char *p)
{
    return
        ((uint64_t)read_be32(p) << 32) |
        ((uint64_t)read_be32(p + 4));
}

static size_t align4(
    size_t n)
{
    return (n + 3u) & ~((size_t)3u);
}

/* ============================================================
 * Minimal MessagePack reader
 * ============================================================
 */

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;

    char *error;
    size_t error_size;
} MpReader;

static void mp_error(
    MpReader *r,
    const char *fmt,
    ...)
{
    if (!r->error ||
        r->error_size == 0 ||
        r->error[0] != '\0')
        return;

    va_list ap;

    va_start(ap, fmt);

    vsnprintf(
        r->error,
        r->error_size,
        fmt,
        ap);

    va_end(ap);
}

static int mp_need(
    MpReader *r,
    size_t n)
{
    if (n > r->len - r->pos) {

        mp_error(
            r,
            "MessagePack truncated "
            "pos=%zu need=%zu len=%zu",
            r->pos,
            n,
            r->len);

        return -1;
    }

    return 0;
}

static int mp_get_u8(
    MpReader *r,
    unsigned char *out)
{
    if (mp_need(r, 1) != 0)
        return -1;

    *out =
        r->data[r->pos++];

    return 0;
}

/* ============================================================
 * MessagePack container lengths
 * ============================================================
 */

static int mp_read_map_len(
    MpReader *r,
    uint32_t *count)
{
    unsigned char c;

    if (mp_get_u8(r, &c) != 0)
        return -1;

    if (c >= 0x80 &&
        c <= 0x8f) {

        *count =
            c & 0x0f;

        return 0;
    }

    if (c == 0xde) {

        if (mp_need(r, 2) != 0)
            return -1;

        *count =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        return 0;
    }

    if (c == 0xdf) {

        if (mp_need(r, 4) != 0)
            return -1;

        *count =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        return 0;
    }

    mp_error(
        r,
        "expected MessagePack map "
        "at pos=%zu type=0x%02x",
        r->pos - 1,
        c);

    return -1;
}

static int mp_read_array_len(
    MpReader *r,
    uint32_t *count)
{
    unsigned char c;

    if (mp_get_u8(r, &c) != 0)
        return -1;

    if (c >= 0x90 &&
        c <= 0x9f) {

        *count =
            c & 0x0f;

        return 0;
    }

    if (c == 0xdc) {

        if (mp_need(r, 2) != 0)
            return -1;

        *count =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        return 0;
    }

    if (c == 0xdd) {

        if (mp_need(r, 4) != 0)
            return -1;

        *count =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        return 0;
    }

    mp_error(
        r,
        "expected MessagePack array "
        "at pos=%zu type=0x%02x",
        r->pos - 1,
        c);

    return -1;
}

/* ============================================================
 * MessagePack string
 * ============================================================
 */

static int mp_read_string(
    MpReader *r,
    char *out,
    size_t out_size)
{
    unsigned char c;
    uint32_t n = 0;

    if (!out ||
        out_size == 0)
        return -1;

    if (mp_get_u8(r, &c) != 0)
        return -1;

    if (c >= 0xa0 &&
        c <= 0xbf) {

        n =
            c & 0x1f;

    } else if (c == 0xd9) {

        unsigned char x;

        if (mp_get_u8(r, &x) != 0)
            return -1;

        n = x;

    } else if (c == 0xda) {

        if (mp_need(r, 2) != 0)
            return -1;

        n =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

    } else if (c == 0xdb) {

        if (mp_need(r, 4) != 0)
            return -1;

        n =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

    } else {

        mp_error(
            r,
            "expected MessagePack string "
            "at pos=%zu type=0x%02x",
            r->pos - 1,
            c);

        return -1;
    }

    if (mp_need(r, n) != 0)
        return -1;

    size_t copy =
        n;

    if (copy >= out_size)
        copy =
            out_size - 1;

    memcpy(
        out,
        r->data + r->pos,
        copy);

    out[copy] =
        '\0';

    r->pos += n;

    return 0;
}

/* ============================================================
 * MessagePack unsigned/nonnegative integer
 * ============================================================
 */

static int mp_read_uint64(
    MpReader *r,
    uint64_t *out)
{
    unsigned char c;

    if (mp_get_u8(r, &c) != 0)
        return -1;

    /* positive fixint */
    if (c <= 0x7f) {

        *out = c;
        return 0;
    }

    switch (c) {

    case 0xcc:

        if (mp_need(r, 1) != 0)
            return -1;

        *out =
            r->data[r->pos++];

        return 0;

    case 0xcd:

        if (mp_need(r, 2) != 0)
            return -1;

        *out =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        return 0;

    case 0xce:

        if (mp_need(r, 4) != 0)
            return -1;

        *out =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        return 0;

    case 0xcf:

        if (mp_need(r, 8) != 0)
            return -1;

        *out =
            read_be64(
                r->data + r->pos);

        r->pos += 8;

        return 0;

    case 0xd0: {

        if (mp_need(r, 1) != 0)
            return -1;

        int8_t v =
            (int8_t)
            r->data[r->pos++];

        if (v < 0)
            break;

        *out =
            (uint64_t)v;

        return 0;
    }

    case 0xd1: {

        if (mp_need(r, 2) != 0)
            return -1;

        int16_t v =
            (int16_t)
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        if (v < 0)
            break;

        *out =
            (uint64_t)v;

        return 0;
    }

    case 0xd2: {

        if (mp_need(r, 4) != 0)
            return -1;

        int32_t v =
            (int32_t)
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        if (v < 0)
            break;

        *out =
            (uint64_t)v;

        return 0;
    }

    case 0xd3: {

        if (mp_need(r, 8) != 0)
            return -1;

        int64_t v =
            (int64_t)
            read_be64(
                r->data + r->pos);

        r->pos += 8;

        if (v < 0)
            break;

        *out =
            (uint64_t)v;

        return 0;
    }

    default:
        break;
    }

    mp_error(
        r,
        "expected nonnegative integer "
        "at pos=%zu type=0x%02x",
        r->pos - 1,
        c);

    return -1;
}

/* ============================================================
 * Generic MessagePack skip
 * ============================================================
 */

static int mp_skip(
    MpReader *r);

static int mp_skip_n(
    MpReader *r,
    uint32_t count)
{
    for (uint32_t i = 0;
         i < count;
         ++i) {

        if (mp_skip(r) != 0)
            return -1;
    }

    return 0;
}

static int mp_skip(
    MpReader *r)
{
    unsigned char c;

    if (mp_get_u8(r, &c) != 0)
        return -1;

    /* fixint / negative fixint / nil / bool */
    if (c <= 0x7f ||
        c >= 0xe0 ||
        c == 0xc0 ||
        c == 0xc2 ||
        c == 0xc3)
        return 0;

    /* fixstr */
    if (c >= 0xa0 &&
        c <= 0xbf) {

        uint32_t n =
            c & 0x1f;

        if (mp_need(r, n) != 0)
            return -1;

        r->pos += n;
        return 0;
    }

    /* fixarray */
    if (c >= 0x90 &&
        c <= 0x9f) {

        return mp_skip_n(
            r,
            c & 0x0f);
    }

    /* fixmap */
    if (c >= 0x80 &&
        c <= 0x8f) {

        return mp_skip_n(
            r,
            2u * (c & 0x0f));
    }

    size_t n = 0;

    switch (c) {

    case 0xc4: /* bin8 */
    case 0xd9: /* str8 */

        if (mp_need(r, 1) != 0)
            return -1;

        n =
            r->data[r->pos++];

        break;

    case 0xc5: /* bin16 */
    case 0xda: /* str16 */

        if (mp_need(r, 2) != 0)
            return -1;

        n =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        break;

    case 0xc6: /* bin32 */
    case 0xdb: /* str32 */

        if (mp_need(r, 4) != 0)
            return -1;

        n =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        break;

    case 0xca: /* float32 */
        n = 4;
        break;

    case 0xcb: /* float64 */
        n = 8;
        break;

    case 0xcc:
    case 0xd0:
        n = 1;
        break;

    case 0xcd:
    case 0xd1:
        n = 2;
        break;

    case 0xce:
    case 0xd2:
        n = 4;
        break;

    case 0xcf:
    case 0xd3:
        n = 8;
        break;

    case 0xdc: { /* array16 */

        if (mp_need(r, 2) != 0)
            return -1;

        uint32_t count =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        return mp_skip_n(
            r,
            count);
    }

    case 0xdd: { /* array32 */

        if (mp_need(r, 4) != 0)
            return -1;

        uint32_t count =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        return mp_skip_n(
            r,
            count);
    }

    case 0xde: { /* map16 */

        if (mp_need(r, 2) != 0)
            return -1;

        uint32_t count =
            read_be16(
                r->data + r->pos);

        r->pos += 2;

        return mp_skip_n(
            r,
            2u * count);
    }

    case 0xdf: { /* map32 */

        if (mp_need(r, 4) != 0)
            return -1;

        uint32_t count =
            read_be32(
                r->data + r->pos);

        r->pos += 4;

        return mp_skip_n(
            r,
            2u * count);
    }

    default:

        mp_error(
            r,
            "unsupported MessagePack type "
            "0x%02x at pos=%zu",
            c,
            r->pos - 1);

        return -1;
    }

    if (mp_need(r, n) != 0)
        return -1;

    r->pos += n;

    return 0;
}

/* ============================================================
 * CoreX metadata decoding
 * ============================================================
 */

static CorexMetaArgKind parse_arg_kind(
    const char *value_kind)
{
    if (strcmp(
            value_kind,
            "global_buffer") == 0)
        return
            COREX_META_ARG_GLOBAL_BUFFER;

    if (strcmp(
            value_kind,
            "by_value") == 0)
        return
            COREX_META_ARG_BY_VALUE;

    return
        COREX_META_ARG_UNKNOWN;
}

static int parse_arg(
    MpReader *r,
    CorexArgMeta *arg)
{
    uint32_t fields = 0;

    if (mp_read_map_len(
            r,
            &fields) != 0)
        return -1;

    memset(
        arg,
        0,
        sizeof(*arg));

    for (uint32_t i = 0;
         i < fields;
         ++i) {

        char key[64];

        if (mp_read_string(
                r,
                key,
                sizeof(key)) != 0)
            return -1;

        if (strcmp(
                key,
                ".name") == 0) {

            if (mp_read_string(
                    r,
                    arg->name,
                    sizeof(arg->name)) != 0)
                return -1;

        } else if (strcmp(
                       key,
                       ".offset") == 0) {

            uint64_t v;

            if (mp_read_uint64(
                    r,
                    &v) != 0)
                return -1;

            if (v > UINT32_MAX) {

                mp_error(
                    r,
                    "arg offset too large");

                return -1;
            }

            arg->offset =
                (uint32_t)v;

        } else if (strcmp(
                       key,
                       ".size") == 0) {

            uint64_t v;

            if (mp_read_uint64(
                    r,
                    &v) != 0)
                return -1;

            if (v > UINT32_MAX) {

                mp_error(
                    r,
                    "arg size too large");

                return -1;
            }

            arg->size =
                (uint32_t)v;

        } else if (strcmp(
                       key,
                       ".value_kind") == 0) {

            if (mp_read_string(
                    r,
                    arg->value_kind,
                    sizeof(arg->value_kind)) != 0)
                return -1;

            arg->kind =
                parse_arg_kind(
                    arg->value_kind);

        } else if (strcmp(
                       key,
                       ".address_space") == 0) {

            if (mp_read_string(
                    r,
                    arg->address_space,
                    sizeof(arg->address_space)) != 0)
                return -1;

        } else {

            if (mp_skip(r) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_kernel(
    MpReader *r,
    CorexKernelMeta *kernel)
{
    uint32_t fields = 0;

    if (mp_read_map_len(
            r,
            &fields) != 0)
        return -1;

    memset(
        kernel,
        0,
        sizeof(*kernel));

    for (uint32_t i = 0;
         i < fields;
         ++i) {

        char key[96];

        if (mp_read_string(
                r,
                key,
                sizeof(key)) != 0)
            return -1;

        if (strcmp(
                key,
                ".name") == 0) {

            if (mp_read_string(
                    r,
                    kernel->name,
                    sizeof(kernel->name)) != 0)
                return -1;

        } else if (strcmp(
                       key,
                       ".symbol") == 0) {

            if (mp_read_string(
                    r,
                    kernel->symbol,
                    sizeof(kernel->symbol)) != 0)
                return -1;

        } else if (strcmp(
                       key,
                       ".kernarg_segment_align") == 0) {

            uint64_t v;

            if (mp_read_uint64(
                    r,
                    &v) != 0)
                return -1;

            kernel->kernarg_segment_align =
                (uint32_t)v;

        } else if (strcmp(
                       key,
                       ".kernarg_segment_size") == 0) {

            uint64_t v;

            if (mp_read_uint64(
                    r,
                    &v) != 0)
                return -1;

            kernel->kernarg_segment_size =
                (uint32_t)v;

        } else if (strcmp(
                       key,
                       ".warp_size") == 0) {

            uint64_t v;

            if (mp_read_uint64(
                    r,
                    &v) != 0)
                return -1;

            kernel->warp_size =
                (uint32_t)v;

        } else if (strcmp(
                       key,
                       ".args") == 0) {

            uint32_t count = 0;

            if (mp_read_array_len(
                    r,
                    &count) != 0)
                return -1;

            if (count >
                COREX_META_MAX_ARGS) {

                mp_error(
                    r,
                    "too many kernel args: %u",
                    count);

                return -1;
            }

            kernel->argc =
                count;

            for (uint32_t j = 0;
                 j < count;
                 ++j) {

                if (parse_arg(
                        r,
                        &kernel->args[j]) != 0)
                    return -1;
            }

        } else {

            if (mp_skip(r) != 0)
                return -1;
        }
    }

    return 0;
}

static int parse_root_metadata(
    const unsigned char *data,
    size_t size,
    CorexModuleMeta *out,
    char *error,
    size_t error_size)
{
    MpReader r;

    memset(
        &r,
        0,
        sizeof(r));

    r.data =
        data;

    r.len =
        size;

    r.error =
        error;

    r.error_size =
        error_size;

    uint32_t root_fields = 0;

    if (mp_read_map_len(
            &r,
            &root_fields) != 0)
        return -1;

    for (uint32_t i = 0;
         i < root_fields;
         ++i) {

        char key[96];

        if (mp_read_string(
                &r,
                key,
                sizeof(key)) != 0)
            return -1;

        if (strcmp(
                key,
                "iluvatar.kernels") == 0) {

            uint32_t count = 0;

            if (mp_read_array_len(
                    &r,
                    &count) != 0)
                return -1;

            if (count >
                COREX_META_MAX_KERNELS) {

                mp_error(
                    &r,
                    "too many kernels: %u",
                    count);

                return -1;
            }

            out->kernel_count =
                count;

            for (uint32_t j = 0;
                 j < count;
                 ++j) {

                if (parse_kernel(
                        &r,
                        &out->kernels[j]) != 0)
                    return -1;
            }

        } else if (strcmp(
                       key,
                       "iluvatar.version") == 0) {

            uint32_t count = 0;

            if (mp_read_array_len(
                    &r,
                    &count) != 0)
                return -1;

            for (uint32_t j = 0;
                 j < count;
                 ++j) {

                uint64_t v;

                if (mp_read_uint64(
                        &r,
                        &v) != 0)
                    return -1;

                if (j == 0)
                    out->version_major =
                        (uint32_t)v;

                if (j == 1)
                    out->version_minor =
                        (uint32_t)v;
            }

        } else {

            if (mp_skip(
                    &r) != 0)
                return -1;
        }
    }

    if (r.pos != r.len) {

        mp_error(
            &r,
            "metadata has trailing bytes: %zu",
            r.len - r.pos);

        return -1;
    }

    return 0;
}

/* ============================================================
 * ELF64 little-endian .note parser
 *
 * Relevant ELF64 offsets:
 *
 * e_shoff     0x28
 * e_shentsize 0x3A
 * e_shnum     0x3C
 * e_shstrndx  0x3E
 *
 * Elf64_Shdr:
 *
 * sh_name     +0
 * sh_type     +4
 * sh_offset   +24
 * sh_size     +32
 * ============================================================
 */

static int safe_range(
    size_t total,
    uint64_t offset,
    uint64_t size)
{
    if (offset > total)
        return -1;

    if (size > total - offset)
        return -1;

    return 0;
}

static int get_section(
    const unsigned char *image,
    size_t image_size,
    uint64_t shoff,
    uint16_t shentsize,
    uint16_t index,
    uint32_t *name_offset,
    uint64_t *section_offset,
    uint64_t *section_size)
{
    uint64_t off =
        shoff +
        (uint64_t)index *
        shentsize;

    if (safe_range(
            image_size,
            off,
            shentsize) != 0)
        return -1;

    const unsigned char *sh =
        image + off;

    *name_offset =
        read_le32(
            sh + 0);

    *section_offset =
        read_le64(
            sh + 24);

    *section_size =
        read_le64(
            sh + 32);

    if (safe_range(
            image_size,
            *section_offset,
            *section_size) != 0)
        return -1;

    return 0;
}

static int section_name_equals(
    const unsigned char *names,
    size_t names_size,
    uint32_t name_offset,
    const char *wanted)
{
    if (name_offset >=
        names_size)
        return 0;

    const char *s =
        (const char *)
        names +
        name_offset;

    size_t available =
        names_size -
        name_offset;

    size_t wanted_len =
        strlen(wanted);

    if (wanted_len + 1 >
        available)
        return 0;

    return
        memcmp(
            s,
            wanted,
            wanted_len + 1) == 0;
}

int corex_parse_metadata(
    const unsigned char *image,
    size_t image_size,
    CorexModuleMeta *out,
    char *error,
    size_t error_size)
{
    if (error &&
        error_size > 0)
        error[0] = '\0';

    if (!image ||
        !out) {

        if (error &&
            error_size > 0)
            snprintf(
                error,
                error_size,
                "invalid argument");

        return -1;
    }

    memset(
        out,
        0,
        sizeof(*out));

    if (image_size < 64) {

        snprintf(
            error,
            error_size,
            "image too small for ELF64");

        return -1;
    }

    if (image[0] != 0x7f ||
        image[1] != 'E' ||
        image[2] != 'L' ||
        image[3] != 'F') {

        snprintf(
            error,
            error_size,
            "not ELF");

        return -1;
    }

    if (image[4] != 2) {

        snprintf(
            error,
            error_size,
            "not ELF64");

        return -1;
    }

    if (image[5] != 1) {

        snprintf(
            error,
            error_size,
            "not little-endian ELF");

        return -1;
    }

    uint64_t shoff =
        read_le64(
            image + 0x28);

    uint16_t shentsize =
        read_le16(
            image + 0x3a);

    uint16_t shnum =
        read_le16(
            image + 0x3c);

    uint16_t shstrndx =
        read_le16(
            image + 0x3e);

    if (shentsize < 64 ||
        shnum == 0 ||
        shstrndx >= shnum) {

        snprintf(
            error,
            error_size,
            "invalid ELF section table");

        return -1;
    }

    if (safe_range(
            image_size,
            shoff,
            (uint64_t)shentsize *
            shnum) != 0) {

        snprintf(
            error,
            error_size,
            "ELF section table out of range");

        return -1;
    }

    uint32_t dummy_name;
    uint64_t shstr_offset;
    uint64_t shstr_size;

    if (get_section(
            image,
            image_size,
            shoff,
            shentsize,
            shstrndx,
            &dummy_name,
            &shstr_offset,
            &shstr_size) != 0) {

        snprintf(
            error,
            error_size,
            "invalid shstrtab");

        return -1;
    }

    const unsigned char *names =
        image +
        shstr_offset;

    uint64_t note_offset = 0;
    uint64_t note_size = 0;

    int note_found = 0;

    for (uint16_t i = 0;
         i < shnum;
         ++i) {

        uint32_t name_offset;
        uint64_t sec_offset;
        uint64_t sec_size;

        if (get_section(
                image,
                image_size,
                shoff,
                shentsize,
                i,
                &name_offset,
                &sec_offset,
                &sec_size) != 0) {

            snprintf(
                error,
                error_size,
                "invalid ELF section %u",
                i);

            return -1;
        }

        if (section_name_equals(
                names,
                (size_t)shstr_size,
                name_offset,
                ".note")) {

            note_offset =
                sec_offset;

            note_size =
                sec_size;

            note_found =
                1;

            break;
        }
    }

    if (!note_found) {

        snprintf(
            error,
            error_size,
            ".note section not found");

        return -1;
    }

    const unsigned char *notes =
        image +
        note_offset;

    size_t pos = 0;

    while (pos + 12 <=
           note_size) {

        uint32_t namesz =
            read_le32(
                notes + pos);

        uint32_t descsz =
            read_le32(
                notes + pos + 4);

        uint32_t type =
            read_le32(
                notes + pos + 8);

        pos += 12;

        size_t name_padded =
            align4(namesz);

        size_t desc_padded =
            align4(descsz);

        if (name_padded >
            note_size - pos)
            break;

        const unsigned char *owner =
            notes + pos;

        pos += name_padded;

        if (desc_padded >
            note_size - pos)
            break;

        const unsigned char *desc =
            notes + pos;

        pos += desc_padded;

        /*
         * GNU note namesz normally includes NUL.
         * We only require the visible 8-byte owner "Iluvatar".
         */
        int owner_is_iluvatar =
            namesz >= 8 &&
            memcmp(
                owner,
                "Iluvatar",
                8) == 0;

        if (owner_is_iluvatar &&
            type == 0x0a) {

            if (parse_root_metadata(
                    desc,
                    descsz,
                    out,
                    error,
                    error_size) != 0)
                return -1;

            if (out->kernel_count == 0) {

                snprintf(
                    error,
                    error_size,
                    "metadata contains no kernels");

                return -1;
            }

            return 0;
        }
    }

    snprintf(
        error,
        error_size,
        "Iluvatar metadata note type 0x0A not found");

    return -1;
}

const CorexKernelMeta *corex_find_kernel_meta(
    const CorexModuleMeta *module,
    const char *kernel_name)
{
    if (!module ||
        !kernel_name)
        return NULL;

    for (uint32_t i = 0;
         i < module->kernel_count;
         ++i) {

        if (strcmp(
                module->kernels[i].name,
                kernel_name) == 0)
            return
                &module->kernels[i];
    }

    return NULL;
}

const char *corex_meta_arg_kind_name(
    CorexMetaArgKind kind)
{
    switch (kind) {

    case COREX_META_ARG_GLOBAL_BUFFER:
        return "global_buffer";

    case COREX_META_ARG_BY_VALUE:
        return "by_value";

    default:
        return "unknown";
    }
}
