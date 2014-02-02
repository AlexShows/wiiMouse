/*************************
wiiMote.cpp
Authored by Alex Shows
Originally put this together sometime in 2009 based on various wiiMote hacking wikis

The intent here is to connect to the wiimote, initialize it, and send/receive packets.
The packet protocols are available online at various sites, and you can find much of the
magic numbers in the header file wiimote.h.

In this particular implementation, the debug loop is used to drive the keyboard and mouse.

NOTE: You'll need to point to the location of your hid.lib and setupapi.lib from the DDK.
	See WiiMote.h for more information.

TODO: This should use more STL, exception handling, and other best practices. It was used
	as a quick demo and hack while exploring the use of Bluetooth HID devices.

**************************/

#include "StdAfx.h"
#include "Wiimote.h"



CWiimote::CWiimote(void)
{
	/* initialize vars and then go find the device */
	memset(sManuf, 0, WM_STRING_SIZE);
	memset(sProd, 0, WM_STRING_SIZE);
	
	ClearPackets();
	rdPkt.success = wrPkt.success = false;
	mote.connected = mote.chuk.connected = false;
	mote.rumbling = false;
	mote.button.a = mote.button.b = mote.button.home = mote.button.minus = mote.button.one = mote.button.plus = mote.button.two = false;
	mote.dpad.down = mote.dpad.left = mote.dpad.right = mote.dpad.up = false;
	mote.force.x = mote.force.y = mote.force.z = 0.f;
	mote.axis.x = mote.axis.y = mote.axis.z = 0;
	mote.tilt.x = mote.tilt.y = mote.tilt.z = 0.f;
	mote.scale.x = mote.scale.y = mote.scale.z = 0;
	mote.zero.x = mote.zero.y = mote.zero.z = 0;
	disconnect = false; /* Intend to disconnect the Class from the mote, but doesn't explicitely call the destructor */
	mote.battery = 0;

	kbLayout = GetKeyboardLayout(NULL);
	HidD_GetHidGuid(&GUID);
	PnPHandle = AttachToPnP();
	
	if(PnPHandle != INVALID_HANDLE_VALUE)
	{
		HIDHandle = GetDeviceHandle();

		if(HIDHandle != INVALID_HANDLE_VALUE)
		{
			mote.connected = Initialize();
			HidD_GetManufacturerString(HIDHandle, sManuf, WM_STRING_SIZE);
			HidD_GetProductString(HIDHandle, sProd, WM_STRING_SIZE);
		} // end if valid device handle
	} // end if valid PnP handle
}

CWiimote::~CWiimote(void)
{	
	if(HIDHandle != INVALID_HANDLE_VALUE)
		CloseHandle(HIDHandle);
	if(PnPHandle != INVALID_HANDLE_VALUE)
		SetupDiDestroyDeviceInfoList(PnPHandle);

	mote.connected = false;
}

/* Using the GUID, attach to the Plug'n'Play node on the system */
HANDLE CWiimote::AttachToPnP()
{
	/* Attach to the Plug and Play node and get devices */
	HANDLE pnp = SetupDiGetClassDevs(&GUID, 
				NULL, NULL, 
				DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);

	if(pnp == INVALID_HANDLE_VALUE)
		printf("Error attaching to PnP node");

	return pnp;
}

/* Using the GUID and Plug'n'Play handle, loop through the available HID device handles
until a match is found */
HANDLE CWiimote::GetDeviceHandle()
{
	HANDLE hDevice = NULL;
	struct{ 
		DWORD cbSize; 
		char DevicePath[WM_STRING_SIZE];
	} MyHIDDeviceData; /* Device class location for opening */

	HIDD_ATTRIBUTES HIDAttributes; /* Attributes of the HID device */
	SP_INTERFACE_DEVICE_DATA DeviceInterfaceData; /* holds device interface data for the current device */ 
	
	int iHIDdev = 0; /* used for looping through all the devices */
	BOOL success;
	ULONG bRet; /* how many bytes were returned from the device interface detail request? */

	/* Security attributes for opening the device for raw file I/O */
	SECURITY_ATTRIBUTES SecurityAttributes;
	SecurityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES); 
	SecurityAttributes.lpSecurityDescriptor = NULL; 
	SecurityAttributes.bInheritHandle = false; 

	/* Cycle through, up to max devices, looking for the one we want to talk to */
	for (iHIDdev = 0; (iHIDdev < WM_MAX_DEVICES); iHIDdev++)
	{
		DeviceInterfaceData.cbSize = sizeof(DeviceInterfaceData);

		/* Test for a device at this index */
		success = SetupDiEnumDeviceInterfaces(PnPHandle, 
			NULL, 
			&GUID, 
			iHIDdev, 
			&DeviceInterfaceData);

		if(success)
		{
			/* Found a device, so get the name */
			MyHIDDeviceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			success = SetupDiGetDeviceInterfaceDetail(PnPHandle, 
						&DeviceInterfaceData, 
						(PSP_INTERFACE_DEVICE_DETAIL_DATA)&MyHIDDeviceData, 
						WM_STRING_SIZE, 
						&bRet, 
						NULL);

			/* Open the device */
			hDevice = CreateFile((LPWSTR)&MyHIDDeviceData.DevicePath, 
				GENERIC_READ|GENERIC_WRITE,
				FILE_SHARE_READ|FILE_SHARE_WRITE, 
				&SecurityAttributes, 
				OPEN_EXISTING, 
				0, NULL);
			
			if(hDevice != INVALID_HANDLE_VALUE)
			{
				/* Get the information about this HID */
				success = HidD_GetAttributes(hDevice, &HIDAttributes);
				
				/* If it matches, return the handle */
				if (success && HIDAttributes.VendorID == WIIMOTE_VID && HIDAttributes.ProductID == WIIMOTE_PID)
					return hDevice;
				
				CloseHandle(hDevice);
			} /* fi valid handle */
		} /* fi successful enum interfaces */

	} /* for (iHIDdev = 0; (iHIDdev < 20); iHIDdev++) */

	return INVALID_HANDLE_VALUE;
}

