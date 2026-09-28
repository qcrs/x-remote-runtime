#include <cuda_runtime.h>

#include <stdio.h>
#include <string.h>

static int expect(cudaError_t actual, cudaError_t expected, const char *name)
{
    if (actual == expected)
        return 0;
    printf("M3_S7_PCI_%s=FAIL actual=%d expected=%d\n",
           name, (int)actual, (int)expected);
    return 1;
}

int main(void)
{
    int failures = 0;
    char pci_bus_id[64];
    memset(pci_bus_id, 0x7f, sizeof(pci_bus_id));
    failures += expect(cudaDeviceGetPCIBusId(pci_bus_id, sizeof(pci_bus_id), 0),
                       cudaSuccess, "GET");
    if (strcmp(pci_bus_id, "0000:81:00.0") != 0)
        failures++;

    int device = -1;
    failures += expect(cudaDeviceGetByPCIBusId(&device, pci_bus_id),
                       cudaSuccess, "LOOKUP");
    if (device != 0)
        failures++;

    char one_byte[1] = {'X'};
    failures += expect(cudaDeviceGetPCIBusId(one_byte, sizeof(one_byte), 0),
                       cudaErrorInvalidValue, "SHORT_BUFFER");
    if (one_byte[0] != '\0')
        failures++;
    char zero_length[1] = {'X'};
    failures += expect(cudaDeviceGetPCIBusId(zero_length, 0, 0),
                       cudaErrorInvalidValue, "ZERO_LENGTH");
    if (zero_length[0] != 'X')
        failures++;
    failures += expect(cudaDeviceGetPCIBusId(pci_bus_id, sizeof(pci_bus_id), 1),
                       cudaErrorInvalidDevice, "INVALID_DEVICE");
    failures += expect(cudaDeviceGetPCIBusId(NULL, sizeof(pci_bus_id), 0),
                       cudaErrorInvalidValue, "NULL_OUTPUT");
    failures += expect(cudaDeviceGetPCIBusId(pci_bus_id, -1, 0),
                       cudaErrorInvalidValue, "NEGATIVE_LENGTH");

    failures += expect(cudaDeviceGetByPCIBusId(&device, "0000:ff:ff.0"),
                       cudaErrorInvalidDevice, "UNKNOWN_ID");
    failures += expect(cudaDeviceGetByPCIBusId(&device, ""),
                       cudaErrorInvalidValue, "EMPTY_ID");
    failures += expect(cudaDeviceGetByPCIBusId(NULL, pci_bus_id),
                       cudaErrorInvalidValue, "NULL_DEVICE");
    failures += expect(cudaDeviceGetByPCIBusId(&device, NULL),
                       cudaErrorInvalidValue, "NULL_ID");
    char oversized[34];
    memset(oversized, 'a', sizeof(oversized));
    oversized[33] = '\0';
    failures += expect(cudaDeviceGetByPCIBusId(&device, oversized),
                       cudaErrorInvalidValue, "OVERSIZED_ID");
    char unterminated[33];
    memset(unterminated, 'a', sizeof(unterminated));
    failures += expect(cudaDeviceGetByPCIBusId(&device, unterminated),
                       cudaErrorInvalidValue, "MISSING_NUL");

    if (failures) {
        printf("M3_S7_DEVICE_PCI=FAIL failures=%d\n", failures);
        return 1;
    }
    printf("M3_S7_DEVICE_PCI=PASS bus_id=%s device=%d\n", pci_bus_id, device);
    return 0;
}
