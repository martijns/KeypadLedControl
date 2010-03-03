// KeypadLedControl.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <pmpolicy.h>

// From MSDN: http://msdn.microsoft.com/en-us/library/ms838354.aspx
#pragma region Power management defines

// GDI Escapes for ExtEscape()
#define QUERYESCSUPPORT	8
 
// The following are unique to CE
#define GETVFRAMEPHYSICAL	6144
#define GETVFRAMELEN		6145
#define DBGDRIVERSTAT		6146
#define SETPOWERMANAGEMENT	6147
#define GETPOWERMANAGEMENT	6148

typedef struct _VIDEO_POWER_MANAGEMENT {
	ULONG Length;
	ULONG DPMSVersion;
	ULONG PowerState;
} VIDEO_POWER_MANAGEMENT, *PVIDEO_POWER_MANAGEMENT;

typedef enum _VIDEO_POWER_STATE {
	VideoPowerOn = 1,
	VideoPowerStandBy,
	VideoPowerSuspend,
	VideoPowerOff
} VIDEO_POWER_STATE, *PVIDEO_POWER_STATE;

#pragma endregion // Power management defines

int _tmain(int argc, _TCHAR* argv[])
{
	// Create an event for ourselves, so that we can be turned off as well
	HANDLE appEvent = CreateEvent(NULL, TRUE, FALSE, L"KeypadLedControlApp");
	if (appEvent == NULL)
	{
		MessageBox(NULL, L"Couldn't create an event for ourselves", L"Error", MB_OK);
		return 1;
	}

	// If the app is already running, we set the event so that it will close itself.
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		SetEvent(appEvent);
		CloseHandle(appEvent);
		return 1;
	}

	// Get the keyped led event (this one is used for Leo at least)
	HANDLE iconOnEvent = CreateEvent(NULL, TRUE, FALSE, L"IconLedOnEvent");
	if (iconOnEvent == NULL)
	{
		// Oops, error creating the event?
		MessageBox(NULL, L"Couldn't create the 'IconLedOnEvent' keypad light event", L"Error", MB_OK);
		return 1;
	}
	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		// This even didn't exist yet. Let's see if we can try another event (it's IconLedEvent for Rhodium)
		CloseHandle(iconOnEvent);
		iconOnEvent = CreateEvent(NULL, TRUE, FALSE, L"IconLedEvent");
		if (iconOnEvent == NULL)
		{
			MessageBox(NULL, L"Couldn't create the 'IconLedEvent' keypad light event", L"Error", MB_OK);
			return 1;
		}
		if (GetLastError() != ERROR_ALREADY_EXISTS)
		{
			// This event didn't exist either.
			MessageBox(NULL, L"Sorry, this device is not supported it seems", L"Error", MB_OK);
			CloseHandle(iconOnEvent);
			return 1;
		}
	}

	// Get the keypad led event to turn it off
	HANDLE iconOffEvent = CreateEvent(NULL, TRUE, FALSE, L"IconLedOffEvent");
	if (iconOffEvent == NULL)
	{
		// Oops, error creating the event?
		MessageBox(NULL, L"Couldn't create the 'IconLedOffEvent' keypad light event", L"Error", MB_OK);
		return 1;
	}
	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		// This event didn't exist yet. This means this device does not use this event. Which means we will just ignore it and hope
		// the keypad lights goes off automatically when it goes into standby.
		CloseHandle(iconOffEvent);
		iconOffEvent = NULL;
	}

	// Check if the driver supports power management information
	HDC gdc = GetDC(NULL);
	int iESC = GETPOWERMANAGEMENT;
	if (ExtEscape(gdc, QUERYESCSUPPORT, sizeof(int), (LPCSTR)&iESC, 0, NULL) == 0)
	{
		MessageBox(NULL, L"This device does not support checking for LCD state", L"Error", MB_OK);
		CloseHandle(iconOnEvent);
		ReleaseDC(NULL, gdc);
		return 1;
	}

	// Request that we stay in UNATTENDED power mode, so that we keep getting CPU cycles while LCD is turned off
	// We must keep track of unattended mode. It uses a refcount. If we would forget to release, the device would never enter real standby anymore = battery drain.
	bool bUsingUnattendedMode = true;
	BOOL res = PowerPolicyNotify(PPN_UNATTENDEDMODE, TRUE);

	// Variables for the loop. To prevent overhead we create these variables outside the loop.
	VIDEO_POWER_MANAGEMENT pm;
	pm.Length = sizeof(pm);
	int ret;
	while (true)
	{
		// Get current power management information
		ret = ExtEscape(gdc, GETPOWERMANAGEMENT, 0, NULL, pm.Length, (LPSTR)&pm);

		// Check the power state
		if (pm.PowerState == VideoPowerOn)
		{
			// LCD is on, set the event so that the keypad lights go on
			SetEvent(iconOnEvent);

			// Also enable unattended mode if it wasn't already enabled
			if (!bUsingUnattendedMode)
			{
				PowerPolicyNotify(PPN_UNATTENDEDMODE, TRUE);
				bUsingUnattendedMode = true;
			}
		}
		else
		{
			// LCD is either in standby, suspend or off. Set the event to turn keypad lights off (if we can).
			if (iconOffEvent != NULL)
			{
				SetEvent(iconOffEvent);
			}

			// And release unattended mode
			if (bUsingUnattendedMode)
			{
				PowerPolicyNotify(PPN_UNATTENDEDMODE, FALSE);
				bUsingUnattendedMode = false;
			}
		}
		
		// We'll keep on running this app as long as the event hasn't been set (by starting this app again)
		// Use a 1-second delay. This means that:
		// - If the device goes into standby, it may take up to 1 second before the keypad light goes off (i.e. for Rhodium where
		//   keypad is not turned off by the driver).
		// - The device won't stay in unattended mode longer than 1 second, as the LCD power is re-evaluated within this time and
		//   unattended mode is released when LCD is off after setting the event. This prevents power drain.
		if (WaitForSingleObject(appEvent, 1000) != WAIT_TIMEOUT)
		{
			break;
		}
	}

	// Cleanup
	ReleaseDC(NULL, gdc);
	CloseHandle(iconOnEvent);
	CloseHandle(appEvent);
	if (iconOffEvent != NULL)
	{
		CloseHandle(iconOffEvent);
	}
	if (bUsingUnattendedMode)
	{
		PowerPolicyNotify(PPN_UNATTENDEDMODE, FALSE);
		bUsingUnattendedMode = false;
	}
	
	return 0;
}

