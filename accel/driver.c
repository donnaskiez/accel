#include "driver.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, WdfDeviceAddCallback)
#pragma alloc_text (PAGE, WdfInternalDeviceIoControl)
#endif

#pragma warning(push)
#pragma warning(disable:4055)
#pragma warning(disable:4152)

/*
* Standard driver entry routine which will be the first thing to run in our driver.
* We initialise our WDF configuration and create the WDF framework driver object.
* From here, the PnP manager will run WdfDeviceAddCallback to initiate our filter driver.
*/
NTSTATUS DriverEntry(
    _In_  PDRIVER_OBJECT  DriverObject,
    _In_  PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status = STATUS_SUCCESS;

    DEBUG_LOG( "DriverEntry called" );

    /* Initialise our drivers WDF_DRIVER_CONFIG structure */
    WDF_DRIVER_CONFIG_INIT(
        &config,
        WdfDeviceAddCallback
    );

    /* Create a framework driver object for our driver */
    status = WdfDriverCreate( 
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE 
    );

    if ( !NT_SUCCESS( status ) )
        DEBUG_ERROR( "WdfDriverCreate failed with status 0x%x", status );

    return status;
}

/*
* This function is called by the PnP manager and is where we configure 
* the properties of our device. In our case we set our device to be a 
* filter driver. We also configure our IO queue properties to use the 
* parallel option and then initiate our queue. We also set our IO request 
* callback function.
*/
NTSTATUS WdfDeviceAddCallback(
    IN WDFDRIVER Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER( Driver );

    /* Ensure we are running at an IRQL that allows paging */
    PAGED_CODE();

    DEBUG_LOG( "WdfDeviceAddCallback called" );

    WDF_OBJECT_ATTRIBUTES device_attributes;
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device_handle;
    WDF_IO_QUEUE_CONFIG io_queue_config;

    /* Tell the framework we are a filter device rather then a function device */
    WdfFdoInitSetFilter( DeviceInit );

    /* tell the framework our device type is of type mouse */
    WdfDeviceInitSetDeviceType( DeviceInit, FILE_DEVICE_MOUSE );

    /* Initialise our drivers WDF_OBJECT_ATTRIBUTES structure and insert our driver defined context */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE( &device_attributes, DEVICE_EXTENSION );

    /* Create our device */
    status = WdfDeviceCreate( &DeviceInit, &device_attributes, &device_handle );

    if ( !NT_SUCCESS( status ) ) 
    {
        DEBUG_ERROR( "WdfDeviceCreate failed with status code 0x%x", status );
        return status;
    }

    /* Initialize our drivers WDF_IO_QUEUE_CONFIG structure with dispatch type parallel*/
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE( 
        &io_queue_config,
        WdfIoQueueDispatchParallel 
    );

    /* register our IO control callback function */
    io_queue_config.EvtIoInternalDeviceControl = WdfInternalDeviceIoControl;

    /* Create our IO queue for this device */
    status = WdfIoQueueCreate( 
        device_handle,
        &io_queue_config,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
    );

    if ( !NT_SUCCESS( status ) )
    {
        DEBUG_ERROR( "WdfIoQueueCreate failed 0x%x", status );
        return status;
    }

    /* Create our IO control device (seperate from the internal device IO control) */
    status = status = WdfCreateControlDevice( Driver );

    if ( !NT_SUCCESS( status ) )
        DEBUG_ERROR( "WdfCreateControlDevice failed with status code 0x%lx", status );

    return status;
}

/*
* This function initializes our drivers WDF_REQUEST_SEND_OPTIONS structure
* with the value WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET meaning our IO target
* will know that we do not care about the completition or cancellation of this IO request
*/
VOID WdfDispatchPassthrough(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target
)
{
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN request_status;
    NTSTATUS status = STATUS_SUCCESS;

    /* 
    * Indicate via WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET that we do not care about 
    * being notified if the request is completed or cancelled 
    */
    WDF_REQUEST_SEND_OPTIONS_INIT( 
        &options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET 
    );

    /* Send the IO request down the stack to the target */
    request_status = WdfRequestSend( Request, Target, &options );

    if ( request_status == FALSE ) 
    {
        status = WdfRequestGetStatus( Request );
        DEBUG_ERROR( "WdfRequestSend failed: 0x%x", status );
        WdfRequestComplete( Request, status );
    }
}

