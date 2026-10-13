#include <cuda_runtime.h>
#include <stdio.h>

texture<float, cudaTextureType1D, cudaReadModeElementType> closure_texture;
texture<float, cudaTextureType2D, cudaReadModeElementType> closure_texture_2d;
surface<void, cudaSurfaceType2D> closure_surface;
#ifdef PROBE_LEGACY_FETCH
__global__ void closure_texture_kernel(float *out) { *out = tex1Dfetch(closure_texture, 0); }
#endif

#define R(call) printf("%s result=%d\n", #call, (int)(call))
int main()
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (cudaSetDevice(0)) return 1;
    const textureReference *texture_ref = NULL;
    const surfaceReference *surface_ref = NULL;
    R(cudaGetTextureReference(&texture_ref, &closure_texture));
    R(cudaGetSurfaceReference(&surface_ref, &closure_surface));
    float *p = NULL;
    if (cudaMalloc((void **)&p, 4096)) return 1;
    cudaChannelFormatDesc desc = cudaCreateChannelDesc<float>();
    size_t offset = 0;
    R(cudaBindTexture(&offset, &closure_texture, p, &desc, 4096));
    R(cudaBindTexture2D(&offset, &closure_texture_2d, p, &desc, 32, 2, 128));
    R(cudaGetTextureAlignmentOffset(&offset, &closure_texture));
    R(cudaUnbindTexture(&closure_texture));
    cudaFree(p);
    puts("LEGACY_REFERENCE_PROBE=COMPLETE");
}
