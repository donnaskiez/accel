#include "driver.h"

NTSTATUS
WdfDeviceAddCallback(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit);

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT  DriverObject,
            _In_ PUNICODE_STRING RegistryPath);

NTSTATUS
WdfCreateControlDevice(_In_ WDFDRIVER         WdfDriver,
                       _In_ PDEVICE_EXTENSION Extension);

VOID
WdfInternalDeviceIoControl(IN WDFQUEUE   Queue,
                           IN WDFREQUEST Request,
                           IN SIZE_T     OutputBufferLength,
                           IN SIZE_T     InputBufferLength,
                           IN ULONG      IoControlCode);

#ifdef ALLOC_PRAGMA
#        pragma alloc_text(INIT, DriverEntry)
#        pragma alloc_text(PAGE, WdfDeviceAddCallback)
#        pragma alloc_text(PAGE, WdfInternalDeviceIoControl)
#endif

#pragma warning(push)
#pragma warning(disable : 4055)
#pragma warning(disable : 4152)

UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\AccelDriver");
UNICODE_STRING device_link = RTL_CONSTANT_STRING(L"\\DosDevices\\AccelDriver");

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
        WDF_DRIVER_CONFIG config = {0};
        NTSTATUS          status = STATUS_SUCCESS;

        WDF_DRIVER_CONFIG_INIT(&config, WdfDeviceAddCallback);

        status = WdfDriverCreate(DriverObject,
                                 RegistryPath,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &config,
                                 WDF_NO_HANDLE);

        if (!NT_SUCCESS(status))
                DEBUG_ERROR("WdfDriverCreate: 0x%x", status);

        return status;
}

STATIC
VOID
WdfDeviceObjectCleanup(_In_ WDFOBJECT Device)
{
        PAGED_CODE();
}

NTSTATUS
WdfDeviceAddCallback(IN WDFDRIVER Driver, IN PWDFDEVICE_INIT DeviceInit)
{
        UNREFERENCED_PARAMETER(Driver);
        PAGED_CODE();

        DEBUG_LOG("WdfDeviceAddCallback called.");

        NTSTATUS              status     = STATUS_SUCCESS;
        WDF_OBJECT_ATTRIBUTES attributes = {0};
        WDF_IO_QUEUE_CONFIG   io         = {0};
        WDFDEVICE             handle     = NULL;
        PDEVICE_EXTENSION     extension  = NULL;

        WdfFdoInitSetFilter(DeviceInit);
        WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_EXTENSION);

        status = WdfDeviceCreate(&DeviceInit, &attributes, &handle);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfDeviceCreate: code 0x%x", status);
                return status;
        }

        attributes.EvtCleanupCallback = WdfDeviceObjectCleanup;

        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&io, WdfIoQueueDispatchParallel);

        io.EvtIoInternalDeviceControl = WdfInternalDeviceIoControl;

        extension = WdfDeviceWdmGetDeviceObject(handle)->DeviceExtension;

        KeInitializeSpinLock(&extension->lock);

        status = WdfIoQueueCreate(
            handle, &io, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfIoQueueCreate failed 0x%x", status);
                return status;
        }

        status = WdfCreateControlDevice(Driver, extension);

        if (!NT_SUCCESS(status))
                DEBUG_ERROR("WdfCreateControlDevice: code 0x%lx", status);

        return status;
}