/* Initialize the mote and any extension controllers connected.
See comments inside for more details.
Returns true on success, or false on a failure.
*/
BOOL CWiimote::Initialize()
{
/*	Notes on this function:
		1. By calling this class' constructor, we've searched for the device
		2. If the device is found, we have a valid handle to the device
		3. The caller can check to see if CWiimote->connected is true
		4. Go look at the class' contructor for more details on what happens during that init step

	INITIALIZATION PROCESS
	Here's what I think needs to happen in order to get the device setup and running properly.
	(So far it's just the mote buttons and accelerometer data, but I'm adding the chuk and will
	add other stuff later.)

		1. Assume device is already connected and we have a valid HID handle to read/write
		2. DO NOT enable continuous reporting mode, or otherwise enable an accelerometer mode or
			even the chuk until the initialization sequence is done
			(This is because the excessive reports fill up the ring buffer and can get lost or
			otherwise interleaved into your expected response to a config or EEPROM write)
		3. First, request (via write) a status report (report type is WM_OUT_CTRLSTAT, with the next byte 0x00)
			(Note the rumble flag is affected by that 0x00, but if it's just an init phase, who cares.)
		4. Next, immediately read the next packet to get the status packet (report type is WM_MODE_EXP_PORT,
			and it'll look like [20h 00h 00h FFh 00h 00h BBh]).
			The 0x20 is WM_MODE_EXP_PORT.
			Ignore the 2nd, 3rd, 5th and 6th bytes.
			The FF byte is the status flags.
				Here are the FF masks:
					0x01	Unknown
					0x02 	An Extension Controller is connected
					0x04 	Speaker enabled
					0x08 	Continuous reporting enabled
					0x10 	LED 1
					0x20 	LED 2
					0x40 	LED 3
					0x80 	LED 4
			The BB byte is the battery level indicator. Divide by 2 and save as a percentage indicator.
		5. IF the FF byte mask contains 0x02 (extension controller connected), let's assume it's 
			a chuk controller, but future TODO is figure out how to interpret the write-ack packet to decypher
			exactly which controller is connected. 
		6. Calibrate the mote - send the read EEPROM packet, and immediately get the response, parsing it
			into the mote's calibration data fields.
		7. Enable the chuk
			Note that this function doesn't interpret the write-ack packet response, so we don't really know if
			gets enabled or not, at least until we attempt to read calibration data.
		8. If chuk connected, immediately calibrate the chuk - send the read config space packet, and
			immediately get the response, parsing it into the mote's calibration data fields.
		
		TODO: Rebuild the class' constructor to comprehend device skipping, so you can have multiple
				class instantiations, each tied to a different mote. Then the LED settings could correspond.
	*/

// DISABLE CONTINUOUS REPORTING

	/* Turn off continuous reporting by setting button-only mode */
	SetReportMode(WM_MODE_DEFAULT);
	/* Read the first packet confirming button-only mode */
	ClearPackets();
	ReadPacket();
	if(rdPkt.buffer[0] != WM_MODE_DEFAULT)
	{
		/* This confirmation step may be overkill (by returning false). Are there cases where 
		immediately after connecting to the device and sending a request for a reporting mode
		the mote would NOT send a confirmation packet? Need to test a variety of scenarios to 
		be sure...*/
		printf("Failed to received confirmation of default mode during initialization phase.\n");
		return false;
	}

// REQUEST CONTROLLER STATUS

	/* Send the request for controller status */
	ClearPackets();
	wrPkt.buffer[0] = WM_OUT_CTRLSTAT;
	wrPkt.buffer[1] = 0x00;
	WritePacket();
	/* Read the response of the controller status */
	ClearPackets();
	ReadPacket();
	/* Test the response for the presence of an extension controller */
	if(rdPkt.buffer[0] == WM_MODE_EXP_PORT)
	{
		printf("Received the packet response with Controller Status\n");

		/* fourth byte contains our status mask.
		0x02 is the bit test for extension presence (the chuk for now) */
		if(rdPkt.buffer[3] & 0x02)
		{
			printf("Controller status indicates an extension is connected.\n");
			mote.chuk.connected = true;
		}
		else
			mote.chuk.connected = false;

		/* Save the battery level.
		Note that battery level will be between 0 and 200. Need to divide by 2 so 
		that it's a percentage stored in the battery value. */
		mote.battery = rdPkt.buffer[6] / 2;

		printf("Current battery level is %i%\n", mote.battery);
	} /* end if WM_MODE_EXP_PORT */

// CALIBRATE THE MOTE

	/* Send the request for calibration of the mote */
	ClearPackets();
	wrPkt.buffer[0] = WM_OUT_READ_DATA;
	wrPkt.buffer[1] = 0x00; /* don't care about the RUMBLE flag */
	wrPkt.buffer[2] = 0x00; /* address bytes - unused in this case */
	wrPkt.buffer[3] = 0x00;	/* address bytes - unused in this case */
	wrPkt.buffer[4] = 0x16; /* 0x16 is the start of the 7-byte calibration data block */
	wrPkt.buffer[5] = 0x00; /* HIBYTE of num bytes to read */
	wrPkt.buffer[6] = 0x07; /* LOBYTE of num bytes to read - 7 in this case for the cal block */

	/* Send the packet to read data from the calibration block */
	WritePacket();

	/* Read the mote calibration packet */
	ClearPackets();
	ReadPacket();
	if(rdPkt.buffer[0] == WM_MODE_READ_DATA)
	{
			/* Calibration data is stored at the 0x16 offset, and includes:
			0x16      zero point for X axis
			0x17      zero point for Y axis
			0x18      zero point for Z axis
			0x19      unknown
			0x1A      +1G point for X axis
			0x1B      +1G point for Y axis
			0x1C      +1G point for Z axis
			
			...BUT the first 6 bytes of the packet are other details like
			report type, button state, error flag, and the offset read from.
			*/

			/* Note that we still need to test if this is the right offset
			we requested the mote calibration data read from... */
			if(rdPkt.buffer[5] == 0x16)
			{
				mote.zero.x = rdPkt.buffer[6];
				mote.zero.y = rdPkt.buffer[7];
				mote.zero.z = rdPkt.buffer[8];
				/* skip rdPkt.buffer[9] because it's unknown (offset 0x19) */
				mote.scale.x = rdPkt.buffer[10];
				mote.scale.y = rdPkt.buffer[11];
				mote.scale.z = rdPkt.buffer[12];
			}
	} /* end if WM_MODE_READ_DATA for the mote calibration data */

// CALIBRATE THE CHUK

	if(mote.chuk.connected == true)
	{
		printf("Nunchuk connected. Enabling it.\n");
		/* Note: Requesting chuk config data without the chuk enabled returns foxes */
		// ENABLE THE CHUK
		/* Sending a 0x00 byte to address 0x04a40040 to enable the chuk */
		ClearPackets();
		wrPkt.buffer[0] = WM_OUT_WRITE_DATA;
		wrPkt.buffer[1] = 0x04; /* address byte, ignoring RUMBLE flag */
		wrPkt.buffer[2] = 0xa4; /* address byte */
		wrPkt.buffer[3] = 0x00;	/* address byte */
		wrPkt.buffer[4] = 0x40; /* 0x40 is the last part of the address */
		wrPkt.buffer[5] = 0x01; /* only writing one byte */
		wrPkt.buffer[6] = 0x00; /* the one byte of data is 0x00 to enable the chuk */

		/* Send the packet to enable the chuk */
		WritePacket();

		/* Read the write-ack packet.
		TODO: Fix this later when write-ack packets are understood. For now just ignore it. */
		ClearPackets();
		ReadPacket();

		printf("Nunchuk enabled. Calibrating it.\n");
		/* Send a request for calibration data on the chuk */
		ClearPackets();
		wrPkt.buffer[0] = WM_OUT_READ_DATA;
		wrPkt.buffer[1] = 0x04; /* 0x00 is EEPROM read, 0x04 is control register space, ignoring rumble */
		wrPkt.buffer[2] = 0xa4; /* address byte - corresponds to chuk data */
		wrPkt.buffer[3] = 0x00;	/* address byte - unused/ignored */
		wrPkt.buffer[4] = 0x20; /* 0x20 is the start of the 14-byte calibration data block */
		wrPkt.buffer[5] = 0x00; /* HIBYTE of num bytes to read */
		wrPkt.buffer[6] = 0x0E; /* LOBYTE of num bytes to read - 14 in this case for chuk cal block */

		/* Send the packet to read data from the calibration block */
		WritePacket();

		/* Read the chuk calibration packet */
		ClearPackets();
		ReadPacket();

		/* If the packet was the response to a read data request... */
		if(rdPkt.buffer[0] == WM_MODE_READ_DATA)
		{
			/* a read packet coming from address 0x04a40020 contains chuk data.
			Note that the read packet returned does not contain the 0x04 or 0xa4.
			These must be known from the previous read request - thus requiring sequential 
			pairing of a read request with a read response. */
			if(rdPkt.buffer[4] == 0x00 && rdPkt.buffer[5] == 0x20)
			{
				/* If the high nibble of byte 4 is 0xd, it's a calib read
				because this the (number_of_bytes_requested - 1).
				If the low nibble of byte 4 is 0x0, there were no errors during
				the read operation, so we're clear to use the data provided. */
				if(rdPkt.buffer[3] == 0xd0)
				{
					mote.chuk.zero.x = WiiDecrypt(rdPkt.buffer[6]);
					mote.chuk.zero.y = WiiDecrypt(rdPkt.buffer[7]);
					mote.chuk.zero.z = WiiDecrypt(rdPkt.buffer[8]);
					/* rdPkt.buffer[9] has some LSB info */
					mote.chuk.scale.x = WiiDecrypt(rdPkt.buffer[10]);
					mote.chuk.scale.y = WiiDecrypt(rdPkt.buffer[11]);
					mote.chuk.scale.z = WiiDecrypt(rdPkt.buffer[12]);
					/* rdPkt.buffer[13] has some LSB info */
					mote.chuk.stickMax.x = WiiDecrypt(rdPkt.buffer[14]);
					mote.chuk.stickMin.x = WiiDecrypt(rdPkt.buffer[15]);
					mote.chuk.stickCenter.x = WiiDecrypt(rdPkt.buffer[16]);
					mote.chuk.stickMax.y = WiiDecrypt(rdPkt.buffer[17]);
					mote.chuk.stickMin.y = WiiDecrypt(rdPkt.buffer[18]);
					mote.chuk.stickCenter.y = WiiDecrypt(rdPkt.buffer[19]);
				} /* end if chuk byte size test */
			} /* end if chuk config space test */
		} /* end if READ_DATA packet */
	} /* end if chuk connected */

	return true;
}

