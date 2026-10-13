#ifdef COREX_GRAPH_INTERNAL_ABI
#include "corex_cuda_runtime.h"
#else
#include <cuda_runtime_api.h>
#endif
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(struct cudaKernelNodeParams) == 56, "kernel graph ABI");
_Static_assert(offsetof(struct cudaKernelNodeParams, kernelParams) == 40,
               "kernel args ABI");
_Static_assert(sizeof(struct cudaMemsetParams) == 40, "fill graph ABI");
_Static_assert(offsetof(struct cudaMemsetParams, width) == 24,
               "fill width ABI");
#ifndef COREX_GRAPH_INTERNAL_ABI
typedef struct corexRemoteModuleHandle *corexRemoteModule_t;
typedef struct {
  int kind;
  uint32_t size;
} ArgDesc;
extern cudaError_t corexRemoteModuleLoad(const char *, corexRemoteModule_t *);
extern cudaError_t corexRemoteModuleUnload(corexRemoteModule_t);
extern cudaError_t corexRemoteRegisterKernel(corexRemoteModule_t, const char *,
                                             const ArgDesc *, size_t,
                                             const void **);
#define CHECK(call)                                                            \
  do {                                                                         \
    cudaError_t r = (call);                                                    \
    printf("%s=%d\n", #call, r);                                               \
    fflush(stdout);                                                            \
    if (r)                                                                     \
      return 1;                                                                \
  } while (0)
#define EXPECT(call, want)                                                     \
  do {                                                                         \
    cudaError_t r = (call);                                                    \
    printf("%s=%d want=%d\n", #call, r, want);                                 \
    fflush(stdout);                                                            \
    if (r != (want))                                                           \
      return 1;                                                                \
  } while (0)
static struct cudaMemcpy3DParms copy_params(void *dst, void *src,
                                            size_t bytes) {
  struct cudaMemcpy3DParms p = {0};
  p.dstPtr = (struct cudaPitchedPtr){dst, bytes, bytes, 1};
  p.srcPtr = (struct cudaPitchedPtr){src, bytes, bytes, 1};
  p.extent = (struct cudaExtent){bytes, 1, 1};
  p.kind = cudaMemcpyDeviceToDevice;
  return p;
}
static int read_value(void *p, int want) {
  int got = 0;
  CHECK(cudaMemcpy(&got, p, 4, cudaMemcpyDeviceToHost));
  printf("GRAPH_VALUE got=%d want=%d\n", got, want);
  return got != want;
}
static void *wrong_capture_thread(void *stream) {
  cudaGraph_t graph = NULL;
  return (void *)(uintptr_t)cudaStreamEndCapture((cudaStream_t)stream, &graph);
}
static void counted_host_function(void *count) { ++*(int *)count; }
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  void *a = NULL, *b = NULL;
  CHECK(cudaMalloc(&a, 256));
  CHECK(cudaMalloc(&b, 256));
  cudaStream_t stream, other;
  CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
  CHECK(cudaStreamCreateWithFlags(&other, cudaStreamNonBlocking));
  corexRemoteModule_t module;
  const void *func;
  ArgDesc arg = {1, sizeof(void *)};
  CHECK(corexRemoteModuleLoad(argv[1], &module));
  CHECK(corexRemoteRegisterKernel(module, "m1s2_lifecycle_kernel", &arg, 1,
                                  &func));
  cudaGraph_t graph;
  cudaGraphNode_t root, fill, kernel, copy, leaf;
  cudaGraphExec_t exec;
  CHECK(cudaGraphCreate(&graph, 0));
  CHECK(cudaGraphAddEmptyNode(&root, graph, NULL, 0));
  struct cudaMemsetParams mp = {0};
  mp.dst = a;
  mp.value = 41;
  mp.elementSize = 4;
  mp.width = 1;
  mp.height = 1;
  CHECK(cudaGraphAddMemsetNode(&fill, graph, &root, 1, &mp));
  void *args[] = {&a};
  struct cudaKernelNodeParams kp = {0};
  kp.func = (void *)func;
  kp.gridDim = (dim3){1, 1, 1};
  kp.blockDim = (dim3){1, 1, 1};
  kp.kernelParams = args;
  CHECK(cudaGraphAddKernelNode(&kernel, graph, &fill, 1, &kp));
  struct cudaMemcpy3DParms cp = copy_params(b, a, 4);
  CHECK(cudaGraphAddMemcpyNode(&copy, graph, &kernel, 1, &cp));
  CHECK(cudaGraphAddEmptyNode(&leaf, graph, &copy, 1));
  enum cudaGraphNodeType type;
  CHECK(cudaGraphNodeGetType(kernel, &type));
  if (type != cudaGraphNodeTypeKernel)
    return 1;
  size_t count = 0;
  CHECK(cudaGraphGetNodes(graph, NULL, &count));
  if (count != 5)
    return 1;
  cudaGraphNode_t nodes[8] = {0};
  count = 8;
  CHECK(cudaGraphGetNodes(graph, nodes, &count));
  if (count != 5 || nodes[5])
    return 1;
  count = 8;
  CHECK(cudaGraphGetRootNodes(graph, nodes, &count));
  if (count != 1 || nodes[0] != root)
    return 1;
  cudaGraphNode_t from[8], to[8];
  count = 8;
  CHECK(cudaGraphGetEdges(graph, from, to, &count));
  if (count != 4)
    return 1;
  count = 8;
  CHECK(cudaGraphNodeGetDependencies(copy, nodes, &count));
  if (count != 1 || nodes[0] != kernel)
    return 1;
  count = 8;
  CHECK(cudaGraphNodeGetDependentNodes(fill, nodes, &count));
  if (count != 1 || nodes[0] != kernel)
    return 1;
  CHECK(cudaGraphRemoveDependencies(graph, &copy, &leaf, 1));
  CHECK(cudaGraphAddDependencies(graph, &copy, &leaf, 1));
  EXPECT(cudaGraphAddDependencies(graph, &leaf, &root, 1),
         cudaErrorInvalidValue);
  EXPECT(cudaGraphAddDependencies(graph, &copy, &leaf, 1),
         cudaErrorInvalidValue);
  struct cudaKernelNodeParams kg;
  CHECK(cudaGraphKernelNodeGetParams(kernel, &kg));
  if (kg.func != (void *)func || *(void **)kg.kernelParams[0] != a)
    return 1;
  struct cudaMemsetParams mg;
  CHECK(cudaGraphMemsetNodeGetParams(fill, &mg));
  if (mg.value != 41 || mg.dst != a)
    return 1;
  struct cudaMemcpy3DParms cg;
  CHECK(cudaGraphMemcpyNodeGetParams(copy, &cg));
  if (cg.srcPtr.ptr != a || cg.dstPtr.ptr != b)
    return 1;
  cudaGraphNode_t error = (cudaGraphNode_t)(uintptr_t)1;
  char log[64];
  CHECK(cudaGraphInstantiate(&exec, graph, &error, log, sizeof(log)));
  if (error || log[0])
    return 1;
  for (int i = 0; i < 2; ++i) {
    CHECK(cudaGraphLaunch(exec, stream));
    CHECK(cudaStreamSynchronize(stream));
    if (read_value(b, 42))
      return 1;
  }
  mp.value = 50;
  CHECK(cudaGraphMemsetNodeSetParams(fill, &mp));
  CHECK(cudaGraphKernelNodeSetParams(kernel, &kp));
  CHECK(cudaGraphMemcpyNodeSetParams(copy, &cp));
  /* Original graph mutation does not change the executable snapshot. */
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 42))
    return 1;
  enum cudaGraphExecUpdateResult update_result;
  CHECK(cudaGraphExecUpdate(exec, graph, &error, &update_result));
  if (update_result != cudaGraphExecUpdateSuccess || error)
    return 1;
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 51))
    return 1;
  CHECK(cudaGraphExecMemsetNodeSetParams(exec, fill, &mp));
  CHECK(cudaGraphExecKernelNodeSetParams(exec, kernel, &kp));
  CHECK(cudaGraphExecMemcpyNodeSetParams(exec, copy, &cp));
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 51))
    return 1;
  unsigned enabled = 99;
  CHECK(cudaGraphNodeSetEnabled(exec, kernel, 0));
  CHECK(cudaGraphNodeGetEnabled(exec, kernel, &enabled));
  if (enabled)
    return 1;
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 50))
    return 1;
  CHECK(cudaGraphNodeSetEnabled(exec, kernel, 1));
  cudaGraphNode_t added_node;
  CHECK(cudaGraphAddEmptyNode(&added_node, graph, &leaf, 1));
  mp.value = 60;
  CHECK(cudaGraphMemsetNodeSetParams(fill, &mp));
  EXPECT(cudaGraphExecUpdate(exec, graph, &error, &update_result),
         cudaErrorGraphExecUpdateFailure);
  if (update_result != cudaGraphExecUpdateErrorTopologyChanged)
    return 1;
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 51))
    return 1;
  mp.value = 50;
  CHECK(cudaGraphMemsetNodeSetParams(fill, &mp));
  CHECK(cudaGraphDestroyNode(added_node));
  int serial_input = 19, serial_output = 0;
  CHECK(cudaMemcpyAsync(b, &serial_input, 4, cudaMemcpyHostToDevice, stream));
  CHECK(cudaMemcpyAsync(&serial_output, b, 4, cudaMemcpyDeviceToHost, stream));
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaGraphLaunch(exec, other));
  CHECK(cudaStreamSynchronize(other));
  if (serial_output != serial_input)
    return 1;
  cudaGraph_t clone;
  cudaGraphNode_t cloned_kernel;
  CHECK(cudaGraphClone(&clone, graph));
  CHECK(cudaGraphNodeFindInClone(&cloned_kernel, kernel, clone));
  CHECK(cudaGraphKernelNodeGetParams(cloned_kernel, &kg));
  if (*(void **)kg.kernelParams[0] != a)
    return 1;
  cudaGraphExec_t clone_exec;
  CHECK(cudaGraphInstantiateWithFlags(&clone_exec, clone, 0));
  CHECK(cudaGraphLaunch(clone_exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  if (read_value(b, 51))
    return 1;
  EXPECT(cudaGraphExecUpdate(exec, clone, &error, &update_result),
         cudaErrorNotSupported);
  CHECK(cudaGraphExecDestroy(clone_exec));
  CHECK(cudaGraphDestroy(clone));
  CHECK(cudaGraphDestroyNode(leaf));
  EXPECT(cudaGraphNodeGetType(leaf, &type), cudaErrorInvalidResourceHandle);
  cudaGraphExec_t second;
  CHECK(cudaGraphInstantiateWithFlags(&second, graph, 0));
  CHECK(cudaGraphExecDestroy(second));
  EXPECT(cudaGraphNodeSetEnabled(exec, root, 0), cudaErrorInvalidValue);
  EXPECT(cudaGraphAddEmptyNode(&leaf, graph, &root, 65), cudaErrorInvalidValue);
  cudaGraph_t foreign;
  cudaGraphNode_t outsider;
  CHECK(cudaGraphCreate(&foreign, 0));
  CHECK(cudaGraphAddEmptyNode(&outsider, foreign, NULL, 0));
  EXPECT(cudaGraphAddDependencies(graph, &outsider, &copy, 1),
         cudaErrorInvalidValue);
  CHECK(cudaGraphDestroy(foreign));
  CHECK(cudaGraphDestroy(graph));
  EXPECT(cudaGraphNodeGetType(root, &type), cudaErrorInvalidResourceHandle);
  CHECK(cudaGraphLaunch(exec, other));
  CHECK(cudaStreamSynchronize(other));
  if (read_value(b, 51))
    return 1;
  CHECK(cudaGraphExecDestroy(exec));
  EXPECT(cudaGraphLaunch(exec, stream), cudaErrorInvalidResourceHandle);
  /* Event nodes preserve the existing hidden-transfer frontier. */
  cudaEvent_t ready, done;
  CHECK(cudaEventCreateWithFlags(&ready, cudaEventDisableTiming));
  CHECK(cudaEventCreateWithFlags(&done, cudaEventDisableTiming));
  int event_input = 27, event_output = 0;
  CHECK(cudaMemcpyAsync(b, &event_input, 4, cudaMemcpyHostToDevice, other));
  CHECK(cudaMemcpyAsync(&event_output, b, 4, cudaMemcpyDeviceToHost, other));
  CHECK(cudaEventRecord(ready, other));
  CHECK(cudaGraphCreate(&graph, 0));
  CHECK(cudaGraphAddEventWaitNode(&root, graph, NULL, 0, ready));
  CHECK(cudaGraphAddMemsetNode(&fill, graph, &root, 1, &mp));
  CHECK(cudaGraphAddEventRecordNode(&leaf, graph, &fill, 1, done));
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaEventSynchronize(done));
  if (event_output != event_input)
    return 1;
  CHECK(cudaEventDestroy(ready));
  EXPECT(cudaGraphLaunch(exec, stream), cudaErrorInvalidResourceHandle);
  CHECK(cudaGraphExecDestroy(exec));
  CHECK(cudaGraphDestroy(graph));
  CHECK(cudaEventDestroy(done));
  /* Native capture defers work and returns the same inspectable graph model. */
  int initial = 10;
  CHECK(cudaMemcpy(a, &initial, 4, cudaMemcpyHostToDevice));
  enum cudaStreamCaptureStatus capture_status;
  unsigned long long capture_id = 0;
  CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
  pthread_t wrong_thread;
  void *thread_result;
  if (pthread_create(&wrong_thread, NULL, wrong_capture_thread, stream) ||
      pthread_join(wrong_thread, &thread_result))
    return 1;
  if ((uintptr_t)thread_result != cudaErrorStreamCaptureWrongThread)
    return 1;
  CHECK(cudaStreamIsCapturing(stream, &capture_status));
  if (capture_status != cudaStreamCaptureStatusActive)
    return 1;
  CHECK(cudaStreamGetCaptureInfo(stream, &capture_status, &capture_id));
  if (!capture_id)
    return 1;
  CHECK(cudaStreamIsCapturing(other, &capture_status));
  if (capture_status != cudaStreamCaptureStatusNone)
    return 1;
  CHECK(cudaMemsetAsync(a, 0, 4, stream));
  CHECK(cudaLaunchKernel(func, kp.gridDim, kp.blockDim, args, 0, stream));
  CHECK(cudaMemcpyAsync(b, a, 4, cudaMemcpyDeviceToDevice, stream));
  cudaGraph_t active_graph = NULL;
  const cudaGraphNode_t *capture_dependencies = NULL;
  count = 0;
  CHECK(cudaStreamGetCaptureInfo_v2(stream, &capture_status, &capture_id,
                                    &active_graph, &capture_dependencies,
                                    &count));
  if (!active_graph || count != 1 || !capture_dependencies ||
      !capture_dependencies[0])
    return 1;
  CHECK(cudaStreamEndCapture(stream, &graph));
  if (graph != active_graph || read_value(a, 10))
    return 1;
  count = 0;
  CHECK(cudaGraphGetNodes(graph, NULL, &count));
  if (count != 3)
    return 1;
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  for (int i = 0; i < 2; ++i) {
    CHECK(cudaGraphLaunch(exec, stream));
    CHECK(cudaStreamSynchronize(stream));
    if (read_value(b, 1))
      return 1;
  }
  CHECK(cudaGraphExecDestroy(exec));
  CHECK(cudaGraphDestroy(graph));
  CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
  EXPECT(cudaMemcpyAsync(a, &initial, 4, cudaMemcpyHostToDevice, stream),
         cudaErrorStreamCaptureUnsupported);
  CHECK(cudaStreamIsCapturing(stream, &capture_status));
  if (capture_status != cudaStreamCaptureStatusInvalidated)
    return 1;
  EXPECT(cudaStreamEndCapture(stream, &graph),
         cudaErrorStreamCaptureInvalidated);
  if (graph)
    return 1;
  CHECK(cudaMemsetAsync(a, 0x27, 4, stream));
  CHECK(cudaStreamSynchronize(stream));
  int callbacks = 0;
  CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
  EXPECT(cudaLaunchHostFunc(stream, counted_host_function, &callbacks),
         cudaErrorStreamCaptureUnsupported);
  EXPECT(cudaStreamEndCapture(stream, &graph),
         cudaErrorStreamCaptureInvalidated);
  if (callbacks)
    return 1;
  CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
  CHECK(cudaStreamEndCapture(stream, &graph));
  count = 0;
  CHECK(cudaGraphGetNodes(graph, NULL, &count));
  if (count)
    return 1;
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaGraphDestroy(graph));
  CHECK(cudaGraphExecDestroy(exec));
  CHECK(cudaStreamSynchronize(stream));
  /* Pitched three-dimensional device node paths and padding. */
  CHECK(cudaGraphCreate(&graph, 0));
  mp = (struct cudaMemsetParams){a, 16, 0x39, 1, 8, 8};
  CHECK(cudaGraphAddMemsetNode(&fill, graph, NULL, 0, &mp));
  cp = (struct cudaMemcpy3DParms){0};
  cp.srcPtr = (struct cudaPitchedPtr){a, 16, 16, 4};
  cp.dstPtr = (struct cudaPitchedPtr){b, 16, 16, 4};
  cp.srcPos = (struct cudaPos){0, 1, 0};
  cp.dstPos = (struct cudaPos){1, 0, 0};
  cp.extent = (struct cudaExtent){7, 3, 2};
  cp.kind = cudaMemcpyDeviceToDevice;
  CHECK(cudaMemset(b, 0, 256));
  CHECK(cudaGraphAddMemcpyNode(&copy, graph, &fill, 1, &cp));
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  CHECK(cudaGraphLaunch(exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  unsigned char data[256];
  CHECK(cudaMemcpy(data, b, 256, cudaMemcpyDeviceToHost));
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 16; ++x)
        if (data[z * 64 + y * 16 + x] != (y < 3 && x >= 1 && x < 8 ? 0x39 : 0))
          return 1;
  cudaGraph_t captured3d;
  cudaGraphExec_t captured3d_exec;
  CHECK(cudaMemset(b, 0, 256));
  CHECK(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal));
  CHECK(cudaMemcpy3DAsync(&cp, stream));
  CHECK(cudaStreamEndCapture(stream, &captured3d));
  CHECK(cudaMemcpy(data, b, 256, cudaMemcpyDeviceToHost));
  for (int i = 0; i < 256; ++i)
    if (data[i])
      return 1;
  CHECK(cudaGraphInstantiateWithFlags(&captured3d_exec, captured3d, 0));
  CHECK(cudaGraphLaunch(captured3d_exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  CHECK(cudaMemcpy(data, b, 256, cudaMemcpyDeviceToHost));
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 16; ++x)
        if (data[z * 64 + y * 16 + x] != (y < 3 && x >= 1 && x < 8 ? 0x39 : 0))
          return 1;
  CHECK(cudaGraphExecDestroy(captured3d_exec));
  CHECK(cudaGraphDestroy(captured3d));
  /* Disjoint strided regions may share one allocation. */
  cudaGraph_t alias_graph;
  cudaGraphExec_t alias_exec;
  CHECK(cudaGraphCreate(&alias_graph, 0));
  mp = (struct cudaMemsetParams){a, 16, 0x43, 1, 8, 4};
  CHECK(cudaGraphAddMemsetNode(&fill, alias_graph, NULL, 0, &mp));
  cp = (struct cudaMemcpy3DParms){0};
  cp.srcPtr = (struct cudaPitchedPtr){a, 16, 16, 4};
  cp.dstPtr = (struct cudaPitchedPtr){(unsigned char *)a + 8, 16, 16, 4};
  cp.extent = (struct cudaExtent){8, 4, 1};
  cp.kind = cudaMemcpyDeviceToDevice;
  CHECK(cudaGraphAddMemcpyNode(&copy, alias_graph, &fill, 1, &cp));
  CHECK(cudaGraphInstantiateWithFlags(&alias_exec, alias_graph, 0));
  CHECK(cudaGraphLaunch(alias_exec, stream));
  CHECK(cudaStreamSynchronize(stream));
  CHECK(cudaMemcpy(data, a, 64, cudaMemcpyDeviceToHost));
  for (int i = 0; i < 64; ++i)
    if (data[i] != 0x43)
      return 1;
  CHECK(cudaGraphExecDestroy(alias_exec));
  CHECK(cudaGraphDestroy(alias_graph));
  /* Retired allocation identities are rejected before native launch. */
  CHECK(cudaFree(a));
  EXPECT(cudaGraphLaunch(exec, stream), cudaErrorInvalidResourceHandle);
  cudaGraph_t invalid_clone = NULL;
  EXPECT(cudaGraphClone(&invalid_clone, graph), cudaErrorInvalidResourceHandle);
  if (invalid_clone)
    return 1;
  CHECK(cudaGraphExecDestroy(exec));
  CHECK(cudaGraphDestroy(graph));
  CHECK(cudaMalloc(&a, 4));
  CHECK(cudaGraphCreate(&graph, 0));
  CHECK(cudaGraphAddKernelNode(&kernel, graph, NULL, 0, &kp));
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  CHECK(corexRemoteModuleUnload(module));
  EXPECT(cudaGraphLaunch(exec, stream), cudaErrorInvalidResourceHandle);
  CHECK(cudaGraphExecDestroy(exec));
  CHECK(cudaGraphDestroy(graph));
  CHECK(cudaFree(a));
  CHECK(cudaFree(b));
  CHECK(cudaStreamDestroy(stream));
  CHECK(cudaStreamDestroy(other));
  /* Reset closes native graph/capture objects and tombstones client tokens. */
  CHECK(cudaGraphCreate(&graph, 0));
  CHECK(cudaGraphAddEmptyNode(&root, graph, NULL, 0));
  CHECK(cudaGraphInstantiateWithFlags(&exec, graph, 0));
  CHECK(cudaDeviceReset());
  EXPECT(cudaGraphLaunch(exec, 0), cudaErrorInvalidResourceHandle);
  EXPECT(cudaGraphDestroy(graph), cudaErrorInvalidResourceHandle);
  EXPECT(cudaGraphNodeGetType(root, &type), cudaErrorInvalidResourceHandle);
  puts("GRAPH_PUBLIC_INTEGRATION=PASS");
  return 0;
}
#endif