/*
* In response to an IRP_MJ_READ request, Mouclass will transfer 0 or more MOUSE_INPUT_DATA
* structures from the internal data queue to the Win32 subsystem buffer. From here, we can 
* filter that data and then return it back to the system buffer.
* 
* https://learn.microsoft.com/en-us/previous-versions/ff542215(v=vs.85)
*/
VOID WdfMouseFilterCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
)
{
    PDEVICE_EXTENSION device_extension;
    WDFDEVICE device_handle;
    INT packet_count = 0;

    /* Get our device's handle and device extension structure*/
    device_handle = WdfWdmDeviceGetWdfDeviceHandle( DeviceObject );
    device_extension = FilterGetData( device_handle );

    packet_count = InputDataEnd - InputDataStart;

    INT64 current_tick = KeQueryPerformanceCounter( NULL ).QuadPart;
    INT64 tick_delta = current_tick - device_extension->PreviousTick;
    INT64 average_tick_time = tick_delta / packet_count;

    device_extension->PreviousTick = current_tick;

    LONG x_distance = InputDataEnd->LastX - InputDataStart->LastX;
    LONG y_distance = InputDataEnd->LastY - InputDataStart->LastY;
    LONG total_distance = x_distance * x_distance + y_distance * y_distance;

    /* 
    * need to link LIBCNTPR to use library math functions 
    * Need to save registers using the 2 functions below whilst doing any floating point operations
    * 
    * https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/floating-point-support-for-64-bit-drivers
    */

    LONG absolute_distance = MySqrt( total_distance );

    LONG speed = absolute_distance / ( average_tick_time * NUM_TICKS_PER_MS );

    DEBUG_LOG( "Absolute distance: %lx, total distance: %lx, x distance: %lx, y distance: %lx",
        absolute_distance, 
        total_distance, 
        x_distance, 
        y_distance
    );

    /* Iterate through all packets */
    //for ( PMOUSE_INPUT_DATA InputData = InputDataStart; InputData < InputDataEnd; InputData++ )
    //{
    //    DEBUG_LOG( "InputDataX Start: %lx, InputDataY End: %lx", InputData->LastX, InputData->LastY );
    //}

    /* now that we are done processing the IO packet, pass the data to the class data queue */
    ( *( PSERVICE_CALLBACK_ROUTINE )device_extension->UpperConnectData.ClassService )(
        device_extension->UpperConnectData.ClassDeviceObject,
        InputDataStart,
        InputDataEnd,
        InputDataConsumed
        );
}

/*
* DeviceIOControl routine which will handle the internal device stack IO requests.
*/
VOID WdfInternalDeviceIoControl(
    IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t OutputBufferLength,
    IN size_t InputBufferLength,
    IN ULONG IoControlCode
)
{
    UNREFERENCED_PARAMETER( OutputBufferLength );
    UNREFERENCED_PARAMETER( InputBufferLength );

    /* Ensure we are running at an IRQL that allows paging */
    PAGED_CODE();

    PDEVICE_EXTENSION device_extension;
    PCONNECT_DATA connect_data;
    PINTERNAL_I8042_HOOK_MOUSE mouse_hook;
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device_handle;
    size_t length;

    /* 
    * Get a handle to our device. No need to check for null here
    * since if the handle is invalid WdfIoQueueGetDevice will issue
    * a bug check anyway
    */
    device_handle = WdfIoQueueGetDevice( Queue );

    /* Retrieve our devices DEVICE_EXTENSION structure */
    device_extension = FilterGetData( device_handle );

    switch ( IoControlCode ) 
    {
    case IOCTL_INTERNAL_MOUSE_CONNECT:

        /* Only allow one connection*/
        if ( device_extension->UpperConnectData.ClassService != NULL ) 
        {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        /* Retrieve the input buffer for this IO request */
        status = WdfRequestRetrieveInputBuffer( 
            Request,
            sizeof( CONNECT_DATA ),
            &connect_data,
            &length 
        );

        if ( !NT_SUCCESS( status ) ) 
        {
            DEBUG_ERROR( "WdfRequestRetrieveInputBuffer failed %x", status );
            break;
        }

        /* store our connect data in our device extension structure */
        device_extension->UpperConnectData = *connect_data;

        /* 
        * The CONNECT_DATA structure is used to store information that the Kbdclass and
        * Mouclass use to connect to a keyboard or mouse port. The ClassDeviceObject stores
        * the device handle that will connect and the ClassService will store a pointer 
        * to the callback function that will be executed during each IO packet
        */
        connect_data->ClassDeviceObject = WdfDeviceWdmGetDeviceObject( device_handle );
        connect_data->ClassService = WdfMouseFilterCallback;

        break;

    case IOCTL_INTERNAL_MOUSE_DISCONNECT:

        /*
        * This IO request has not been implemented, however it would be implemented
        * by clearing the connection parameters in the CONNECT_DATA structure
        * located in our devices DEVICE_EXTENSION structure
        * 
        * 
        * device_extension->UpperConnectData.ClassDeviceObject = NULL;
        * device_extension->UpperConnectData.ClassService = NULL;
        * 
        */
        status = STATUS_NOT_IMPLEMENTED;

        break;

    default:
        break;
    }

    if ( !NT_SUCCESS( status ) ) 
    {
        WdfRequestComplete( Request, status );
        return;
    }
    
    /* 
    * If we get here it means we are not interested in this request, hence pass 
    * down the device stack and do no further processing. This device sits below the 
    * mouclass device and above the mouhid device hence we only see IO requests
    * that mouclass sends on to the devices beneath them in the stack. This is because
    * IO requests start at the top of the stack with functional device objects and move
    * there way down the stack towards physical device objects. Read below for more info.
    * 
    * https://www.osr.com/nt-insider/2020-issue1/a-generic-device-class-filter-using-wdf/
    */
    WdfDispatchPassthrough( Request, WdfDeviceGetIoTarget( device_handle ) );
}

/*
* This function will handle our external device IO requests that interface with outside 
* applications i.e requests that do not come from inside the device stack
*/
VOID WdfExternalDeviceIoControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ SIZE_T OutputBufferLength,
    _In_ SIZE_T InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    UNREFERENCED_PARAMETER( Queue );
    UNREFERENCED_PARAMETER( OutputBufferLength );
    UNREFERENCED_PARAMETER( InputBufferLength );

    PAGED_CODE();

    DEBUG_LOG( "WdfExternalDeviceIoControl called with control code: %lx", IoControlCode );

    /* Complete the request */
    WdfRequestCompleteWithInformation(
        Request,
        STATUS_SUCCESS,
        NULL
    );
}