/* Enter into a debug loop, doing something that seems useful at the time. 
 Returns 0 on success.
 Read the comments inside this function for more details on what this is used for.
*/
int CWiimote::DebugLoop()
{
	/* Continuous reporting, with mote, chuk and acceleration data */
	if(mote.chuk.connected == true)
		SetReportMode(WM_MODE_ACC_EXT, WM_MODE_CONT);
	else /* Otherwise just get mote and acceleration data */
		SetReportMode(WM_MODE_ACC, WM_MODE_CONT);

	/* Start out in mouse mode */
	int myMode = WM_MY_MOUSE;
	bool myModeChanged = false;
	
	EnableLED(WM_LED_ONE);

	/* used for mouse mode */
	DWORD mouse_x, mouse_y;
	mouse_x = mouse_y = 0;
	bool lmbDown, rmbDown;
	lmbDown = rmbDown = false;

	/* track last button states...
	TODO: clean this up with a struct or something. */
	bool last_a, last_b, last_one, last_two,
		last_up, last_down, last_right, last_left, jumping;
	last_a = last_b = last_one = last_two = last_up = last_down = last_right = last_left = jumping = false;

	bool last_moveleft, last_moveright, last_moveforward, last_movebackward, last_running, last_anchor, last_throw;
	last_moveleft = last_moveright = last_moveforward = last_movebackward = last_running = last_anchor = last_throw = false;

	while(!disconnect)
	{
		ParseReport();

		switch(myMode)
		{
		case WM_MY_MOUSE:
			mouse_x = (int) (mote.tilt.x / 4);
			mouse_y = (int) (mote.tilt.y / 2);
			MouseEvent(MOUSEEVENTF_MOVE, mouse_x, mouse_y);
			if(mote.dpad.down)
				MouseEvent(MOUSEEVENTF_WHEEL, NULL, NULL, -120);
			else if(mote.dpad.up)
				MouseEvent(MOUSEEVENTF_WHEEL, NULL, NULL, 120);
			if(mote.button.a && !lmbDown)
			{
				MouseEvent(MOUSEEVENTF_LEFTDOWN);
				lmbDown = true;
			}
			else if(!mote.button.a)
				if(lmbDown)
			{
				{
					MouseEvent(MOUSEEVENTF_LEFTUP);
					lmbDown = false;
				}
			}
			if(mote.button.b && !rmbDown)
			{
				MouseEvent(MOUSEEVENTF_RIGHTDOWN);
				rmbDown = true;
			}
			else if(!mote.button.b)
			{
				if(rmbDown)
				{
					MouseEvent(MOUSEEVENTF_RIGHTUP);
					rmbDown = false;
				}
			}
			break;
		case WM_MY_EMU:
			if(mote.chuk.connected == true)
			{
//				system("cls");
//				printf("Chuk: Sx: %f\t Sy %f\n",
//					mote.chuk.stick.x, mote.chuk.stick.y);
/*				printf("Chuk: Xt: %2.2f\t Yt: %2.2f\t Zt: %2.2f\n", 
					mote.chuk.tilt.x, mote.chuk.tilt.y, mote.chuk.tilt.z);
				printf("Chuk: Xf: %2.2f\t Yf: %2.2f\t Zf: %2.2f\n", 
					mote.chuk.force.x, mote.chuk.force.y, mote.chuk.force.z);
				if(mote.chuk.button.c)
					printf("Chuk button C pressed.\n");
				if(mote.chuk.button.z)
					printf("Chuk button Z pressed.\n");
*/			}

			if(mote.button.a && !last_a)
				KeyboardEvent('A');
			if(!mote.button.a && last_a)
				KeyboardEvent('A', KEYEVENTF_KEYUP);
			
			if(mote.button.b && !last_b)
				KeyboardEvent('B');
			if(!mote.button.b && last_b)
				KeyboardEvent('B', KEYEVENTF_KEYUP);
			
			if(mote.button.one && !last_one)
				KeyboardEvent('1');
			if(!mote.button.one && last_one)
				KeyboardEvent('1', KEYEVENTF_KEYUP);
			
			if(mote.button.two && !last_two)
				KeyboardEvent('2');
			if(!mote.button.two && last_two)
				KeyboardEvent('2', KEYEVENTF_KEYUP);
			
			if(mote.dpad.down && !last_down)
				KeyboardEvent(VK_RIGHT);
			if(!mote.dpad.down && last_down)
				KeyboardEvent(VK_RIGHT, KEYEVENTF_KEYUP);
			
			if(mote.dpad.up && !last_up)
				KeyboardEvent(VK_LEFT);
			if(!mote.dpad.up && last_up)
				KeyboardEvent(VK_LEFT, KEYEVENTF_KEYUP);
			
			if(mote.dpad.left && !last_left)
				KeyboardEvent(VK_DOWN);
			if(!mote.dpad.left && last_left)
				KeyboardEvent(VK_DOWN, KEYEVENTF_KEYUP);
			
			if(mote.dpad.right && !last_right)
				KeyboardEvent(VK_UP);
			if(!mote.dpad.right && last_right) 
				KeyboardEvent(VK_UP, KEYEVENTF_KEYUP);

			/* Save off the current button states as the last_* for 
			comparison on the next pass */
			last_a = mote.button.a;
			last_b = mote.button.b;
			last_one = mote.button.one;
			last_two = mote.button.two;
			last_up = mote.dpad.up;
			last_down = mote.dpad.down;
			last_left = mote.dpad.left;
			last_right = mote.dpad.right;
			break;
		case WM_MY_FPS:
			/* First person shooter configuration */

			/* Tilt-based mouse movement */
/*			if(mote.tilt.x < -8.f || mote.tilt.x > 8.f) // Setting a dead zone
			{
				if(mote.tilt.x > 0.f)
					mouse_x = (mote.tilt.x - 8.f) / 3.f; // Subtract the dead zone and apply sensitivity
				else
					mouse_x = (mote.tilt.x + 8.f) / 3.f;
			}
			else
				mouse_x = 0.f;
			if(mote.tilt.y > 8.f || mote.tilt.y < -8.f) // Setting a dead zone
			{
				if(mote.tilt.y > 0.f)
					mouse_y = (mote.tilt.y - 8.f) / 4.f;
				else
					mouse_y = (mote.tilt.y + 8.f) / 4.f;
			}
			else
				mouse_y = 0.f;
	*/
			/* Chukstick-based mouse movement */
			if(mote.chuk.connected)
			{
				mouse_x = (int)(mote.chuk.stick.x * 18.f);
				mouse_y = (int)(mote.chuk.stick.y * 18.f);

				if(mouse_x < 2)
					mouse_x = 0;
				if(mouse_y < 2)
					mouse_y = 0;
			}
			MouseEvent(MOUSEEVENTF_MOVE, mouse_x, mouse_y);
			
			/* Remap the A button on the wiimote to the right mouse button,
			which is typically a zoom modifier */
			if(mote.button.a && !rmbDown)
			{
				MouseEvent(MOUSEEVENTF_RIGHTDOWN);
				rmbDown = true;
			}
			else if(!mote.button.a)
			{
				if(rmbDown)
				{
					MouseEvent(MOUSEEVENTF_RIGHTUP);
					rmbDown = false;
				}
			}

			/* Remap the B (trigger) button on the wiimote to the right mouse button,
			which is typically the "shoot" button */
			if(mote.button.b && !lmbDown)
			{
				MouseEvent(MOUSEEVENTF_LEFTDOWN);
				lmbDown = true;
			}
			else if(!mote.button.b)
			{
				if(lmbDown)
				{
					MouseEvent(MOUSEEVENTF_LEFTUP);
					lmbDown = false;
				}
			}
			
			if(mote.chuk.connected)
			{
				/* Stick-based WASD */
/*				if(mote.chuk.stick.x < -0.5f && !last_moveleft)
					KeyboardEvent('A');
				if(!(mote.chuk.stick.x < -0.5f) && last_moveleft)
					KeyboardEvent('A', KEYEVENTF_KEYUP);
				if(mote.chuk.stick.x > 0.5f && !last_moveright)
					KeyboardEvent('D');
				if(!(mote.chuk.stick.x > 0.5f) && last_moveright)
					KeyboardEvent('D', KEYEVENTF_KEYUP);
				if(mote.chuk.stick.y > 0.5f && !last_moveforward)
					KeyboardEvent('W');
				if(!(mote.chuk.stick.y > 0.5f) && last_moveforward)
					KeyboardEvent('W', KEYEVENTF_KEYUP);
				if(mote.chuk.stick.y < -0.5f && !last_movebackward)
					KeyboardEvent('S');
				if(!(mote.chuk.stick.y < -0.5f) && last_movebackward)
					KeyboardEvent('S', KEYEVENTF_KEYUP);
*/
				/* Tilt-based WASD */
				if(mote.chuk.tilt.x < -20.f && mote.chuk.tilt.x > -90.f && !last_moveleft)
					KeyboardEvent('A');
				if(!(mote.chuk.tilt.x < -20.f && mote.chuk.tilt.x > -90.f) && last_moveleft)
					KeyboardEvent('A', KEYEVENTF_KEYUP);
				if(mote.chuk.tilt.x > 20.f && mote.chuk.tilt.x < 90.f && !last_moveright)
					KeyboardEvent('D');
				if(!(mote.chuk.tilt.x > 20.f && mote.chuk.tilt.x < 90.f) && last_moveright)
					KeyboardEvent('D', KEYEVENTF_KEYUP);
				if(mote.chuk.tilt.y > 20.f && mote.chuk.tilt.y < 60.f && !last_moveforward)
					KeyboardEvent('W');
				if(!(mote.chuk.tilt.y > 20.f && mote.chuk.tilt.y < 60.f) && last_moveforward)
					KeyboardEvent('W', KEYEVENTF_KEYUP);
				if(mote.chuk.tilt.y < -20.f && mote.chuk.tilt.y > -60.f && !last_movebackward)
					KeyboardEvent('S');
				if(!(mote.chuk.tilt.y < -20.f && mote.chuk.tilt.y > -60.f) && last_movebackward)
					KeyboardEvent('S', KEYEVENTF_KEYUP);

				if(mote.chuk.button.z && !last_running)
					KeyboardEvent(VK_SHIFT);
				if(!mote.chuk.button.z && last_running)
					KeyboardEvent(VK_SHIFT, KEYEVENTF_KEYUP);

				if(mote.chuk.button.c && !last_anchor)
					KeyboardEvent('C');
				if(!mote.chuk.button.c && last_anchor)
					KeyboardEvent('C', KEYEVENTF_KEYUP);

				if(mote.chuk.force.z < -2.0f && !jumping)
					KeyboardEvent(VK_SPACE);
				if(!(mote.chuk.force.z < -2.0f) && jumping)
					KeyboardEvent(VK_SPACE, KEYEVENTF_KEYUP);

				/* Tilt-based WASD */
				last_moveleft = (mote.chuk.tilt.x < -20.f && mote.chuk.tilt.x > -90.f);
				last_moveright = (mote.chuk.tilt.x > 20.f && mote.chuk.tilt.x < 90.f);
				last_moveforward = (mote.chuk.tilt.y > 20.f && mote.chuk.tilt.y < 60.f);
				last_movebackward = (mote.chuk.tilt.y < -20.f && mote.chuk.tilt.y > -60.f);
				
				/* Stick-based WASD */
/*				last_moveleft = mote.chuk.stick.x < -0.5f;
				last_moveright = mote.chuk.stick.x > 0.5f;
				last_moveforward = mote.chuk.stick.y > 0.5f;
				last_movebackward = mote.chuk.stick.y < -0.5f;
*/
				last_running = mote.chuk.button.z;
				last_anchor = mote.chuk.button.c;
				jumping = (mote.chuk.force.z < -2.0f);
			}

			if(mote.button.one && !last_one)
				KeyboardEvent('R'); /* r key */
			if(!mote.button.one && last_one)
				KeyboardEvent('R', KEYEVENTF_KEYUP);
			
			if(mote.button.two && !last_two)
				KeyboardEvent('F');
			if(!mote.button.two && last_two)
				KeyboardEvent('F', KEYEVENTF_KEYUP);
			
			if(mote.dpad.down && !last_down)
				KeyboardEvent('G');
			if(!mote.dpad.down && last_down)
				KeyboardEvent('G', KEYEVENTF_KEYUP);
			
			if(mote.dpad.up && !last_up)
				KeyboardEvent('E');
			if(!mote.dpad.up && last_up)
				KeyboardEvent('E', KEYEVENTF_KEYUP);
			
			if(mote.dpad.left && !last_left)
				KeyboardEvent('Q');
			if(!mote.dpad.left && last_left)
				KeyboardEvent('Q', KEYEVENTF_KEYUP);
			
			if(mote.dpad.right && !last_right)
				KeyboardEvent('Q');
			if(!mote.dpad.right && last_right) 
				KeyboardEvent('Q', KEYEVENTF_KEYUP);

			if(mote.force.z < -2.f && !last_throw)
				KeyboardEvent('G');
			if(!(mote.force.z < -2.f) && last_throw)
				KeyboardEvent('G', KEYEVENTF_KEYUP);

			/* Save off the current button states as the last_* for 
			comparison on the next pass */
			last_a = mote.button.a;
			last_b = mote.button.b;
			last_one = mote.button.one;
			last_two = mote.button.two;
			last_up = mote.dpad.up;
			last_down = mote.dpad.down;
			last_left = mote.dpad.left;
			last_right = mote.dpad.right;

			last_throw = (mote.force.z < -2.f);

			break;
		default:
			break;
		}

		/* If minus is pressed, cycle to the next mode type */
		if(mote.button.minus)
		{
			/* Rotating backwards, so long as we're at a mode higher than zero */
			if(myMode > 0)
				myMode--;
			else
				myMode = WM_MY_MAX;
		
			myModeChanged = true;
		}
		
		if(mote.button.plus)
		{
			/* Rotate forwards, so long as we're at a mode lower than max */
			if(myMode == WM_MY_MAX)
				myMode = 0;
			else
				myMode++;

			myModeChanged = true;
		}

		/* Change the LED display to show you the mode it's running */
		if(myModeChanged)
		{
			switch(myMode)
			{
			case WM_MY_MOUSE:
				EnableLED(WM_LED_ONE);
				break;
			case WM_MY_EMU:
				EnableLED(WM_LED_TWO);
				break;
			case WM_MY_FPS:
				EnableLED(WM_LED_THREE);
				break;
			default:
				EnableLED(WM_LED_NONE);
				break;
			}

			myModeChanged = false;

			// Slow things down a bit so packets aren't continuously
			// processed in this loop (without this, pressing the mode
			// switch button cycles modes really, really quickly
			Sleep(1000);
		}

		if(mote.button.home)
			disconnect = true;
	}

	SetReportMode(WM_MODE_DEFAULT);		

	return 0;
}

