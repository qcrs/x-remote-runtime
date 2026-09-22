#ifndef COREX_FATBIN_RUNTIME_H
#define COREX_FATBIN_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const unsigned char *fatbin_bytes;
    size_t fatbin_size;
    const unsigned char *image_bytes;
    size_t image_size;
    size_t image_offset;
    uintptr_t object_base;
    char object_name[256];
} CorexLoadableImage;

int corex_image_from_compiler_wrapper(
    const void *wrapper,
    CorexLoadableImage *out,
    char *err,
    size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
