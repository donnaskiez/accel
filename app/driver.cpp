#include "driver.h"

#include <iostream>

DriverInterface::DriverInterface( LPCWSTR DeviceName )
{
	this->device_name = DeviceName;
	this->device_handle = CreateFileW(
		DeviceName,
		GENERIC_WRITE | GENERIC_READ | GENERIC_EXECUTE,
		NULL,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
		NULL
	);

	std::cout << this->device_handle << std::endl;
	std::cout << GetLastError() << std::endl;
}

void DriverInterface::UpdateAccelConfiguration( DEVICE_CONFIGURATION_OPTIONS* ConfigurationOptions )
{
	BOOL status = DeviceIoControl(
		this->device_handle,
		UPDATE_DRIVER_CONFIGURATION,
		ConfigurationOptions,
		sizeof( DEVICE_CONFIGURATION_OPTIONS ),
		NULL,
		NULL,
		NULL,
		( LPOVERLAPPED )NULL
	);

	if ( status )
		std::cout << "Failed to send IOCTL to device" << std::endl;
}
