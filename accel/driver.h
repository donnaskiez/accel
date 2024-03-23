#ifndef MOUFILTER_H
#define MOUFILTER_H

#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <wdf.h>

#define DEBUG_LOG(fmt, ...) \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "[+] " fmt "\n", ##__VA_ARGS__)
#define DEBUG_ERROR(fmt, ...) \
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0, "[-] " fmt "\n", ##__VA_ARGS__)

#define STATIC static

#define UPDATE_DRIVER_CONFIGURATION \
        CTL_CODE(FILE_DEVICE_MOUSE, 0x8000, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _DEVICE_EXTENSION {
        KSPIN_LOCK   lock;
        CONNECT_DATA connect_data;
        WDFDEVICE    control_device;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, FilterGetData)

#endif
