#include <cuda_runtime.h>
extern "C" {
__device__ int runtime_symbol_probe=41;
__global__ void runtime_symbol_probe_kernel(int *out){if(threadIdx.x==0)*out=runtime_symbol_probe;}
}
