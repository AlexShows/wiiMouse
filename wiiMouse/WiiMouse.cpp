/*************************
WiiMouse.cpp
Authored by Alex Shows
Originally put this together sometime in 2009 based on various wiiMote hacking wikis

In this particular implementation, the debug loop is used to drive the keyboard and mouse.
**************************/

#include "stdafx.h"
#include "Wiimote.h"

int _tmain(int argc, _TCHAR* argv[])
{
	int retCode = 0;
	CWiimote * wiimote_device;
	wiimote_device = new CWiimote();

	if(wiimote_device->mote.connected)
		retCode = wiimote_device->DebugLoop();

	delete wiimote_device;

	return retCode;
}

