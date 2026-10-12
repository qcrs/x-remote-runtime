#include <cuda_runtime.h>
#include <stdio.h>
extern "C" __global__ void handles_probe_kernel(int *value){if(threadIdx.x==0)*value+=1;}
int main(){
 setvbuf(stdout,NULL,_IONBF,0);
 cudaFunction_t function=NULL;cudaKernel_t kernel=NULL;
 cudaError_t a=cudaGetFuncBySymbol(&function,(const void *)handles_probe_kernel);
 printf("cudaGetFuncBySymbol=%d function_nonnull=%d\n",a,function!=NULL);
 cudaError_t b=cudaGetKernel(&kernel,(const void *)handles_probe_kernel);
 printf("cudaGetKernel=%d kernel_nonnull=%d\n",b,kernel!=NULL);

#ifdef COREX_REMOTE_HANDLES_TEST
 if(a || b || !function || !kernel)return 1;
 cudaFuncAttributes attributes;
 if(cudaFuncGetAttributes(&attributes,handles_probe_kernel)!=cudaSuccess)return 1;
 if(cudaFuncGetAttributes(&attributes,(const void *)function)!=cudaSuccess)return 1;
 int *value=NULL,host=41;
 if(cudaMalloc((void **)&value,4) || cudaMemcpy(value,&host,4,cudaMemcpyHostToDevice))return 1;
 void *args[]={&value};
 if(cudaLaunchKernel((const void *)kernel,dim3(1),dim3(1),args,0,NULL))return 1;
 if(cudaDeviceSynchronize() || cudaMemcpy(&host,value,4,cudaMemcpyDeviceToHost) || host!=42)return 1;
 if(cudaFree(value))return 1;
 puts("COMPILER_KERNEL_IDENTITIES_REMOTE=PASS");
#endif
 return 0;
}
