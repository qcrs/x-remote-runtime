#ifndef COREX_METADATA_H
#define COREX_METADATA_H

#include <stddef.h>
#include <stdint.h>

#define COREX_META_MAX_KERNELS 32
#define COREX_META_MAX_ARGS    32

typedef enum {
    COREX_META_ARG_UNKNOWN = 0,
    COREX_META_ARG_GLOBAL_BUFFER,
    COREX_META_ARG_BY_VALUE
} CorexMetaArgKind;

typedef struct {
    char name[64];

    uint32_t offset;
    uint32_t size;

    CorexMetaArgKind kind;

    char value_kind[32];
    char address_space[32];
} CorexArgMeta;

typedef struct {
    char name[128];
    char symbol[128];

    uint32_t kernarg_segment_align;
    uint32_t kernarg_segment_size;
    uint32_t warp_size;

    uint32_t argc;

    CorexArgMeta args[COREX_META_MAX_ARGS];
} CorexKernelMeta;

typedef struct {
    uint32_t version_major;
    uint32_t version_minor;

    uint32_t kernel_count;

    CorexKernelMeta kernels[COREX_META_MAX_KERNELS];
} CorexModuleMeta;

/*
 * Parse CoreX / Iluvatar metadata directly from an in-memory cubin.
 *
 * return:
 *   0  success
 *  -1  parse/format error
 */
int corex_parse_metadata(
    const unsigned char *image,
    size_t image_size,
    CorexModuleMeta *out,
    char *error,
    size_t error_size);

/*
 * Find metadata by source-level kernel name,
 * e.g. "mixed_kernel".
 */
const CorexKernelMeta *corex_find_kernel_meta(
    const CorexModuleMeta *module,
    const char *kernel_name);

const char *corex_meta_arg_kind_name(
    CorexMetaArgKind kind);

#endif