/* Self explanatory */
void CWiimote::ClearPackets()
{
	rdPkt.success = wrPkt.success = false;
	rdPkt.bytesTransferred = wrPkt.bytesTransferred = 0;
	memset(&rdPkt.buffer,0,WM_PACKET_SIZE);
	memset(&wrPkt.buffer,0,WM_PACKET_SIZE);
}

/* Given an input mask with button states, save the 
individual button states into the booleans of the class */
void CWiimote::UpdateButtonStates(unsigned short buttons)
{
	mote.dpad.up = (buttons & WM_BUT_UP) != 0; // Avoiding C4800 warning
	mote.dpad.down = (buttons & WM_BUT_DOWN) != 0;
	mote.dpad.left = (buttons & WM_BUT_LEFT) != 0;
	mote.dpad.right = (buttons & WM_BUT_RIGHT) != 0;

	mote.button.minus = (buttons & WM_BUT_MINUS) != 0;
	mote.button.plus = (buttons & WM_BUT_PLUS) != 0;
	mote.button.home = (buttons & WM_BUT_HOME) != 0;
	mote.button.a = (buttons & WM_BUT_A) != 0;

	mote.button.b = (buttons & WM_BUT_B) != 0;
	mote.button.one = (buttons & WM_BUT_ONE) != 0;
	mote.button.two = (buttons & WM_BUT_TWO) != 0;
}

