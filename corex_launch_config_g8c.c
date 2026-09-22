#include "corex_remote_cuda_g8c.h"

#include <stdio.h>
#include <string.h>

/*
 * Gate 8A owns the compiler launch-configuration helpers that were previously
 * supplied by CoreX libcudart.
 *
 * CoreX 4.4 Gate-7F evidence proved the compiler lowering:
 *
 *   __cudaPushCallConfiguration
 *       -> generated __device_stub__
 *       -> __cudaPopCallConfiguration
 *       -> cudaLaunchKernel
 *
 * The public CUDA-compatible ABI is:
 *
 *   unsigned __cudaPushCallConfiguration(
 *       dim3 gridDim, dim3 blockDim, size_t sharedMem, void *stream);
 *
 *   cudaError_t __cudaPopCallConfiguration(
 *       dim3 *gridDim, dim3 *blockDim, size_t *sharedMem, void *stream);
 *
 * A per-thread LIFO is used so independent host threads do not share launch
 * configuration and nested compiler launch setup remains well-defined.
 */

#define G8A_LAUNCH_CONFIG_STACK_MAX 32u

typedef struct G8ALaunchConfig {
    dim3 grid;
    dim3 block;
    size_t shared_mem;
    void *stream;
} G8ALaunchConfig;

static __thread G8ALaunchConfig g_g8a_launch_stack[G8A_LAUNCH_CONFIG_STACK_MAX];
static __thread unsigned g_g8a_launch_depth = 0;

unsigned __cudaPushCallConfiguration(
    dim3 gridDim,
    dim3 blockDim,
    size_t sharedMem,
    void *stream)
{
    if (g_g8a_launch_depth >= G8A_LAUNCH_CONFIG_STACK_MAX) {
        fprintf(stderr,
                "G8A_PUSH_CONFIG result=FAIL reason=STACK_OVERFLOW depth=%u\n",
                g_g8a_launch_depth);
        return (unsigned)cudaErrorInvalidValue;
    }

    G8ALaunchConfig *cfg = &g_g8a_launch_stack[g_g8a_launch_depth++];
    cfg->grid = gridDim;
    cfg->block = blockDim;
    cfg->shared_mem = sharedMem;
    cfg->stream = stream;

    printf("G8A_PUSH_CONFIG depth=%u grid=(%u,%u,%u) block=(%u,%u,%u) shared=%zu stream=%p result=PASS\n",
           g_g8a_launch_depth,
           gridDim.x, gridDim.y, gridDim.z,
           blockDim.x, blockDim.y, blockDim.z,
           sharedMem,
           stream);

    return 0u;
}

cudaError_t __cudaPopCallConfiguration(
    dim3 *gridDim,
    dim3 *blockDim,
    size_t *sharedMem,
    void *stream)
{
    if (!gridDim || !blockDim || !sharedMem || !stream) {
        fprintf(stderr,
                "G8A_POP_CONFIG result=FAIL reason=NULL_OUTPUT depth=%u\n",
                g_g8a_launch_depth);
        return cudaErrorInvalidValue;
    }

    if (g_g8a_launch_depth == 0) {
        memset(gridDim, 0, sizeof(*gridDim));
        memset(blockDim, 0, sizeof(*blockDim));
        *sharedMem = 0;
        *(void **)stream = NULL;

        fprintf(stderr,
                "G8A_POP_CONFIG result=FAIL reason=STACK_UNDERFLOW depth=0\n");
        return cudaErrorInvalidValue;
    }

    G8ALaunchConfig cfg = g_g8a_launch_stack[--g_g8a_launch_depth];

    *gridDim = cfg.grid;
    *blockDim = cfg.block;
    *sharedMem = cfg.shared_mem;
    *(void **)stream = cfg.stream;

    printf("G8A_POP_CONFIG depth=%u grid=(%u,%u,%u) block=(%u,%u,%u) shared=%zu stream=%p result=PASS\n",
           g_g8a_launch_depth,
           cfg.grid.x, cfg.grid.y, cfg.grid.z,
           cfg.block.x, cfg.block.y, cfg.block.z,
           cfg.shared_mem,
           cfg.stream);

    return cudaSuccess;
}
