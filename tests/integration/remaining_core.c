#include <cuda_runtime_api.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define CHECK(call) do { cudaError_t r=(call); printf("%s=%d\n",#call,r); if(r)return 1; } while(0)
#define EXPECT(call,want) do { cudaError_t r=(call); printf("%s=%d expected=%d\n",#call,r,want); if(r!=(want))return 1; } while(0)
extern cudaError_t __cudaPopCallConfiguration(dim3 *,dim3 *,size_t *,void *);
typedef struct {
 pthread_mutex_t mutex;pthread_cond_t condition;int release;int observed;int calls;unsigned char *host;
} CallbackData;
static void host_function(void *opaque) {
 CallbackData *data=opaque;
 pthread_mutex_lock(&data->mutex);
 while(!data->release)pthread_cond_wait(&data->condition,&data->mutex);
 data->observed=1;
 for(int i=0;i<16;++i)if(data->host[i]!=0x35)data->observed=0;
 memset(data->host,0x4a,16);++data->calls;
 pthread_mutex_unlock(&data->mutex);
}
static void callback(cudaStream_t stream,cudaError_t status,void *opaque) {
 (void)stream;CallbackData *data=opaque;
 if(status==cudaSuccess)++data->calls;
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 CHECK(cudaDeviceReset());CHECK(cudaSetDeviceFlags(cudaDeviceScheduleSpin));unsigned flags=99;
 CHECK(cudaGetDeviceFlags(&flags));if(flags!=cudaDeviceScheduleSpin)return 1;
 EXPECT(cudaSetDeviceFlags(32),cudaErrorInvalidValue);
 cudaStream_t stream;CHECK(cudaStreamCreate(&stream));
 CHECK(cudaSetDeviceFlags(cudaDeviceScheduleSpin));EXPECT(cudaSetDeviceFlags(cudaDeviceScheduleYield),cudaErrorNotSupported);
 unsigned char *a=NULL,*b=NULL,host[16]={0},readback[16]={0};
 CHECK(cudaMalloc((void**)&a,16));CHECK(cudaMalloc((void**)&b,16));
 CHECK(cudaMemsetAsync(a,0x35,16,stream));CHECK(cudaMemcpyAsync(b,a,16,cudaMemcpyDeviceToDevice,stream));
 CHECK(cudaMemcpyAsync(host,b,16,cudaMemcpyDeviceToHost,stream));
 CallbackData data={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0,0,host};
 CHECK(cudaLaunchHostFunc(stream,host_function,&data));
 /* A waiting callback proves enqueue returns before function completion. */
 pthread_mutex_lock(&data.mutex);data.release=1;pthread_cond_broadcast(&data.condition);pthread_mutex_unlock(&data.mutex);
 CHECK(cudaMemcpyAsync(a,host,16,cudaMemcpyHostToDevice,stream));
 CHECK(cudaStreamAddCallback(stream,callback,&data,0));CHECK(cudaStreamSynchronize(stream));
 if(!data.observed||data.calls!=2)return 1;
 CHECK(cudaMemcpy(readback,a,16,cudaMemcpyDeviceToHost));for(int i=0;i<16;++i)if(readback[i]!=0x4a)return 1;
 CHECK(cudaLaunchHostFunc(NULL,host_function,&data));CHECK(cudaDeviceSynchronize());if(data.calls!=3)return 1;
 EXPECT(cudaLaunchHostFunc(stream,NULL,NULL),cudaErrorInvalidValue);
 EXPECT(cudaStreamAddCallback(stream,callback,&data,1),cudaErrorInvalidValue);
 CHECK(cudaConfigureCall((dim3){2,1,1},(dim3){8,1,1},13,stream));dim3 grid,block;size_t shared=0;cudaStream_t popped=NULL;
 CHECK(__cudaPopCallConfiguration(&grid,&block,&shared,&popped));if(grid.x!=2||block.x!=8||shared!=13||popped!=stream)return 1;
 cudaLibrary_t library;cudaKernel_t kernel;CHECK(cudaLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0));
 CHECK(cudaLibraryGetKernel(&kernel,library,"m1s2_lifecycle_kernel"));int before=42,after=0;CHECK(cudaMemcpy(a,&before,4,cudaMemcpyHostToDevice));
 cudaLaunchConfig_t config={0};config.gridDim=config.blockDim=(dim3){1,1,1};config.stream=stream;void *args[]={&a};
 CHECK(cudaLaunchKernelExC(&config,kernel,args));
 cudaLaunchAttribute attr={0};attr.id=cudaLaunchAttributeIgnore;config.attrs=&attr;config.numAttrs=1;
 CHECK(cudaLaunchKernelExC(&config,kernel,args));CHECK(cudaStreamSynchronize(stream));CHECK(cudaMemcpy(&after,a,4,cudaMemcpyDeviceToHost));if(after!=44)return 1;
 attr.id=cudaLaunchAttributeCooperative;EXPECT(cudaLaunchKernelExC(&config,kernel,args),cudaErrorNotSupported);
 EXPECT(cudaLaunchKernelExC(NULL,kernel,args),cudaErrorInvalidValue);
 memset(host,0,16);CHECK(cudaMemcpyAsync(host,a,16,cudaMemcpyDeviceToHost,stream));
 CHECK(cudaDeviceReset());memcpy(&after,host,4);if(after!=44)return 1;
 CHECK(cudaGetDeviceFlags(&flags));if(flags)return 1;
 EXPECT(cudaFree(a),cudaErrorInvalidDevicePointer);EXPECT(cudaStreamSynchronize(stream),cudaErrorInvalidResourceHandle);
 EXPECT(cudaLibraryUnload(library),cudaErrorInvalidResourceHandle);
 CHECK(cudaSetDeviceFlags(cudaDeviceScheduleYield));CHECK(cudaGetDeviceFlags(&flags));if(flags!=cudaDeviceScheduleYield)return 1;
 CHECK(cudaMalloc((void**)&a,16));CHECK(cudaFree(a));CHECK(cudaThreadExit());
 CHECK(cudaGetDeviceFlags(&flags));if(flags)return 1;
 puts("REMAINING_CORE_REMOTE=PASS");return 0;
}