/* Read a packet from the device. 
Assumes the caller has set up the read packet buffer with appropriate contents.
Relies on the _packet struct's various data to store succcess, bytes read, etc. */
void CWiimote::ReadPacket()
{ 
	rdPkt.success = ReadFile(HIDHandle, &rdPkt.buffer, WM_PACKET_SIZE, &rdPkt.bytesTransferred, NULL);
}

/* Write a packet to the device.
Assumes the caller has set up the read packet buffer with appropriate contents. */
void CWiimote::WritePacket()
{ 
	wrPkt.success = WriteFile(HIDHandle, &wrPkt.buffer, WM_PACKET_SIZE, &wrPkt.bytesTransferred, NULL);
}

/* Read a report from the wiimote and dissect
it, saving the reports data */
void CWiimote::ParseReport()
{
	unsigned short buttons = 0;

	/* MAJOR TODO: Rewrite this section to do better breakdowns of the different types of reports.
	Maybe break out the button mask checks into separate functions for cleanliness...*/
	ClearPackets();
	ReadPacket();
	if(rdPkt.success)
	{
		byte reportType = rdPkt.buffer[0];
		
		/* Depending on the report type, dissect the packets appropriately */
		if(reportType == WM_MODE_DEFAULT)
		{
			/* WM_MODE_DEFAULT contains only 2 bytes of relevent payload.
			These need to be combined into a 16-bit value and then bit-tested
			with each of the button masks. */

			// Combine the buffer bytes into the unsigned shot
			buttons = rdPkt.buffer[1] << 8;
			buttons |= rdPkt.buffer[2];

			// Update button state based on the bytes provided
			UpdateButtonStates(buttons);
		}

		if(reportType == WM_MODE_ACC)
		{
			/* WM_MODE_ACC contains 5 bytes of relevent payload.
			As before, the first 2 payload bytes are combined and bit-tested against masks.
			The last 3 payload bytes are raw axis information, which needs to be recalibrated. */

			buttons = rdPkt.buffer[1] << 8;
			buttons |= rdPkt.buffer[2];

			UpdateButtonStates(buttons);

			mote.axis.x = rdPkt.buffer[3];
			mote.axis.y = rdPkt.buffer[4];
			mote.axis.z = rdPkt.buffer[5];

			/* If calibration data has been gathered...recalibrate */
			if(mote.zero.x)
			{
				CalcTilt();
				CalcForce();
			}
		}

		if(reportType == WM_MODE_ACC_EXT)
		{
			/* WM_MODE_ACC_EXT contains a full payload.
			As before, the first 2 payload bytes are combined and bit-tested against button masks.
			The next 3 payload bytes are raw axis information, which needs to be recalibrated.
			The last 16 payload bytes are extension controller data, which also needs recalibration
			and bit-testing against button masks. */
			buttons = rdPkt.buffer[1] << 8;
			buttons |= rdPkt.buffer[2];

			/* Update the button information */
			UpdateButtonStates(buttons);

			mote.axis.x = rdPkt.buffer[3];
			mote.axis.y = rdPkt.buffer[4];
			mote.axis.z = rdPkt.buffer[5];

			mote.chuk.stickAxis.x = WiiDecrypt(rdPkt.buffer[6]);
			mote.chuk.stickAxis.y = WiiDecrypt(rdPkt.buffer[7]);
			mote.chuk.axis.x = WiiDecrypt(rdPkt.buffer[8]);
			mote.chuk.axis.y = WiiDecrypt(rdPkt.buffer[9]);
			mote.chuk.axis.z = WiiDecrypt(rdPkt.buffer[10]);
			
			byte chukButtons = WiiDecrypt(rdPkt.buffer[11]);
			/* Unlike the mote buttons, 0 means the button is pressed */
			if(chukButtons & WM_CHUK_BUT_C) mote.chuk.button.c = false;
			else mote.chuk.button.c = true;
			if(chukButtons & WM_CHUK_BUT_Z) mote.chuk.button.z = false;
			else mote.chuk.button.z = true;

			/* If calibration data has been gathered...recalibrate */
			if(mote.zero.x)
			{
				CalcTilt();
				CalcForce();
				CalcStick();
			}
		}

		/* This section isn't working as designed ... */
		if(reportType == WM_MODE_WRITE_DATA)
		{
			/* TODO: Write confirmation packet not yet understood */
		}

		/* TODO: Add dissection for the rest of the input report types */
	}
}