/*
* This routine creates a control device for our driver that allows us to communicate
* with our device without having to send our IOCTL through the entire stack. An example
* is from a user mode application sent directly to our device. 
*/
NTSTATUS WdfCreateControlDevice(
    _In_ WDFDRIVER WdfDriver
)
{
    PAGED_CODE();

    PWDFDEVICE_INIT wdf_device_init = NULL;
    WDFDEVICE control_device = NULL;
    WDF_OBJECT_ATTRIBUTES control_device_attributes;
    WDF_IO_QUEUE_CONFIG io_queue_config;
    BOOLEAN create_status = FALSE;
    NTSTATUS status = STATUS_SUCCESS;
    WDFQUEUE queue;

    /* Constants that will be used to allow us to identify our device */
    DECLARE_CONST_UNICODE_STRING( device_control_name, DEVICE_CONTROL_NAME_STRING );
    DECLARE_CONST_UNICODE_STRING( device_control_symbolic_link, DEVICE_CONTROL_SYMBOLIC_LINK );

    DEBUG_LOG( "WdfCreateControlDevice called" );

    /* Allocate a control device for our driver */
    wdf_device_init = WdfControlDeviceInitAllocate(
        WdfDriver,
        &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
    );

    if ( !wdf_device_init )
        goto error;

    /* 
    * Set our device exclusivity to false meaning we can have multiple handles 
    * open to our device at once
    */
    WdfDeviceInitSetExclusive( wdf_device_init, FALSE );

    /* assign our control device name */
    status = WdfDeviceInitAssignName(
        wdf_device_init,
        &device_control_name
    );

    if ( !NT_SUCCESS( status ) )
        goto error;

    /* create our control device */
    status = WdfDeviceCreate(
        &wdf_device_init,
        WDF_NO_OBJECT_ATTRIBUTES,
        &control_device
    );

    if ( !NT_SUCCESS( status ) )
        goto error;

    /* create our control device symbolic link */
    status = WdfDeviceCreateSymbolicLink(
        control_device,
        &device_control_symbolic_link
    );

    if ( !NT_SUCCESS( status ) )
        goto error;

    /* create our control device sequential queue (FIFO) */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &io_queue_config,
        WdfIoQueueDispatchSequential
    );

    /* Assign our IO device control callback function */
    io_queue_config.EvtIoDeviceControl = WdfExternalDeviceIoControl;

    /* Create our queue */
    status = WdfIoQueueCreate(
        control_device,
        &io_queue_config,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
    );

    if ( !NT_SUCCESS( status ) )
        goto error;

    /* notify the framework that we have finished initialising our device */
    WdfControlFinishInitializing( control_device );

    return STATUS_SUCCESS;

error:

    if ( wdf_device_init )
        WdfDeviceInitFree( wdf_device_init );

    if ( control_device )
        WdfObjectDelete( control_device );

    DEBUG_ERROR( "WdfCreateControlDevice failed with status code 0x%x", status );

    return status;
}

#pragma warning(pop)
