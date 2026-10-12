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

/* The caller supplies a complete readable CoreX ELF image, as for the vendor
 * load-data API. ELF spans provide the extent absent from that public API. */
int corex_library_image_size(const void *image, size_t available, size_t *size_out);

#ifdef __cplusplus
}
#endif

#endif
