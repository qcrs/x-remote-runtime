#ifndef COREX_DEVICE_INFO_H
#define COREX_DEVICE_INFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COREX_REMOTE_DEVICE_INFO_DTO_VERSION 1u
#define COREX_REMOTE_DEVICE_NAME_BYTES 256u

typedef struct CorexRemoteDeviceInfo {
    char name[COREX_REMOTE_DEVICE_NAME_BYTES];

    uint64_t total_global_mem;
    uint64_t free_mem;
    uint64_t shared_mem_per_block;

    uint32_t regs_per_block;
    uint32_t warp_size;
    uint64_t mem_pitch;

    uint32_t max_threads_per_block;
    uint32_t max_threads_dim[3];
    uint32_t max_grid_size[3];

    uint32_t clock_rate;
    uint64_t total_const_mem;

    uint32_t major;
    uint32_t minor;

    uint64_t texture_alignment;

    uint32_t device_overlap;
    uint32_t multi_processor_count;
    uint32_t kernel_exec_timeout_enabled;
    uint32_t integrated;
    uint32_t can_map_host_memory;
    uint32_t compute_mode;
    uint32_t concurrent_kernels;
    uint32_t ecc_enabled;
    uint32_t pci_bus_id;
    uint32_t pci_device_id;
    uint32_t tcc_driver;
    uint32_t memory_clock_rate;
    uint32_t memory_bus_width;
    uint32_t l2_cache_size;
    uint32_t max_threads_per_multiprocessor;
    uint32_t async_engine_count;
    uint32_t unified_addressing;
} CorexRemoteDeviceInfo;

/*
 * Hidden implementation ABI.
 * These symbols are linked inside libcorex_remote_cudart and hidden by the
 * version script; applications never see them.
 */
int corexRemoteGetDeviceInfoInternal(
    uint32_t logical_device,
    CorexRemoteDeviceInfo *info_out);

int corexRemoteRecordErrorInternal(int error_code);

#ifdef __cplusplus
}
#endif

#endif
