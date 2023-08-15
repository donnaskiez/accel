#include "driver.h"

int main( void )
{
	// ERROR symbolic link is not appoearing in winobj fuck sakesss
	LPCWSTR name = L"\\DosDevices\\AccelDriver";
	DriverInterface driver( name );
	
	DEVICE_CONFIGURATION_OPTIONS options;
	options.AccelMultiplier = 5;
	options.Status = 1;
	options.SensitivityMultiplier = 0;
	driver.UpdateAccelConfiguration( &options );

	while ( 1 )
	{
		
	}
}