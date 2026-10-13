#include <cuda_runtime_api.h>
#include <cuda.h>
#include <stdio.h>
#include <string.h>

#define CHECK(call) do { int r=(call); printf("%s=%d\n",#call,r); if(r) return 1; } while(0)
#define QUERY(name, fn) CHECK(cudaGetDriverEntryPoint(name,(void **)&fn,cudaEnableLegacyStream))

int main(void)
{
    CHECK(cudaSetDevice(0));
    void *bootstrap=NULL;
    CHECK(cudaMalloc(&bootstrap,4));
    CUresult (CUDAAPI *alloc)(CUdeviceptr *,size_t), (CUDAAPI *release)(CUdeviceptr);
    CUresult (CUDAAPI *info)(size_t *,size_t *);
    CUresult (CUDAAPI *create)(CUstream *,unsigned), (CUDAAPI *create_priority)(CUstream *,unsigned,int);
    CUresult (CUDAAPI *destroy)(CUstream), (CUDAAPI *sync)(CUstream), (CUDAAPI *query)(CUstream);
    CUresult (CUDAAPI *flags)(CUstream,unsigned *), (CUDAAPI *priority)(CUstream,int *);
    CUresult (CUDAAPI *htod)(CUdeviceptr,const void *,size_t,CUstream);
    CUresult (CUDAAPI *dtoh)(void *,CUdeviceptr,size_t,CUstream);
    CUresult (CUDAAPI *dtod)(CUdeviceptr,CUdeviceptr,size_t,CUstream);
    CUresult (CUDAAPI *fill)(CUdeviceptr,unsigned char,size_t);
    CUresult (CUDAAPI *fill_async)(CUdeviceptr,unsigned char,size_t,CUstream);
    CUresult (CUDAAPI *event_create)(CUevent *,unsigned), (CUDAAPI *event_destroy)(CUevent);
    CUresult (CUDAAPI *record)(CUevent,CUstream), (CUDAAPI *event_sync)(CUevent), (CUDAAPI *event_query)(CUevent);
    CUresult (CUDAAPI *wait)(CUstream,CUevent,unsigned);
    QUERY("cuMemAlloc",alloc); QUERY("cuMemFree",release); QUERY("cuMemGetInfo",info);
    QUERY("cuStreamCreate",create); QUERY("cuStreamCreateWithPriority",create_priority);
    QUERY("cuStreamDestroy",destroy); QUERY("cuStreamSynchronize",sync); QUERY("cuStreamQuery",query);
    QUERY("cuStreamGetFlags",flags); QUERY("cuStreamGetPriority",priority);
    QUERY("cuMemcpyHtoDAsync",htod); QUERY("cuMemcpyDtoHAsync",dtoh); QUERY("cuMemcpyDtoDAsync",dtod);
    QUERY("cuMemsetD8",fill); QUERY("cuMemsetD8Async",fill_async);
    QUERY("cuEventCreate",event_create); QUERY("cuEventDestroy",event_destroy);
    QUERY("cuEventRecord",record); QUERY("cuEventSynchronize",event_sync); QUERY("cuEventQuery",event_query);
    QUERY("cuStreamWaitEvent",wait);
    const char *independent[]={"cuMemAlloc","cuMemFree","cuMemGetInfo","cuCtxSynchronize",
        "cuStreamCreate","cuStreamCreateWithPriority","cuStreamDestroy","cuEventCreate",
        "cuEventDestroy","cuEventQuery","cuEventSynchronize"};
    for(unsigned i=0;i<sizeof(independent)/sizeof(independent[0]);++i) {
        void *legacy=NULL,*ptds=NULL;
        CHECK(cudaGetDriverEntryPoint(independent[i],&legacy,cudaEnableLegacyStream));
        CHECK(cudaGetDriverEntryPoint(independent[i],&ptds,cudaEnablePerThreadDefaultStream));
        printf("NON_STREAM_ENTRY name=%s same=%d\n",independent[i],legacy==ptds);
        if(!legacy || legacy!=ptds) return 1;
    }
    CUstream a,b; CUevent event; CUdeviceptr src,dst;
    CHECK(create(&a,CU_STREAM_NON_BLOCKING)); CHECK(create_priority(&b,CU_STREAM_NON_BLOCKING,-1));
    unsigned f=0; int p=99; CHECK(flags(a,&f)); if(f!=CU_STREAM_NON_BLOCKING) return 1;
    CHECK(priority(b,&p)); printf("STREAM_PRIORITY=%d\n",p);
    CHECK(event_create(&event,CU_EVENT_DISABLE_TIMING)); CHECK(alloc(&src,32)); CHECK(alloc(&dst,32));
    size_t free_bytes=0,total_bytes=0; CHECK(info(&free_bytes,&total_bytes)); if(!total_bytes || free_bytes>total_bytes) return 1;
    unsigned char host[32],out[32]; memset(host,0x63,sizeof(host)); memset(out,0,sizeof(out));
    CHECK(htod(src,host,32,a)); CHECK(dtod(dst,src,32,a)); CHECK(dtoh(out,dst,32,a));
    CHECK(record(event,a)); CHECK(event_sync(event)); if(memcmp(host,out,32)) return 1;
    CHECK(event_query(event)); CHECK(wait(b,event,0));
    CHECK(fill_async(dst,0x42,32,b)); CHECK(dtoh(out,dst,32,b)); CHECK(sync(b));
    for(unsigned i=0;i<32;++i) if(out[i]!=0x42) return 1;
    CHECK(query(b)); CHECK(fill(src,0x15,32)); CHECK(dtoh(out,src,32,a)); CHECK(sync(a));
    for(unsigned i=0;i<32;++i) if(out[i]!=0x15) return 1;
    CHECK(event_destroy(event)); CHECK(destroy(a)); CHECK(destroy(b)); CHECK(release(src)); CHECK(release(dst));
    CHECK(cudaFree(bootstrap)); CHECK(cudaDeviceReset()); puts("PARTIAL_DRIVER_ENTRIES=PASS"); return 0;
}