/* Using the WM_OUT_REPORT_TYPE report ID, write a packet
to set the mode, such a buttons, or buttons + accelerometer, etc. 
First parameter is the mode, second is continuous mode */
BOOL CWiimote::SetReportMode(byte mode, byte continuous)
{
	ClearPackets();

	/* Set accelerometer mode */
	wrPkt.buffer[0] = WM_OUT_REPORT_TYPE;
	wrPkt.buffer[1] = continuous;
	wrPkt.buffer[2] = mode;
	WritePacket();

	return wrPkt.success;
}

/* Turn on/off the rumble effect - 
Note that rumbling needs to carry forward to certain other 
write packets such as: 
WM_OUT_LEDFF (0x11) LED control and force feedback
WM_OUT_IRSENSE (0x13) IR Sensor enable
WM_OUT_SPKR_ENABLE (0x14) Speaker enable
WM_OUT_CTRLSTAT (0x15) Controller status
WM_OUT_SPKR_MUTE (0x19) Speaker mute
WM_OUT_IRSENSE2 (0x1a) IR Sense 2 */
BOOL CWiimote::Rumble(bool on)
{
	ClearPackets();

	/* Rumble */
	wrPkt.buffer[0] = WM_OUT_IRSENSE2;
	if(on)
	{
		mote.rumbling = true;	
		wrPkt.buffer[1] = WM_OUT_RUMBLE;
	}
	else
	{
		mote.rumbling = false;
		wrPkt.buffer[1] = 0x00;
	}

	WritePacket();

	return wrPkt.success;
}

