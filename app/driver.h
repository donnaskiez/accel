#ifndef IDRIVER_H
#define IDRIVER_H

#include <windows.h>
#include <iostream>

#define UPDATE_DRIVER_CONFIGURATION CTL_CODE(FILE_DEVICE_MOUSE, 0x8000, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct DEVICE_CONFIGURATION_OPTIONS
{
	INT AccelMultiplier;
	INT Enabled;
};

class DriverInterface
{
	HANDLE device_handle;
	LPCWSTR device_name;

public:
	DriverInterface( LPCWSTR DeviceName );
	void UpdateAccelConfiguration( DEVICE_CONFIGURATION_OPTIONS* ConfigurationOptions );
};

#endif // !IDRIVER_H
