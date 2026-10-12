#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc!=2)return 2;
 cudaLibrary_t library=NULL;cudaKernel_t kernel=NULL;unsigned int count=0;
 puts("ENTER_RUNTIME_LIBRARY_LOAD");
 cudaError_t result=cudaLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0);
 printf("cudaLibraryLoadFromFile=%d\n",result);
 if(result)return 0;
 result=cudaLibraryGetKernelCount(&count,library);printf("cudaLibraryGetKernelCount=%d count=%u\n",result,count);
 result=cudaLibraryGetKernel(&kernel,library,"missing_kernel");printf("cudaLibraryGetKernel(missing)=%d\n",result);
 puts("NATIVE_RUNTIME_UNLOAD_SKIPPED=known non-returning multi-kernel destructor");_Exit(0);
}
