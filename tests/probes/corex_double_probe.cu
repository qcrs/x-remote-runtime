#include <cuda_runtime.h>
#include <stdio.h>
extern "C" __global__ void native_double_probe_kernel(double *value){if(threadIdx.x==0)*value+=0x1p-40;}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);double *device=NULL,value=1.0;
 cudaError_t r=cudaMalloc((void **)&device,sizeof(value));printf("cudaMalloc=%d\n",r);if(r)return 1;
 r=cudaMemcpy(device,&value,sizeof(value),cudaMemcpyHostToDevice);printf("cudaMemcpy(H2D)=%d\n",r);if(r)return 1;
 native_double_probe_kernel<<<1,1>>>(device);r=cudaDeviceSynchronize();printf("cudaDeviceSynchronize=%d\n",r);if(r)return 1;
 r=cudaMemcpy(&value,device,sizeof(value),cudaMemcpyDeviceToHost);printf("cudaMemcpy(D2H)=%d\n",r);if(r)return 1;
 printf("DOUBLE_ARITHMETIC=%s value=%.17g expected=%.17g\n",value==1.0+0x1p-40?"PASS":"FAIL",value,1.0+0x1p-40);
 cudaFree(device);return value==1.0+0x1p-40?0:1;
}