/* Enable LED's based on a provided mask - 
WM_LED_NONE, WM_LED_ONE, WM_LED_TWO, etc */
BOOL CWiimote::EnableLED(byte mask)
{
	ClearPackets();

	/* Enable an LED */
	wrPkt.buffer[0] = WM_OUT_LEDFF;
	wrPkt.buffer[1] = mask;

	if(mote.rumbling)
		wrPkt.buffer[1] |= WM_OUT_RUMBLE;

	WritePacket();

	return wrPkt.success;
}

/* Calculate Tilt for each axis, in degrees, based on the raw G-force
data...assumes axis and calibration data is already gathered */
void CWiimote::CalcTilt()
{
    float xs = (float) (mote.scale.x) - (float) (mote.zero.x);
    float ys = (float) (mote.scale.y) - (float) (mote.zero.y);
    float zs = (float) (mote.scale.z) - (float) (mote.zero.z);	
	
    float x = (float) ((float)mote.axis.x - (float)mote.zero.x) / xs;
    float y = (float) ((float)mote.axis.y - (float)mote.zero.y) / ys;
    float z = (float) ((float)mote.axis.z - (float)mote.zero.z) / zs;

    mote.tilt.x = (asin(x) * 180.0f / (float) M_PI);
    mote.tilt.y = (asin(y) * 180.0f / (float) M_PI);
    mote.tilt.z = (asin(z) * 180.0f / (float) M_PI);

	if(mote.chuk.connected == true)
	{
		xs = (float) (mote.chuk.scale.x) - (float) (mote.chuk.zero.x);
		ys = (float) (mote.chuk.scale.y) - (float) (mote.chuk.zero.y);
		zs = (float) (mote.chuk.scale.z) - (float) (mote.chuk.zero.z);

		x = (float) (mote.chuk.axis.x - mote.chuk.zero.x) / xs;
		y = (float) (mote.chuk.axis.y - mote.chuk.zero.y) / ys;
		z = (float) (mote.chuk.axis.z - mote.chuk.zero.z) / zs;
		
		mote.chuk.tilt.x = (asin(x) * 180.0f / (float) M_PI);
		mote.chuk.tilt.y = (asin(y) * 180.0f / (float) M_PI);
		mote.chuk.tilt.z = (asin(z) * 180.0f / (float) M_PI);
	}
}

