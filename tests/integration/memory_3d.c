#include <cuda_runtime_api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call) do { cudaError_t r=(call); if(r!=cudaSuccess) { \
    printf("FAIL line=%d call=%s cudaError=%d\n",__LINE__,#call,(int)r);return 1; } } while(0)

static const struct cudaExtent region={17,5,3};
static const struct cudaPos hp={3,1,1},ap={5,2,1},bp={2,1,1},outp={7,2,1};

static struct cudaMemcpy3DParms copy(struct cudaPitchedPtr dst,struct cudaPos dp,
                                    struct cudaPitchedPtr src,struct cudaPos sp,
                                    enum cudaMemcpyKind kind)
{
    struct cudaMemcpy3DParms p;
    memset(&p,0,sizeof(p));
    p.srcPtr=src;p.srcPos=sp;p.dstPtr=dst;p.dstPos=dp;p.extent=region;p.kind=kind;
    return p;
}

static size_t offset(struct cudaPitchedPtr ptr,struct cudaPos pos)
{ return pos.z*ptr.pitch*ptr.ysize+pos.y*ptr.pitch+pos.x; }

static void pattern(struct cudaPitchedPtr ptr,struct cudaPos pos)
{
    unsigned char *base=(unsigned char *)ptr.ptr+offset(ptr,pos);
    for(size_t z=0;z<region.depth;++z)
        for(size_t y=0;y<region.height;++y)
            for(size_t x=0;x<region.width;++x)
                base[z*ptr.pitch*ptr.ysize+y*ptr.pitch+x]=(unsigned char)(1+x+11*y+53*z);
}

static int basic_allocation(void)
{
    struct cudaPitchedPtr device;
    unsigned char input[255],output[255];
    for(size_t i=0;i<sizeof(input);++i)input[i]=(unsigned char)(i+1);
    memset(output,0,sizeof(output));
    CHECK(cudaMalloc3D(&device,region));
    if(!device.ptr||device.pitch<17||device.xsize!=17||device.ysize!=5)return 1;
    struct cudaPitchedPtr src={input,17,17,5},dst={output,17,17,5};
    struct cudaPos zero={0,0,0};
    struct cudaMemcpy3DParms p=copy(device,zero,src,zero,cudaMemcpyDefault);
    CHECK(cudaMemcpy3D(&p));
    p=copy(dst,zero,device,zero,cudaMemcpyDefault);
    CHECK(cudaMemcpy3D(&p));
    if(memcmp(input,output,sizeof(input)))return 1;
    CHECK(cudaFree(device.ptr));
    puts("MALLOC3D_BASIC_AND_DEFAULT=PASS");
    return 0;
}

int main(void)
{
    if(basic_allocation())return 1;
    unsigned char input[64*9*5],output[48*8*5],expected_output[sizeof(output)];
    struct cudaPitchedPtr host_src={input,64,64,9},host_dst={output,48,48,8};
    struct cudaPitchedPtr expected_host={expected_output,48,48,8};
    memset(input,0xCC,sizeof(input));pattern(host_src,hp);
    memset(output,0xCC,sizeof(output));memset(expected_output,0xCC,sizeof(expected_output));
    pattern(expected_host,outp);
    struct cudaMemcpy3DParms p=copy(host_dst,outp,host_src,hp,cudaMemcpyHostToHost);
    CHECK(cudaMemcpy3D(&p));
    if(memcmp(output,expected_output,sizeof(output)))return 1;
    puts("SYNC_3D_H2H_NONZERO_POS=PASS");

    struct cudaExtent allocation={32,8,5};
    struct cudaPitchedPtr a,b;
    CHECK(cudaMalloc3D(&a,allocation));CHECK(cudaMalloc3D(&b,allocation));
    if(a.pitch!=b.pitch)return 1;
    size_t bytes=a.pitch*allocation.height*allocation.depth;
    unsigned char *blank=malloc(bytes),*expected=malloc(bytes),*readback=malloc(bytes);
    if(!blank||!expected||!readback)return 1;
    memset(blank,0xCC,bytes);memcpy(expected,blank,bytes);
    struct cudaPitchedPtr expected_device={expected,b.pitch,b.xsize,b.ysize};
    CHECK(cudaMemcpy(a.ptr,blank,bytes,cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(b.ptr,blank,bytes,cudaMemcpyHostToDevice));
    p=copy(a,ap,host_src,hp,cudaMemcpyHostToDevice);CHECK(cudaMemcpy3D(&p));
    pattern(expected_device,ap);
    CHECK(cudaMemcpy(readback,a.ptr,bytes,cudaMemcpyDeviceToHost));
    if(memcmp(readback,expected,bytes))return 1;
    puts("SYNC_3D_H2D_NONZERO_POS_PADDING=PASS");
    memset(output,0xCC,sizeof(output));
    p=copy(host_dst,outp,a,ap,cudaMemcpyDeviceToHost);CHECK(cudaMemcpy3D(&p));
    if(memcmp(output,expected_output,sizeof(output)))return 1;
    puts("SYNC_3D_D2H_NONZERO_POS_PADDING=PASS");
    p=copy(b,bp,a,ap,cudaMemcpyDeviceToDevice);CHECK(cudaMemcpy3D(&p));
    memcpy(expected,blank,bytes);pattern(expected_device,bp);
    CHECK(cudaMemcpy(readback,b.ptr,bytes,cudaMemcpyDeviceToHost));
    if(memcmp(readback,expected,bytes))return 1;
    puts("SYNC_3D_D2D_NONZERO_POS_PADDING=PASS");

    p.srcPos.z=allocation.depth-1;
    if(cudaMemcpy3D(&p)!=cudaErrorInvalidValue)return 1;
    p.srcPos.z=SIZE_MAX;
    if(cudaMemcpy3D(&p)!=cudaErrorInvalidValue)return 1;
    puts("POSITION_OOB_AND_OVERFLOW_REJECTED=PASS");

    CHECK(cudaMemcpy(a.ptr,blank,bytes,cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(b.ptr,blank,bytes,cudaMemcpyHostToDevice));
    memset(output,0xCC,sizeof(output));
    cudaStream_t stream=NULL;CHECK(cudaStreamCreate(&stream));
    p=copy(a,ap,host_src,hp,cudaMemcpyHostToDevice);CHECK(cudaMemcpy3DAsync(&p,stream));
    /* The source may be reused after submit: the wire/server owns its snapshot. */
    memset(input,0xE1,sizeof(input));
    p=copy(b,bp,a,ap,cudaMemcpyDeviceToDevice);CHECK(cudaMemcpy3DAsync(&p,stream));
    p=copy(host_dst,outp,b,bp,cudaMemcpyDeviceToHost);CHECK(cudaMemcpy3DAsync(&p,NULL));
    for(size_t i=0;i<sizeof(output);++i)if(output[i]!=0xCC)return 1;
    puts("ASYNC_D2H_HOST_UNCHANGED_BEFORE_RETIREMENT=PASS");
    CHECK(cudaStreamSynchronize(NULL));
    if(memcmp(output,expected_output,sizeof(output)))return 1;
    CHECK(cudaMemcpy(readback,b.ptr,bytes,cudaMemcpyDeviceToHost));
    if(memcmp(readback,expected,bytes))return 1;
    puts("ASYNC_3D_H2D_EXPLICIT_PACKED_SNAPSHOT=PASS");
    puts("ASYNC_3D_D2D_EXPLICIT_STREAM_ORDER=PASS");
    puts("ASYNC_3D_D2H_DEFAULT_SCATTER_RETIREMENT_PADDING=PASS");
    puts("EXPLICIT_TO_DEFAULT_FRONTIER=PASS");
    CHECK(cudaStreamDestroy(stream));
    CHECK(cudaFree(b.ptr));CHECK(cudaFree(a.ptr));
    free(readback);free(expected);free(blank);
    puts("MEMORY_3D_INTEGRATION=PASS");
    return 0;
}
