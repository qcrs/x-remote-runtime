#include <cuda_runtime_api.h>
#include <stdio.h>
int main(void)
{
    const char *names[]={"cuMemAlloc","cuMemFree","cuMemcpyHtoD","cuCtxSynchronize","cuStreamCreate"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)
        for(unsigned long long flag=0;flag<3;++flag) {
            void *entry=NULL;
            cudaError_t result=cudaGetDriverEntryPoint(names[i],&entry,flag);
            printf("DRIVER_QUERY name=%s flags=%llu result=%d present=%d\n",names[i],flag,result,entry!=NULL);
        }
    return 0;
}