/* Calculate Force for each axis, based on the raw G-force
data...assumes axis and calibration data is already gathered */
void CWiimote::CalcForce()
{
    mote.force.x = (float) (mote.axis.x - mote.zero.x) / (mote.scale.x - mote.zero.x);
    mote.force.y = (float) (mote.axis.y - mote.zero.y) / (mote.scale.y - mote.zero.y);
    mote.force.z = (float) (mote.axis.z - mote.zero.z) / (mote.scale.z - mote.zero.z);

	if(mote.chuk.connected == true)
	{
		mote.chuk.force.x = (float) (mote.chuk.axis.x - mote.chuk.zero.x) / (mote.chuk.scale.x - mote.chuk.zero.x);
		mote.chuk.force.y = (float) (mote.chuk.axis.y - mote.chuk.zero.y) / (mote.chuk.scale.y - mote.chuk.zero.y);
		mote.chuk.force.z = (float) (mote.chuk.axis.z - mote.chuk.zero.z) / (mote.chuk.scale.z - mote.chuk.zero.z);
	}
}
/* Calculate the stick position relative to it's min/max/center */
void CWiimote::CalcStick()
{
	float temp = 0.f;
	float stickAxisX = mote.chuk.stickAxis.x;
	float stickAxisY = mote.chuk.stickAxis.y;
	float stickCenterX = mote.chuk.stickCenter.x;
	float stickCenterY = mote.chuk.stickCenter.y;
	float stickMaxX = mote.chuk.stickMax.x;
	float stickMaxY = mote.chuk.stickMax.y;
	float stickMinX = mote.chuk.stickMin.x;
	float stickMinY = mote.chuk.stickMin.y;

	if(mote.chuk.stickAxis.x == mote.chuk.stickCenter.x)
		mote.chuk.stick.x = 0.f;
	if(mote.chuk.stickAxis.y == mote.chuk.stickCenter.y)
		mote.chuk.stick.y = 0.f;

	if(stickAxisX < stickCenterX)
	{
		temp = (float)((stickAxisX - stickCenterX) / (stickCenterX - stickMinX));
		mote.chuk.stick.x = temp;
	}
	else if(stickAxisX > stickCenterX)
	{
		temp = (float)((stickAxisX - stickCenterX) / (stickMaxX - stickCenterX));
		mote.chuk.stick.x = temp;
	}
	if(stickAxisY < stickCenterY)
	{
		temp = (float)((stickAxisY - stickCenterY) / (stickCenterY - stickMinY));
		mote.chuk.stick.y = temp;
	}
	else if(stickAxisY > stickCenterY)
	{
		temp = (float)((stickAxisY - stickCenterY) / (stickMaxY - stickCenterY));
		mote.chuk.stick.y = temp;
	}

}

/* Send a keyboard event using SendInput.
Accepts a key and a flag value */
UINT CWiimote::KeyboardEvent(byte keyCode, DWORD flags)
{
	INPUT key;
	UINT ret = 0;

	key.type = INPUT_KEYBOARD;
	key.ki.wVk = keyCode;
	key.ki.dwFlags = flags;
	key.ki.time = 0;
	key.ki.wScan = MapVirtualKeyEx(keyCode, 0, kbLayout); /* 2nd param is MAPVK_VK_TO_VSC, PSDK guys didn't
														  feel it necessary to include this in winuser.h...*/
	key.ki.dwExtraInfo = 0;

	// Send a single key using the keyCode and flags provided
	ret = SendInput(1, &key, sizeof(INPUT));

	return ret;
}

/* Sends a mouse event using SendInput. */
UINT CWiimote::MouseEvent(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extraInfo)
{
	INPUT key;
	UINT ret;

	key.type = INPUT_MOUSE;
	key.mi.dwFlags = flags;
	key.mi.dwExtraInfo = extraInfo;
	key.mi.dx = dx;
	key.mi.dy = dy;
	key.mi.mouseData = data;
	key.mi.time = 0;
	
	ret = SendInput(1, &key, sizeof(INPUT));

	return ret;
}

/* Decrypt a byte value from a packet.
Some packets include data which must be decrypted with:
(cryptByte XOR 0x17) + 0x17 */
byte CWiimote::WiiDecrypt(byte c){ return ((c ^ 0x17) + 0x17);}