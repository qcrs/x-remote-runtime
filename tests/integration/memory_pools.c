#ifdef COREX_POOL_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(struct cudaMemPoolProps)==88,"CoreX pool properties ABI");
_Static_assert(offsetof(struct cudaMemPoolProps,location)==8,"pool location ABI");
_Static_assert(offsetof(struct cudaMemPoolProps,win32SecurityAttributes)==16,"pool security ABI");
_Static_assert(offsetof(struct cudaMemPoolProps,reserved)==24,"pool reserved ABI");
_Static_assert(sizeof(struct cudaMemLocation)==8,"location ABI");
_Static_assert(sizeof(struct cudaMemAccessDesc)==12,"access descriptor ABI");
#ifndef COREX_POOL_INTERNAL_ABI
#define CHECK(call) do{cudaError_t r=(call);printf("%s=%d\n",#call,r);if(r)return 1;}while(0)
#define EXPECT(call,want) do{cudaError_t r=(call);printf("%s=%d expected=%d\n",#call,r,want);if(r!=(want))return 1;}while(0)
int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 int supported=0;CHECK(cudaDeviceGetAttribute(&supported,cudaDevAttrMemoryPoolsSupported,0));if(!supported)return 1;
 cudaMemPool_t default_pool,current,pool;
 CHECK(cudaDeviceGetDefaultMemPool(&default_pool,0));CHECK(cudaDeviceGetMemPool(&current,0));if(current!=default_pool)return 1;
 struct cudaMemPoolProps props={0};props.allocType=cudaMemAllocationTypePinned;props.location.type=cudaMemLocationTypeDevice;
 CHECK(cudaMemPoolCreate(&pool,&props));if(pool==default_pool)return 1;
 CHECK(cudaDeviceSetMemPool(0,pool));CHECK(cudaDeviceGetMemPool(&current,0));if(current!=pool)return 1;
 CHECK(cudaDeviceGetDefaultMemPool(&current,0));if(current!=default_pool)return 1;
 unsigned long long threshold=4194304,read=0;
 CHECK(cudaMemPoolSetAttribute(pool,cudaMemPoolAttrReleaseThreshold,&threshold));
 CHECK(cudaMemPoolGetAttribute(pool,cudaMemPoolAttrReleaseThreshold,&read));if(read!=threshold)return 1;
 for(int a=1;a<=3;++a){struct{int value;uint32_t canary;} small={99,0xa5a5a5a5};
  CHECK(cudaMemPoolGetAttribute(pool,(enum cudaMemPoolAttr)a,&small.value));if(small.value!=0||small.canary!=0xa5a5a5a5)return 1;
  CHECK(cudaMemPoolSetAttribute(pool,(enum cudaMemPoolAttr)a,&small.value));small.value=1;
  EXPECT(cudaMemPoolSetAttribute(pool,(enum cudaMemPoolAttr)a,&small.value),cudaErrorNotSupported);
 }
 struct cudaMemLocation location={cudaMemLocationTypeDevice,0};enum cudaMemAccessFlags flags;
 CHECK(cudaMemPoolGetAccess(&flags,pool,&location));if(flags!=cudaMemAccessFlagsProtReadWrite)return 1;
 struct cudaMemAccessDesc access={location,cudaMemAccessFlagsProtReadWrite};CHECK(cudaMemPoolSetAccess(pool,&access,1));
 access.flags=cudaMemAccessFlagsProtRead;EXPECT(cudaMemPoolSetAccess(pool,&access,1),cudaErrorNotSupported);
 EXPECT(cudaMemPoolSetAccess(pool,NULL,0),cudaErrorInvalidValue);
 cudaStream_t first,second;cudaEvent_t event;CHECK(cudaStreamCreate(&first));CHECK(cudaStreamCreateWithFlags(&second,cudaStreamNonBlocking));
 CHECK(cudaEventCreateWithFlags(&event,cudaEventDisableTiming));
 unsigned char *a=NULL,*b=NULL,source[64],host[64];memset(source,0x36,sizeof(source));memset(host,0,sizeof(host));
 CHECK(cudaMallocAsync((void**)&a,64,first));
 struct cudaPointerAttributes attributes;CHECK(cudaPointerGetAttributes(&attributes,a));if(attributes.type!=cudaMemoryTypeDevice)return 1;
 CHECK(cudaMemcpyAsync(a,source,64,cudaMemcpyHostToDevice,first));memset(source,0x8f,sizeof(source));
 CHECK(cudaEventRecord(event,first));CHECK(cudaStreamWaitEvent(second,event,0));
 CHECK(cudaMallocFromPoolAsync((void**)&b,64,default_pool,second));
 CHECK(cudaMemcpyAsync(b,a,64,cudaMemcpyDeviceToDevice,second));CHECK(cudaMemcpyAsync(host,b,64,cudaMemcpyDeviceToHost,second));
 CHECK(cudaFreeAsync(b,second));for(int i=0;i<64;++i)if(host[i])return 1;
 EXPECT(cudaPointerGetAttributes(&attributes,b),cudaErrorInvalidValue);
 EXPECT(cudaMemsetAsync(b,1,64,second),cudaErrorInvalidDevicePointer);
 EXPECT(cudaFreeAsync(b,second),cudaErrorInvalidDevicePointer);
 CHECK(cudaEventRecord(event,second));CHECK(cudaStreamWaitEvent(first,event,0));CHECK(cudaFreeAsync(a,first));
 CHECK(cudaEventSynchronize(event));for(int i=0;i<64;++i)if(host[i]!=0x36)return 1;
 CHECK(cudaStreamSynchronize(first));
 CHECK(cudaMemPoolTrimTo(pool,0));
 CHECK(cudaMemPoolGetAttribute(pool,cudaMemPoolAttrReleaseThreshold,&read));if(read!=threshold)return 1;
 CHECK(cudaMemPoolGetAttribute(pool,cudaMemPoolAttrReservedMemCurrent,&read));if(read)return 1;
 for(int attr=5;attr<=8;++attr){read=0;CHECK(cudaMemPoolGetAttribute(pool,(enum cudaMemPoolAttr)attr,&read));printf("NATIVE_POOL_METRIC_%d=%llu\n",attr,read);}
 read=0;CHECK(cudaMemPoolSetAttribute(pool,cudaMemPoolAttrUsedMemHigh,&read));CHECK(cudaMemPoolGetAttribute(pool,cudaMemPoolAttrUsedMemHigh,&read));if(read)return 1;
 EXPECT(cudaMemPoolSetAttribute(pool,cudaMemPoolAttrUsedMemCurrent,&read),cudaErrorInvalidValue);
 read=1;EXPECT(cudaMemPoolSetAttribute(pool,cudaMemPoolAttrUsedMemHigh,&read),cudaErrorInvalidValue);
 void *ordinary_free=NULL;CHECK(cudaMallocFromPoolAsync(&ordinary_free,64,pool,first));
 CHECK(cudaStreamSynchronize(first));CHECK(cudaFree(ordinary_free));
 /* Destroy the selected pool while native work and an allocation remain. */
 CHECK(cudaMallocFromPoolAsync((void**)&a,64,pool,first));CHECK(cudaMemsetAsync(a,0x59,64,first));
 memset(host,0,64);CHECK(cudaMemcpyAsync(host,a,64,cudaMemcpyDeviceToHost,first));
 CHECK(cudaMemPoolTrimTo(pool,0));CHECK(cudaMemsetAsync(a,0x59,64,first));
 CHECK(cudaMemcpyAsync(host,a,64,cudaMemcpyDeviceToHost,first));
 CHECK(cudaMemPoolDestroy(pool));for(int i=0;i<64;++i)if(host[i])return 1;
 CHECK(cudaDeviceGetMemPool(&current,0));if(current!=default_pool)return 1;
 EXPECT(cudaMemPoolTrimTo(pool,0),cudaErrorInvalidResourceHandle);CHECK(cudaFreeAsync(a,first));
 CHECK(cudaStreamSynchronize(first));for(int i=0;i<64;++i)if(host[i]!=0x59)return 1;
 /* New fake addresses remain unique even when the native pool reuses memory. */
 void *previous=a;for(int i=0;i<3;++i){CHECK(cudaMallocAsync((void**)&a,64,NULL));if(a==previous)return 1;previous=a;CHECK(cudaMemsetAsync(a,0x6a,64,NULL));CHECK(cudaFreeAsync(a,NULL));}
 CHECK(cudaDeviceSynchronize());
 CHECK(cudaMallocAsync((void**)&a,64,first));CHECK(cudaStreamSynchronize(first));CHECK(cudaFree(a));
 CHECK(cudaMalloc((void**)&a,64));CHECK(cudaMemsetAsync(a,0x27,64,second));CHECK(cudaFreeAsync(a,second));CHECK(cudaStreamSynchronize(second));
 cudaLibrary_t library;cudaKernel_t kernel;CHECK(cudaLibraryLoadFromFile(&library,argv[1],NULL,NULL,0,NULL,NULL,0));
 CHECK(cudaLibraryGetKernel(&kernel,library,"m1s2_lifecycle_kernel"));
 int value=41,out=0;CHECK(cudaMallocAsync((void**)&a,4,second));CHECK(cudaMemcpyAsync(a,&value,4,cudaMemcpyHostToDevice,second));void *args[]={&a};
 CHECK(cudaLaunchKernel(kernel,(dim3){1,1,1},(dim3){1,1,1},args,0,second));CHECK(cudaMemcpyAsync(&out,a,4,cudaMemcpyDeviceToHost,second));CHECK(cudaFreeAsync(a,second));CHECK(cudaStreamSynchronize(second));if(out!=42)return 1;
 CHECK(cudaLibraryUnload(library));
 a=(void *)(uintptr_t)1;CHECK(cudaMallocAsync((void**)&a,0,first));if(a)return 1;
 EXPECT(cudaMallocFromPoolAsync((void**)&a,0,default_pool,first),cudaErrorInvalidValue);if(a)return 1;
 CHECK(cudaFreeAsync(NULL,first));
 EXPECT(cudaMallocFromPoolAsync((void**)&a,64,pool,first),cudaErrorInvalidResourceHandle);
 EXPECT(cudaMemPoolDestroy(default_pool),cudaErrorInvalidValue);
 EXPECT(cudaMemPoolGetAttribute(default_pool,(enum cudaMemPoolAttr)9,&read),cudaErrorInvalidValue);
 EXPECT(cudaDeviceGetMemPool(&current,1),cudaErrorInvalidDevice);
 props.reserved[0]=1;EXPECT(cudaMemPoolCreate(&pool,&props),cudaErrorInvalidValue);props.reserved[0]=0;
 props.handleTypes=cudaMemHandleTypePosixFileDescriptor;EXPECT(cudaMemPoolCreate(&pool,&props),cudaErrorNotSupported);props.handleTypes=cudaMemHandleTypeNone;
 CHECK(cudaStreamDestroy(second));EXPECT(cudaMallocAsync((void**)&a,64,second),cudaErrorInvalidResourceHandle);
 /* Native default pools outlive context reset; the session restores threshold. */
 read=0;CHECK(cudaMemPoolGetAttribute(default_pool,cudaMemPoolAttrReleaseThreshold,&read));
 threshold=123456;CHECK(cudaMemPoolSetAttribute(default_pool,cudaMemPoolAttrReleaseThreshold,&threshold));
 CHECK(cudaMemPoolCreate(&pool,&props));CHECK(cudaDeviceSetMemPool(0,pool));CHECK(cudaMallocAsync((void**)&a,64,first));
 CHECK(cudaMemsetAsync(a,0x28,64,first));memset(host,0,64);CHECK(cudaMemcpyAsync(host,a,64,cudaMemcpyDeviceToHost,first));
 CHECK(cudaDeviceReset());for(int i=0;i<64;++i)if(host[i]!=0x28)return 1;
 EXPECT(cudaMemPoolTrimTo(pool,0),cudaErrorInvalidResourceHandle);EXPECT(cudaFreeAsync(a,NULL),cudaErrorInvalidDevicePointer);
 CHECK(cudaDeviceGetDefaultMemPool(&current,0));if(current==default_pool)return 1;
 CHECK(cudaMemPoolGetAttribute(current,cudaMemPoolAttrReleaseThreshold,&threshold));if(threshold!=read)return 1;
 CHECK(cudaDeviceGetMemPool(&default_pool,0));if(default_pool!=current)return 1;
 /* Normal-exit cleanup with both a live pool and a live async allocation. */
 CHECK(cudaMemPoolCreate(&pool,&props));CHECK(cudaDeviceSetMemPool(0,pool));CHECK(cudaMallocAsync((void**)&a,64,NULL));CHECK(cudaMemsetAsync(a,0x7b,64,NULL));
 puts("MEMORY_POOLS_REMOTE=PASS");return 0;
}
#endif
