#include <cuda.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Group the still-unforwarded attributes; success alone does not establish
 * event, policy or cluster semantics. Always measure effects/readback. */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    CUdevice device; CUcontext context; CUmodule module; CUfunction function;
    CUstream stream, other; CUdeviceptr ptr;
    if (argc != 2 || cuInit(0) || cuDeviceGet(&device, 0) ||
        cuCtxCreate(&context, 0, device) || cuModuleLoad(&module, argv[1]) ||
        cuModuleGetFunction(&function, module, "m1s2_lifecycle_kernel") ||
        cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING) ||
        cuStreamCreate(&other, CU_STREAM_NON_BLOCKING) || cuMemAlloc(&ptr, 4096)) return 1;
    int ids[] = {1, 3, 8, 9, 10, 16};
    for (unsigned i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
        CUlaunchAttributeValue value = {0}, readback = {0};
        if (ids[i] == 8) value.priority = -1;
        if (ids[i] == 1) { value.accessPolicyWindow.base_ptr = (void *)ptr;
            value.accessPolicyWindow.num_bytes = 32;
            value.accessPolicyWindow.hitRatio = 1.0f; }
        CUresult get = cuStreamGetAttribute(stream, (CUstreamAttrID)ids[i], (CUstreamAttrValue *)&readback);
        CUresult set = cuStreamSetAttribute(stream, (CUstreamAttrID)ids[i], (const CUstreamAttrValue *)&value);
        CUresult after = cuStreamGetAttribute(stream, (CUstreamAttrID)ids[i], (CUstreamAttrValue *)&readback);
        printf("STREAM_ATTR id=%d get=%d set=%d after=%d value=%d\n", ids[i], get, set, after, readback.priority);
        if (!set) {
            CUresult copy = cuStreamCopyAttributes(other, stream);
            CUresult copied = cuStreamGetAttribute(other, (CUstreamAttrID)ids[i], (CUstreamAttrValue *)&readback);
            printf("STREAM_COPY id=%d result=%d get=%d value=%d\n", ids[i], copy, copied, readback.priority);
        }
    }
    CUlaunchConfig config = {.gridDimX=1, .gridDimY=1, .gridDimZ=1,
        .blockDimX=1, .blockDimY=1, .blockDimZ=1, .hStream=stream};
    void *args[] = {&ptr};
    int launch_ids[] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 11, 14, 16};
    for (unsigned i = 0; i < sizeof(launch_ids)/sizeof(launch_ids[0]); ++i) {
        CUlaunchAttribute attr = {0}; attr.id = (CUlaunchAttributeID)launch_ids[i];
        if (attr.id == CU_LAUNCH_ATTRIBUTE_COOPERATIVE) attr.value.cooperative = 1;
        if (attr.id == CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION) {
            attr.value.clusterDim.x=1; attr.value.clusterDim.y=1; attr.value.clusterDim.z=1;
        }
        if (attr.id == CU_LAUNCH_ATTRIBUTE_PRIORITY) attr.value.priority = -1;
        if (attr.id == CU_LAUNCH_ATTRIBUTE_PROGRAMMATIC_STREAM_SERIALIZATION)
            attr.value.programmaticStreamSerializationAllowed = 1;
        config.attrs=&attr; config.numAttrs=1;
        int input=40, output=-1;
        if (cuMemcpyHtoD(ptr,&input,4)) return 1;
        CUresult launch=cuLaunchKernelEx(&config,function,args,NULL);
        CUresult sync=cuStreamSynchronize(stream), copy=cuMemcpyDtoH(&output,ptr,4);
        printf("LAUNCH_ATTR id=%d result=%d sync=%d copy=%d data=%d\n",attr.id,launch,sync,copy,output);
    }
    CUlaunchAttribute invalid = {0}; invalid.id = (CUlaunchAttributeID)999;
    config.attrs=&invalid;
    printf("LAUNCH_INVALID_ATTRIBUTE=%d\n",cuLaunchKernelEx(&config,function,args,NULL));
    invalid.id = CU_LAUNCH_ATTRIBUTE_CLUSTER_DIMENSION;
    invalid.value.clusterDim.x=3; invalid.value.clusterDim.y=1; invalid.value.clusterDim.z=1;
    printf("LAUNCH_INVALID_CLUSTER=%d\n",cuLaunchKernelEx(&config,function,args,NULL));
    printf("DIRECT_COOPERATIVE=%d\n",cuLaunchCooperativeKernel(function,1,1,1,1,1,1,0,stream,args));
    CUevent event; if(cuEventCreate(&event,CU_EVENT_DISABLE_TIMING)) return 1;
    invalid.id=CU_LAUNCH_ATTRIBUTE_LAUNCH_COMPLETION_EVENT;
    memset(&invalid.value,0,sizeof(invalid.value)); invalid.value.launchCompletionEvent.event=event;
    printf("LAUNCH_COMPLETION_EVENT=%d\n",cuLaunchKernelEx(&config,function,args,NULL));
    cuStreamSynchronize(stream);
    /* A native unrecorded event query can itself return success. Elapsed-time
     * behavior distinguishes a real record from an ignored attribute. */
    CUevent timed; if(cuEventCreate(&timed,0)) return 1;
    invalid.value.launchCompletionEvent.event=timed;
    printf("LAUNCH_TIMED_COMPLETION_EVENT=%d\n",cuLaunchKernelEx(&config,function,args,NULL));
    cuStreamSynchronize(stream); float ms=-1;
    printf("LAUNCH_EVENT_ELAPSED=%d\n",cuEventElapsedTime(&ms,timed,timed));
    printf("DIRECT_EVENT_RECORD=%d\n",cuEventRecord(timed,stream)); cuStreamSynchronize(stream);
    printf("DIRECT_EVENT_ELAPSED=%d\n",cuEventElapsedTime(&ms,timed,timed));
    cuEventDestroy(timed); cuEventDestroy(event);
    FILE *file=fopen(argv[1],"rb"); if(!file) return 1;
    fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
    void *image=malloc((size_t)size); if(!image || fread(image,1,(size_t)size,file)!=(size_t)size) return 1;
    fclose(file);
    CUjit_option options[]={CU_JIT_MAX_REGISTERS,CU_JIT_OPTIMIZATION_LEVEL,CU_JIT_TARGET_FROM_CUCONTEXT};
    for(unsigned i=0;i<sizeof(options)/sizeof(options[0]);++i) {
        CUmodule loaded=NULL; void *value=(void *)(uintptr_t)(options[i]==CU_JIT_MAX_REGISTERS ? 32 : 0);
        CUresult result=cuModuleLoadDataEx(&loaded,image,1,&options[i],&value);
        printf("MODULE_JIT id=%d result=%d\n",options[i],result);
        if(!result) printf("MODULE_JIT_UNLOAD=%d\n",cuModuleUnload(loaded));
    }
    free(image); cuMemFree(ptr); cuStreamDestroy(other); cuStreamDestroy(stream);
    cuModuleUnload(module); cuCtxDestroy(context);
    puts("PARTIAL_SEMANTICS_NATIVE=COMPLETE"); return 0;
}
