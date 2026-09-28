#include <cuda.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  CUresult r=cuInit(0); printf("cuInit=%d\n",r); if(r) return 0;
  int n=0; r=cuDeviceGetCount(&n); printf("count=%d r=%d\n",n,r); if(r||n<1)return 0;
  CUdevice d; r=cuDeviceGet(&d,0); printf("device=%d r=%d\n",d,r); CUcontext c=0; r=cuCtxCreate(&c,0,d); printf("ctx_create=%d r=%d\n",(int)(uintptr_t)c,r); if(r)return 0;
  unsigned int flags=0; r=cuCtxGetFlags(&flags); printf("ctx_flags=%u r=%d\n",flags,r);
  int least=0,greatest=0; r=cuCtxGetStreamPriorityRange(&least,&greatest); printf("priority least=%d greatest=%d r=%d\n",least,greatest,r);
  size_t val=0; for(int lim=0;lim<=6;lim++){r=cuCtxGetLimit(&val,(CUlimit)lim);printf("limit[%d]=%zu r=%d\n",lim,val,r);}
  CUfunc_cache cache=0; r=cuCtxGetCacheConfig(&cache); printf("cache=%d r=%d\n",(int)cache,r);
  CUsharedconfig shared=0; r=cuCtxGetSharedMemConfig(&shared); printf("shared=%d r=%d\n",(int)shared,r);
  char pci[64]; memset(pci,0,sizeof(pci)); r=cuDeviceGetPCIBusId(pci,sizeof(pci),d); printf("pci=%s r=%d\n",pci,r);
  CUdevice d2=-1; r=cuDeviceGetByPCIBusId(&d2,pci); printf("pci_reverse=%d r=%d\n",d2,r);
  int attrs[]={1,8,10,13,16,36,37,75,76}; for(size_t i=0;i<sizeof(attrs)/sizeof(attrs[0]);i++){int v=-1;r=cuDeviceGetAttribute(&v,(CUdevice_attribute)attrs[i],d);printf("attr[%d]=%d r=%d\n",attrs[i],v,r);}
  r=cuCtxDestroy(c); printf("ctx_destroy=%d\n",r); return 0;
}
