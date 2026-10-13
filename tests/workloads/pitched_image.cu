#include "common.cuh"
__global__ void image(const char *src,size_t sp,char *dst,size_t dp,int w,int h,int scale) {
    int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x<w && y<h) ((int *)(dst+y*dp))[x]=((const int *)(src+y*sp))[x]*scale+x-y;
}
int main() {
    const int w=53,h=29,row=w+7; size_t ap,bp,cp;
    char *a,*b,*c; CUDA(cudaMallocPitch(&a,&ap,row*sizeof(int),h));
    CUDA(cudaMallocPitch(&b,&bp,row*sizeof(int),h)); CUDA(cudaMallocPitch(&c,&cp,row*sizeof(int),h));
    std::vector<int> src(row*h,0x12121212),out(row*h,0);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) src[y*row+x]=x+11*y;
    CUDA(cudaMemcpy2D(a,ap,src.data(),row*sizeof(int),row*sizeof(int),h,cudaMemcpyHostToDevice));
    CUDA(cudaMemset2D(b,bp,0x12,row*sizeof(int),h)); CUDA(cudaMemset2D(c,cp,0x12,row*sizeof(int),h));
    dim3 block(8,8),grid((w+7)/8,(h+7)/8);
    image<<<grid,block>>>(a,ap,b,bp,w,h,2); CUDA(cudaGetLastError());
    CUDA(cudaMemcpy2D(c,cp,b,bp,row*sizeof(int),h,cudaMemcpyDeviceToDevice));
    image<<<grid,block>>>(c,cp,a,ap,w,h,3); CUDA(cudaGetLastError());
    CUDA(cudaMemcpy2D(out.data(),row*sizeof(int),a,ap,row*sizeof(int),h,cudaMemcpyDeviceToHost));
    for(int y=0;y<h;++y) for(int x=0;x<row;++x)
        REQUIRE(out[y*row+x]==(x<w?6*src[y*row+x]+4*(x-y):0x12121212));
    CUDA(cudaFree(a)); CUDA(cudaFree(b)); CUDA(cudaFree(c)); pass("pitched_image");
}
