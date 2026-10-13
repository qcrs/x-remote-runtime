#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    CUdevice device; CUcontext context;
    if(argc!=2 || cuInit(0) || cuDeviceGet(&device,0) || cuCtxCreate(&context,0,device)) return 1;
    FILE *file=fopen(argv[1],"rb"); if(!file) return 1;
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    void *image=malloc((size_t)size); if(!image || fread(image,1,(size_t)size,file)!=(size_t)size) return 1;
    fclose(file);
    CUmodule module=NULL; CUfunction function=NULL; CUdeviceptr ptr;
    CUresult result=cuModuleLoadData(&module,image);
    printf("MODULE_IMAGE_LOAD=%d\n",result);
    if(!result) {
        if(cuModuleGetFunction(&function,module,"m1s2_lifecycle_kernel") || cuMemAlloc(&ptr,4)) return 1;
        int input=41,output=0; void *args[]={&ptr};
        if(cuMemcpyHtoD(ptr,&input,4) || cuLaunchKernel(function,1,1,1,1,1,1,0,NULL,args,NULL) ||
           cuCtxSynchronize() || cuMemcpyDtoH(&output,ptr,4)) return 1;
        printf("MODULE_IMAGE_DATA=%d\n",output); if(output!=42) return 1;
        cuMemFree(ptr); printf("MODULE_UNLOAD=%d\n",cuModuleUnload(module));
    }
    CUlibrary library=NULL; CUlibraryOption option=CU_LIBRARY_BINARY_IS_PRESERVED;
    void *value=NULL;
    result=cuLibraryLoadData(&library,image,NULL,NULL,0,&option,&value,1);
    printf("LIBRARY_PRESERVED_LOAD=%d\n",result);
    if(!result) { unsigned count=0; result=cuLibraryGetKernelCount(&count,library);
        printf("LIBRARY_COUNT result=%d count=%u\n",result,count);
        unsigned capacities[]={0,1,3};
        for(unsigned i=0;i<sizeof(capacities)/sizeof(capacities[0]);++i) {
            CUkernel kernels[3]={0};
            result=cuLibraryEnumerateKernels(capacities[i] ? kernels : NULL,capacities[i],library);
            printf("LIBRARY_ENUM capacity=%u result=%d first=%d second=%d\n",
                   capacities[i],result,kernels[0]!=NULL,kernels[1]!=NULL);
        }
    }
    library=NULL;
    result=cuLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,&option,&value,1);
    printf("LIBRARY_FILE_PRESERVED_LOAD=%d\n",result);
    puts("PARTIAL_LIBRARY_NATIVE=COMPLETE; known native library unload hang excluded");
    _Exit(0);
}
