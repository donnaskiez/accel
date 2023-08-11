#ifndef MOUFILTER_H
#define MOUFILTER_H

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <wdf.h>

#define DEBUG_LOG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "[+] " fmt "\n", ##__VA_ARGS__)
#define DEBUG_ERROR(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "[-] " fmt "\n", ##__VA_ARGS__)

typedef struct _DEVICE_EXTENSION
{                           
    PVOID UpperContext;
    PI8042_MOUSE_ISR UpperIsrHook;
    IN PI8042_ISR_WRITE_PORT IsrWritePort;
    IN PVOID CallContext;
    IN PI8042_QUEUE_PACKET QueueMousePacket;
    CONNECT_DATA UpperConnectData;

} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    DEVICE_EXTENSION,
    FilterGetData
)

#define DEVICE_CONTROL_NAME_STRING L"\\Device\\AccelDriver"
#define DEVICE_CONTROL_SYMBOLIC_LINK L"\\DosDevices\\AccelDriver"

UNICODE_STRING device_control_name;
UNICODE_STRING device_control_symbolic_link;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD WdfDeviceAddCallback;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL WdfInternalDeviceIoControl;

NTSTATUS WdfCreateControlDevice(
    _In_ WDFDRIVER WdfDriver
);

#endif


