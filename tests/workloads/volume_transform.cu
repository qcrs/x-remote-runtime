#include "common.cuh"
__global__ void voxel(char *p,size_t pitch,size_t slice,int w,int h,int d) {
    int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y,z=blockIdx.z*blockDim.z+threadIdx.z;
    if(x<w && y<h && z<d) { int *row=(int *)(p+z*slice+y*pitch); row[x]=row[x]*2+x-y+3*z; }
}
int main() {
    const int w=23,h=11,d=7,row=w+5; cudaPitchedPtr p;
    CUDA(cudaMalloc3D(&p,make_cudaExtent(row*sizeof(int),h,d))); cudaStream_t s=stream();
    std::vector<int> src(row*h*d,0x33333333),out(row*h*d,-1);
    for(int z=0;z<d;++z) for(int y=0;y<h;++y) for(int x=0;x<w;++x) src[(z*h+y)*row+x]=x+10*y+100*z;
    cudaMemcpy3DParms cp={}; cp.srcPtr=make_cudaPitchedPtr(src.data(),row*sizeof(int),row*sizeof(int),h);
    cp.dstPtr=p; cp.extent=make_cudaExtent(row*sizeof(int),h,d); cp.kind=cudaMemcpyHostToDevice;
    CUDA(cudaMemcpy3DAsync(&cp,s));
    voxel<<<dim3((w+3)/4,(h+3)/4,(d+1)/2),dim3(4,4,2),0,s>>>((char *)p.ptr,p.pitch,p.pitch*p.ysize,w,h,d); CUDA(cudaGetLastError());
    cp.srcPtr=p; cp.dstPtr=make_cudaPitchedPtr(out.data(),row*sizeof(int),row*sizeof(int),h); cp.kind=cudaMemcpyDeviceToHost;
    CUDA(cudaMemcpy3DAsync(&cp,s)); REQUIRE(out[0]==-1); CUDA(cudaStreamSynchronize(s));
    for(int z=0;z<d;++z) for(int y=0;y<h;++y) for(int x=0;x<row;++x)
        REQUIRE(out[(z*h+y)*row+x]==(x<w?2*src[(z*h+y)*row+x]+x-y+3*z:0x33333333));
    CUDA(cudaFree(p.ptr)); CUDA(cudaStreamDestroy(s)); pass("volume_transform");
}