STATIC
VOID
WdfDispatchPassthrough(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
{
        NTSTATUS                 status  = STATUS_UNSUCCESSFUL;
        WDF_REQUEST_SEND_OPTIONS options = {0};

        WDF_REQUEST_SEND_OPTIONS_INIT(&options,
                                      WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

        if (!WdfRequestSend(Request, Target, &options)) {
                status = WdfRequestGetStatus(Request);
                DEBUG_ERROR("WdfRequestSend failed: 0x%x", status);
                WdfRequestComplete(Request, status);
        }
}

STATIC
VOID
WdfMouseFilterCallback(IN PDEVICE_OBJECT    DeviceObject,
                       IN PMOUSE_INPUT_DATA InputDataStart,
                       IN PMOUSE_INPUT_DATA InputDataEnd,
                       IN OUT PULONG        InputDataConsumed)
{
        PDEVICE_EXTENSION extension = NULL;
        WDFDEVICE         handle    = NULL;

        handle    = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);
        extension = FilterGetData(handle);

        /* we know we are running at IRQL >= DISPATCH_LEVEL so we can simply
         * acquire the lock - saving us time from checking and potentially
         * raising the IRQL.*/
        KeAcquireSpinLockAtDpcLevel(&extension->lock);

        DEBUG_LOG("irql: %lx", KeGetCurrentIrql());
        DEBUG_LOG("x: %lx, y: %lx", InputDataStart->LastX, InputDataEnd->LastY);

end:
        KeReleaseSpinLockFromDpcLevel(&extension->lock);

        (*(PSERVICE_CALLBACK_ROUTINE)extension->connect_data.ClassService)(
            extension->connect_data.ClassDeviceObject,
            InputDataStart,
            InputDataEnd,
            InputDataConsumed);
}

VOID
WdfInternalDeviceIoControl(IN WDFQUEUE   Queue,
                           IN WDFREQUEST Request,
                           IN SIZE_T     OutputBufferLength,
                           IN SIZE_T     InputBufferLength,
                           IN ULONG      IoControlCode)
{
        UNREFERENCED_PARAMETER(OutputBufferLength);
        UNREFERENCED_PARAMETER(InputBufferLength);
        PAGED_CODE();

        NTSTATUS          status    = STATUS_UNSUCCESSFUL;
        PDEVICE_EXTENSION extension = NULL;
        PCONNECT_DATA     connect   = NULL;
        WDFDEVICE         device    = NULL;
        SIZE_T            length    = 0;

        device    = WdfIoQueueGetDevice(Queue);
        extension = FilterGetData(device);

        switch (IoControlCode) {
        case IOCTL_INTERNAL_MOUSE_CONNECT:

                if (extension->connect_data.ClassService != NULL) {
                        status = STATUS_SHARING_VIOLATION;
                        break;
                }

                status = WdfRequestRetrieveInputBuffer(
                    Request, sizeof(CONNECT_DATA), &connect, &length);

                if (!NT_SUCCESS(status)) {
                        DEBUG_ERROR("WdfRequestRetrieveInputBuffer failed %x",
                                    status);
                        break;
                }

                extension->connect_data = *connect;

                connect->ClassDeviceObject =
                    WdfDeviceWdmGetDeviceObject(device);
                connect->ClassService = WdfMouseFilterCallback;

                break;

        case IOCTL_INTERNAL_MOUSE_DISCONNECT:

                /*
                 * This IO request has not been implemented, however it would be
                 * implemented by clearing the connection parameters in the
                 * CONNECT_DATA structure located in our devices
                 * DEVICE_EXTENSION structure
                 *
                 *
                 * extension->UpperConnectData.ClassDeviceObject = NULL;
                 * extension->UpperConnectData.ClassService = NULL;
                 *
                 */
                status = STATUS_NOT_IMPLEMENTED;

                break;

        default: break;
        }

        if (!NT_SUCCESS(status)) {
                WdfRequestComplete(Request, status);
                return;
        }

        WdfDispatchPassthrough(Request, WdfDeviceGetIoTarget(device));
}

STATIC VOID
WdfExternalDeviceIoControl(_In_ WDFQUEUE   Queue,
                           _In_ WDFREQUEST Request,
                           _In_ SIZE_T     OutputBufferLength,
                           _In_ SIZE_T     InputBufferLength,
                           _In_ ULONG      IoControlCode)
{
        UNREFERENCED_PARAMETER(Queue);
        UNREFERENCED_PARAMETER(OutputBufferLength);
        UNREFERENCED_PARAMETER(InputBufferLength);

        PAGED_CODE();

        NTSTATUS          status        = STATUS_UNSUCCESSFUL;
        PDEVICE_EXTENSION extension     = NULL;
        WDFOBJECT         device_object = NULL;
        WDFDEVICE         device        = NULL;
        PVOID             buffer        = NULL;
        SIZE_T            buffer_size   = 0;

        DEBUG_LOG("WdfExternalDeviceIoControl called with control code: %lx",
                  IoControlCode);

        device    = WdfIoQueueGetDevice(Queue);
        extension = WdfDeviceWdmGetDeviceObject(device);

        switch (IoControlCode) {
        case UPDATE_DRIVER_CONFIGURATION:
                DEBUG_LOG("IOCTL UPDATE_DRIVER_CONFIGURATION received");

                // status = WdfRequestRetrieveInputBuffer(
                //     Request,
                //     sizeof(DEVICE_CONFIGURATION_OPTIONS),
                //     &buffer,
                //     &buffer_size);

                // if (!NT_SUCCESS(status)) {
                //         DEBUG_ERROR("WdfRequestRetrieveInputBuffer: code
                //         0x%x",
                //                     status);
                //         break;
                // }

                // PDEVICE_CONFIGURATION_OPTIONS device_options =
                //     (PDEVICE_CONFIGURATION_OPTIONS)buffer;

                break;
        default:
                DEBUG_ERROR(
                    "WdfExternalDeviceIoControl received invalid IOCTL code");
                status = STATUS_INVALID_DEVICE_REQUEST;
        }

        /* Complete the request */
        WdfRequestCompleteWithInformation(Request, status, NULL);
}

NTSTATUS
WdfCreateControlDevice(_In_ WDFDRIVER         WdfDriver,
                       _In_ PDEVICE_EXTENSION Extension)
{
        PAGED_CODE();

        NTSTATUS              status            = STATUS_UNSUCCESSFUL;
        PWDFDEVICE_INIT       device_init       = NULL;
        WDFDEVICE             device            = NULL;
        WDF_OBJECT_ATTRIBUTES device_attributes = {0};
        WDF_IO_QUEUE_CONFIG   queue_config      = {0};
        WDFQUEUE              queue             = NULL;

        device_init = WdfControlDeviceInitAllocate(
            WdfDriver, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);

        if (!device_init)
                return STATUS_INSUFFICIENT_RESOURCES;

        WdfDeviceInitSetExclusive(device_init, FALSE);

        status = WdfDeviceInitAssignName(device_init, &device_name);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfDeviceInitAssignName: %x", status);
                WdfDeviceInitFree(device_init);
                return status;
        }

        status =
            WdfDeviceCreate(&device_init, WDF_NO_OBJECT_ATTRIBUTES, &device);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfDeviceCreate: %x", status);
                WdfDeviceInitFree(device_init);
                return status;
        }

        status = WdfDeviceCreateSymbolicLink(device, &device_link);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfDeviceCreateSymbolicLink: %x", status);
                WdfDeviceInitFree(device_init);
                WdfObjectDelete(device);
                return status;
        }

        WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config,
                                               WdfIoQueueDispatchSequential);

        queue_config.EvtIoDeviceControl = WdfExternalDeviceIoControl;

        status = WdfIoQueueCreate(
            device, &queue_config, WDF_NO_OBJECT_ATTRIBUTES, &queue);

        if (!NT_SUCCESS(status)) {
                DEBUG_ERROR("WdfIoQueueCreate: %x", status);
                WdfDeviceInitFree(device_init);
                WdfObjectDelete(device);
                return status;
        }

        Extension->control_device = device;
        WdfControlFinishInitializing(device);
        return status;
}

#pragma warning(pop)
