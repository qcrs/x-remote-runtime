/* Test-only interposition remembers the real compiler registration handle so
 * the public shared-library client can exercise module-owned symbol lifetime. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
static void **registration;
void **__cudaRegisterFatBinary(void *image)
{
    void **(*real_register)(void *) = dlsym(RTLD_NEXT, "__cudaRegisterFatBinary");
    if (!real_register) abort();
    registration = real_register(image);
    return registration;
}
void **symbol_test_registration(void) { return registration; }
