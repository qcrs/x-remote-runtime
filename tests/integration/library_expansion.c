#include <cuda_runtime_api.h>
#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(call) do { cudaError_t r=(call);printf("%s=%d\n",#call,r);if(r)return 1; } while(0)
#define EXPECT(call,want) do { cudaError_t r=(call);printf("%s=%d expected=%d\n",#call,r,(want));if(r!=(want))return 1; } while(0)
int main(int argc,char **argv)
{
 if(argc<2)return 2;
 FILE *file=fopen(argv[1],"rb");if(!file)return 1;
 fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 void *image=malloc(size);if(!image||fread(image,1,size,file)!=(size_t)size)return 1;fclose(file);
 for(int from_data=0;from_data<3;++from_data){
  cudaLibrary_t library=NULL;cudaKernel_t kernel=NULL,mixed=NULL;unsigned int count=0;
  enum cudaLibraryOption preserved=cudaLibraryBinaryIsPreserved;void *hint=NULL;
  if(from_data==2)CHECK(cudaLibraryLoadData(&library,image,NULL,NULL,0,&preserved,&hint,1));
  else if(from_data)CHECK(cudaLibraryLoadData(&library,image,NULL,NULL,0,NULL,NULL,0));
  else CHECK(cudaLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0));
  CHECK(cudaLibraryGetKernelCount(&count,library));if(count!=2)return 1;
  CHECK(cudaLibraryGetKernel(&kernel,library,"m1s2_lifecycle_kernel"));
  CHECK(cudaLibraryGetKernel(&mixed,library,"library_mixed_kernel"));
  cudaFunction_t function=NULL;cudaKernel_t identity=NULL;
  CHECK(cudaGetFuncBySymbol(&function,(const void *)kernel));
  CHECK(cudaGetKernel(&identity,(const void *)kernel));
  if((const void *)function!=(const void *)kernel || identity!=kernel)return 1;
  struct cudaFuncAttributes attributes;
  CHECK(cudaFuncGetAttributes(&attributes,(const void *)function));
  cudaKernel_t missing=(cudaKernel_t)1;
  EXPECT(cudaLibraryGetKernel(&missing,library,"missing_kernel"),cudaErrorSymbolNotFound);
  if(missing || strcmp(cudaGetErrorName(cudaGetLastError()),"cudaErrorSymbolNotFound"))return 1;
  cudaKernel_t kernels[3]={(cudaKernel_t)1,(cudaKernel_t)2,(cudaKernel_t)3};
  CHECK(cudaLibraryEnumerateKernels(kernels,1,library));
  if(!kernels[0]||kernels[1]!=(cudaKernel_t)2||kernels[2]!=(cudaKernel_t)3)return 1;
  CHECK(cudaLibraryEnumerateKernels(kernels,3,library));
  if(!kernels[0]||!kernels[1]||kernels[0]==kernels[1]||kernels[2]!=(cudaKernel_t)3)return 1;
  if(!((kernels[0]==kernel&&kernels[1]==mixed)||(kernels[1]==kernel&&kernels[0]==mixed)))return 1;
  CHECK(cudaLibraryEnumerateKernels(kernels,0,library));
  EXPECT(cudaLibraryEnumerateKernels(NULL,0,library),cudaErrorInvalidResourceHandle);
  EXPECT(cudaLibraryEnumerateKernels(NULL,1,library),cudaErrorInvalidResourceHandle);
  int *ptr=NULL,host=41,delta=3;float scale=2.0f;cudaStream_t stream=NULL;
  CHECK(cudaMalloc((void **)&ptr,4));CHECK(cudaMemcpy(ptr,&host,4,cudaMemcpyHostToDevice));
  CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
  void *args[]={&ptr};void *mixed_args[]={&ptr,&delta,&scale};
  dim3 grid={1,1,1},block={1,1,1};
  CHECK(cudaLaunchKernel((const void *)function,grid,block,args,0,stream));
  CUresult (CUDAAPI *driver_launch)(CUfunction,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,CUstream,void **,void **);
  CHECK(cudaGetDriverEntryPoint("cuLaunchKernel",(void **)&driver_launch,cudaEnableLegacyStream));
  void *extra[]={NULL};
  if(driver_launch((CUfunction)function,1,1,1,1,1,1,0,(CUstream)stream,args,extra)!=CUDA_ERROR_NOT_SUPPORTED)return 1;
  if(driver_launch((CUfunction)function,1,1,1,1,1,1,0,(CUstream)stream,args,NULL))return 1;
  CHECK(cudaLaunchKernel((const void *)mixed,grid,block,mixed_args,0,stream));
  /* Unload must wait for outstanding launches and invalidate every token. */
  CHECK(cudaLibraryUnload(library));
  if(driver_launch((CUfunction)function,1,1,1,1,1,1,0,(CUstream)stream,args,NULL)!=CUDA_ERROR_INVALID_HANDLE)return 1;
  EXPECT(cudaLaunchKernel((const void *)kernel,grid,block,args,0,stream),cudaErrorInvalidResourceHandle);
  EXPECT(cudaLibraryGetKernelCount(&count,library),cudaErrorInvalidResourceHandle);
  EXPECT(cudaGetKernel(&identity,(const void *)kernel),cudaErrorInvalidResourceHandle);
  EXPECT(cudaGetFuncBySymbol(&function,(const void *)kernel),cudaErrorInvalidResourceHandle);
  EXPECT(cudaLibraryUnload(library),cudaErrorInvalidResourceHandle);
  CHECK(cudaMemcpy(&host,ptr,4,cudaMemcpyDeviceToHost));if(host!=49)return 1;
  CHECK(cudaStreamDestroy(stream));CHECK(cudaFree(ptr));
  printf("LIBRARY_DATA from_data=%d value=%d PASS\n",from_data,host);
 }
 cudaLibrary_t unsupported=NULL;
 EXPECT(cudaLibraryLoadData(&unsupported,image,NULL,NULL,1,NULL,NULL,0),cudaErrorNotSupported);
 enum cudaLibraryOption preserved=cudaLibraryBinaryIsPreserved;void *hint=NULL;
 EXPECT(cudaLibraryLoadFromFile(&unsupported,argv[1],NULL,NULL,0,&preserved,&hint,1),cudaErrorInvalidValue);
 EXPECT(cudaLibraryLoadData(&unsupported,image,NULL,NULL,0,NULL,&hint,1),cudaErrorInvalidValue);
 enum cudaLibraryOption universal=cudaLibraryHostUniversalFunctionAndDataTable;
 EXPECT(cudaLibraryLoadData(&unsupported,image,NULL,NULL,0,&universal,&hint,1),cudaErrorNotSupported);
 const unsigned char short_image[4]={0x7f,'E','L','F'};
 /* LoadData trusts a complete image; file loading must validate known lengths. */
 FILE *bad=tmpfile();if(!bad)return 1;fwrite(short_image,1,4,bad);
 char path[64];snprintf(path,sizeof(path),"/proc/self/fd/%d",fileno(bad));
 EXPECT(cudaLibraryLoadFromFile(&unsupported,path,NULL,NULL,0,NULL,NULL,0),cudaErrorInvalidValue);fclose(bad);
 /* Genuine fatbin magic/version with a declared span beyond the readable file
  * must be rejected before scanning its embedded image. */
 unsigned char truncated[24]={0x29,0x03,0x16,0x20,0,0,1,0,0xff,0xff};
 bad=tmpfile();if(!bad)return 1;fwrite(truncated,1,sizeof(truncated),bad);fflush(bad);
 snprintf(path,sizeof(path),"/proc/self/fd/%d",fileno(bad));
 EXPECT(cudaLibraryLoadFromFile(&unsupported,path,NULL,NULL,0,NULL,NULL,0),cudaErrorInvalidValue);fclose(bad);
 /* Intentionally leave a live library: normal exit exercises session cleanup. */
 CHECK(cudaLibraryLoadData(&unsupported,image,NULL,NULL,0,NULL,NULL,0));
 free(image);puts("LIBRARY_EXPANSION_REMOTE=PASS");return 0;
}
