#include <cuda_runtime.h>
extern "C" __global__ void m1s2_lifecycle_kernel(int *value)
{
    if(threadIdx.x==0) *value+=1;
}
extern "C" __global__ void library_mixed_kernel(int *value,int delta,float scale)
{
    if(threadIdx.x==0) *value+=(int)(delta*scale);
}
