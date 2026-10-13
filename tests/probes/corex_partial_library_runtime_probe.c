#include <cuda_runtime_api.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc!=2)return 1;
    cudaLibrary_t library=NULL;
    cudaError_t result=cudaLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0);
    printf("RUNTIME_LIBRARY_LOAD=%d\n",result);if(result)return 1;
    unsigned capacities[]={0,1,3};
    for(unsigned i=0;i<sizeof(capacities)/sizeof(capacities[0]);++i) {
        cudaKernel_t kernels[3]={0};
        result=cudaLibraryEnumerateKernels(capacities[i] ? kernels : NULL,capacities[i],library);
        printf("RUNTIME_LIBRARY_ENUM capacity=%u result=%d first=%d second=%d\n",
               capacities[i],result,kernels[0]!=NULL,kernels[1]!=NULL);
    }
    cudaKernel_t kernels[1]={0};
    printf("RUNTIME_LIBRARY_ENUM_NULL_ONE=%d\n",cudaLibraryEnumerateKernels(NULL,1,library));
    printf("RUNTIME_LIBRARY_ENUM_NONNULL_ZERO=%d\n",cudaLibraryEnumerateKernels(kernels,0,library));
    puts("PARTIAL_LIBRARY_RUNTIME_NATIVE=COMPLETE; known unload hang excluded");_Exit(0);
}
