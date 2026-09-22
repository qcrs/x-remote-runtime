#ifndef COREX_RUNTIME_CONTEXT_H
#define COREX_RUNTIME_CONTEXT_H

#include "corex_remote_cuda.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define G6A_MAX_ALLOCS              1024
#define G6C_MAX_STREAM_HANDLES      1024
#define G6E_FRONTIER_SLOTS         (G6C_MAX_STREAM_HANDLES + 1)
#define G6E_DEFAULT_STREAM_SLOT    G6C_MAX_STREAM_HANDLES
#define G6C_MAX_EVENT_HANDLES       1024
#define G6C_MAX_HIDDEN_TRANSFERS    2048
#define G6D_MAX_MODULE_HANDLES      128
#define G6D_MAX_KERNEL_HANDLES      512
#define G6D_MAX_KERNEL_ARGS         32
#define G7C_MAX_FATBIN_REGISTRATIONS 128

typedef struct {
    int live;
    uintptr_t virtual_base;
    size_t size;
    uint64_t allocation_id;
} VirtualAllocation;

struct corexCudaStreamHandle {
    uint64_t cookie;
    uint64_t stream_id;
    int live;
    size_t slot_index;
    uint64_t next_transfer_seq;
    uint64_t *frontier;
};

struct corexCudaEventHandle {
    uint64_t cookie;
    uint64_t event_id;
    int live;
    int recorded;
    uint64_t *frontier;
};

struct corexRemoteModuleHandle {
    uint64_t cookie;
    uint64_t module_id;
    int live;
};

typedef enum {
    G7D_KERNEL_ORIGIN_CONTROLLED = 1,
    G7D_KERNEL_ORIGIN_COMPILER = 2,
} G7DKernelOrigin;

typedef enum {
    G7D_ABI_UNKNOWN = 0,
    G7D_ABI_READY = 1,
    G7D_ABI_FAILED = 2,
} G7DAbiState;

typedef struct corexRemoteKernelHandle {
    uint64_t cookie;
    uint64_t kernel_id;
    uint64_t module_id;
    int live;

    G7DKernelOrigin origin;
    G7DAbiState abi_state;

    const void *host_fun;
    uint64_t registration_generation;

    size_t argc;
    corexRemoteKernelArgDesc args[G6D_MAX_KERNEL_ARGS];
    char name[128];
} corexRemoteKernelHandle;

typedef enum {
    G7C_REG_EMPTY = 0,
    G7C_REG_REGISTERING,
    G7C_REG_REMOTE_READY,
    G7C_REG_LIVE,
    G7C_REG_FAILED,
    G7C_REG_UNLOAD_FAILED,
    G7C_REG_DEAD,
} G7CRegistrationState;

typedef struct {
    uint64_t cookie;
    uint64_t generation;
    G7CRegistrationState state;

    const void *compiler_wrapper;
    const unsigned char *fatbin_ptr;
    size_t fatbin_size;
    const unsigned char *image_ptr;
    size_t image_size;
    size_t image_offset;

    corexRemoteModule_t remote_module;
    uint64_t module_id_snapshot;

    cudaError_t registration_error;
    int extraction_error;
    unsigned function_registration_calls;
} G7CFatbinRegistration;

typedef enum {
    HIDDEN_TRANSFER_H2D = 1,
    HIDDEN_TRANSFER_D2H = 2,
} HiddenTransferKind;

typedef struct {
    int live;
    uint64_t transfer_id;
    HiddenTransferKind kind;
    uint64_t allocation_id;
    size_t origin_stream_slot;
    uint64_t origin_stream_seq;
    uint64_t submit_order;
    void *host_dst;
    size_t bytes;
} HiddenTransfer;

/*
 * One process owns one client RuntimeContext in M1-S1.  The fields below are
 * the former process-global mutable Runtime/RPC state.  TLS error/device and
 * compiler launch configuration state intentionally remain outside this object.
 */
typedef struct CorexRuntimeContext {
    pthread_mutex_t mutex;

    int fd;
    uint32_t next_req_id;

    unsigned char *va_arena;
    size_t va_next;
    VirtualAllocation allocs[G6A_MAX_ALLOCS];

    struct corexCudaStreamHandle stream_handles[G6C_MAX_STREAM_HANDLES];
    struct corexCudaEventHandle event_handles[G6C_MAX_EVENT_HANDLES];
    HiddenTransfer hidden_transfers[G6C_MAX_HIDDEN_TRANSFERS];
    struct corexRemoteModuleHandle module_handles[G6D_MAX_MODULE_HANDLES];
    corexRemoteKernelHandle kernel_handles[G6D_MAX_KERNEL_HANDLES];
    G7CFatbinRegistration registrations[G7C_MAX_FATBIN_REGISTRATIONS];

    size_t stream_handle_next;
    size_t event_handle_next;
    size_t module_handle_next;
    size_t kernel_handle_next;
    size_t registration_next;
    uint64_t registration_generation_next;
    uint64_t transfer_submit_order;

    uint64_t *default_frontier;
    uint64_t default_next_transfer_seq;
    int shutdown_registered;
} CorexRuntimeContext;

CorexRuntimeContext *corex_runtime_context_get(void);
void corex_runtime_context_lock(CorexRuntimeContext *context);
void corex_runtime_context_unlock(CorexRuntimeContext *context);

#ifdef __cplusplus
}
#endif

#endif
