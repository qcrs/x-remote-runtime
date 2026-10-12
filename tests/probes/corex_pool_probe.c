#include <cuda.h>
#include <stdio.h>
#define SHOW(call) do { CUresult r=(call); printf("%s=%d\n",#call,(int)r); fflush(stdout); } while(0)
int main(void) {
 CUdevice dev; CUcontext ctx; CUstream stream; CUmemoryPool pool=NULL, current=NULL, custom=NULL;
 CUdeviceptr ptr=0, linear=0; int supported=-1;
 if(cuInit(0)||cuDeviceGet(&dev,0)||cuCtxCreate(&ctx,0,dev)||cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING))return 1;
 SHOW(cuDeviceGetAttribute(&supported,CU_DEVICE_ATTRIBUTE_MEMORY_POOLS_SUPPORTED,dev));
 printf("MEMORY_POOLS_SUPPORTED=%d\n",supported);
 SHOW(cuDeviceGetDefaultMemPool(&pool,dev));
 SHOW(cuDeviceGetMemPool(&current,dev));
 printf("DEFAULT_POOL_PRESENT=%d CURRENT_POOL_PRESENT=%d\n",pool!=NULL,current!=NULL);
 CUmemPoolProps props={0}; props.allocType=CU_MEM_ALLOCATION_TYPE_PINNED;
 props.location.type=CU_MEM_LOCATION_TYPE_DEVICE; props.location.id=dev;
 SHOW(cuMemPoolCreate(&custom,&props)); printf("CUSTOM_POOL_PRESENT=%d\n",custom!=NULL);
 if(custom){
  for(int a=1;a<=4;++a){unsigned long long value=a==4?4194304:1,read=99;
   SHOW(cuMemPoolSetAttribute(custom,(CUmemPool_attribute)a,&value));
   SHOW(cuMemPoolGetAttribute(custom,(CUmemPool_attribute)a,&read));printf("SET_ATTR_%d_READBACK=%llu\n",a,read);
  }
  CUmemAccessDesc access={0};access.location.type=CU_MEM_LOCATION_TYPE_DEVICE;access.location.id=dev;
  unsigned options[]={0,1,3};for(unsigned i=0;i<3;++i){access.flags=(CUmemAccess_flags)options[i];
   CUresult r=cuMemPoolSetAccess(custom,&access,1);printf("SET_ACCESS flags=%u rc=%d\n",options[i],r);
   CUmemAccess_flags read=99;SHOW(cuMemPoolGetAccess(&read,custom,&access.location));printf("ACCESS_READBACK=%u\n",read);
  }
 }
 SHOW(cuMemAllocAsync(&ptr,4096,stream));
 if(ptr){SHOW(cuMemsetD8Async(ptr,0x39,4096,stream));SHOW(cuMemFreeAsync(ptr,stream));}
 if(custom){
  SHOW(cuDeviceSetMemPool(dev,custom));
  SHOW(cuMemAllocFromPoolAsync(&ptr,4096,custom,stream));
  if(ptr){SHOW(cuMemFreeAsync(ptr,stream));ptr=0;}
  for(int a=1;a<=8;++a){unsigned long long value=0;SHOW(cuMemPoolGetAttribute(custom,(CUmemPool_attribute)a,&value));printf("ATTR_%d=%llu\n",a,value);}
  SHOW(cuMemPoolTrimTo(custom,0));
  unsigned long long zero=0,high=99;
  SHOW(cuMemPoolSetAttribute(custom,CU_MEMPOOL_ATTR_USED_MEM_HIGH,&zero));
  SHOW(cuMemPoolGetAttribute(custom,CU_MEMPOOL_ATTR_USED_MEM_HIGH,&high));printf("RESET_USED_HIGH=%llu\n",high);
  SHOW(cuMemPoolSetAccess(custom,NULL,0));
  CUdeviceptr ordinary_free=0;
  SHOW(cuMemAllocFromPoolAsync(&ordinary_free,4096,custom,stream));SHOW(cuStreamSynchronize(stream));
  if(ordinary_free)SHOW(cuMemFree(ordinary_free));
  CUdeviceptr large=0;unsigned long long keep=67108864,reserved_before=0,reserved_after=0;
  size_t free_before=0,free_after=0,total=0;
  SHOW(cuMemPoolSetAttribute(custom,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&keep));
  SHOW(cuMemAllocFromPoolAsync(&large,8388608,custom,stream));
  if(large)SHOW(cuMemFreeAsync(large,stream));
  SHOW(cuStreamSynchronize(stream));
  SHOW(cuMemPoolGetAttribute(custom,CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,&reserved_before));
  SHOW(cuMemGetInfo(&free_before,&total));SHOW(cuMemPoolTrimTo(custom,0));
  SHOW(cuMemPoolGetAttribute(custom,CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,&reserved_after));SHOW(cuMemGetInfo(&free_after,&total));
  printf("TRIM reserved_before=%llu reserved_after=%llu free_before=%zu free_after=%zu\n",reserved_before,reserved_after,free_before,free_after);
  keep=0;SHOW(cuMemPoolSetAttribute(custom,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&keep));
  SHOW(cuCtxSynchronize());SHOW(cuMemPoolTrimTo(custom,0));
  SHOW(cuMemPoolGetAttribute(custom,CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,&reserved_after));SHOW(cuMemGetInfo(&free_after,&total));
  printf("ZERO_THRESHOLD_TRIM reserved_after=%llu free_after=%zu\n",reserved_after,free_after);
  CUmemAccess_flags flags=0;CUmemLocation loc={CU_MEM_LOCATION_TYPE_DEVICE,dev};
  SHOW(cuMemPoolGetAccess(&flags,custom,&loc));
  CUmemAccessDesc access={0};access.location=loc;access.flags=CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
  SHOW(cuMemPoolSetAccess(custom,&access,1));
  SHOW(cuStreamSynchronize(stream));if(pool)SHOW(cuDeviceSetMemPool(dev,pool));SHOW(cuMemPoolDestroy(custom));
 }
 if(!cuMemAlloc(&linear,4096)){CUresult r=cuMemFreeAsync(linear,stream);printf("cuMemFreeAsync(linear,stream)=%d\n",r);if(r)cuMemFree(linear);}
 ptr=0;SHOW(cuMemAllocAsync(&ptr,0,stream));printf("ZERO_ALLOC_NULL=%d\n",ptr==0);
 SHOW(cuMemFreeAsync(0,stream));
 if(pool){unsigned long long threshold=12345;SHOW(cuMemPoolSetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&threshold));}
 ptr=0;SHOW(cuMemAllocAsync(&ptr,4096,stream));SHOW(cuStreamSynchronize(stream));
 if(ptr)SHOW(cuMemFree(ptr));
 custom=NULL;SHOW(cuMemPoolCreate(&custom,&props));
 if(custom){
  ptr=0;SHOW(cuMemAllocFromPoolAsync(&ptr,4096,custom,stream));
  if(ptr){
   SHOW(cuMemsetD8Async(ptr,0x57,4096,stream));SHOW(cuMemPoolDestroy(custom));
   unsigned char data[4096]={0};SHOW(cuMemcpyDtoHAsync(data,ptr,4096,stream));SHOW(cuStreamSynchronize(stream));
   int match=1;for(unsigned i=0;i<sizeof(data);++i)if(data[i]!=0x57)match=0;printf("DESTROY_WITH_LIVE_ALLOCATION_DATA=%d\n",match);
   SHOW(cuMemFreeAsync(ptr,stream));
  }
 }
 SHOW(cuStreamSynchronize(stream));SHOW(cuStreamDestroy(stream));SHOW(cuCtxDestroy(ctx));
 SHOW(cuCtxCreate(&ctx,0,dev));SHOW(cuDeviceGetDefaultMemPool(&pool,dev));
 unsigned long long threshold=0;SHOW(cuMemPoolGetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&threshold));printf("DEFAULT_THRESHOLD_AFTER_CONTEXT_RESET=%llu\n",threshold);
 threshold=0;SHOW(cuMemPoolSetAttribute(pool,CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,&threshold));SHOW(cuCtxDestroy(ctx));return 0;
}
