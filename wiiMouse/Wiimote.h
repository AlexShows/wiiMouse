#pragma once

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <stdio.h>
#include <tchar.h>
#include <math.h>

#define _WIN32_WINNT 0x0501
#include <windows.h>

#include <iostream>
#include <sstream>
using namespace std;

#include "objbase.h"
#include "stdlib.h"

extern "C"{
#include "setupapi.h"		// needs setupapi.lib
#include "hidsdi.h"			// needs hid.lib
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WIIMOTE_VID 0x057e /* Nintendo */
#define WIIMOTE_PID 0x0306 /* WiiMote */

#define WM_MAX_DEVICES 20
#define WM_STRING_SIZE 256
#define WM_PACKET_SIZE 22

/* My modes */
#define WM_MY_MAX 0x02 /* how many my modes do I have; used for rotation */
#define WM_MY_MOUSE  0x00 /* mouse input mode, wiimote acts like a mouse */
#define WM_MY_EMU	0x01 /* emulator mode, wiimote held on it's side */
#define WM_MY_FPS 0x02 /* a first person shooter mode */

/* Wiimote input report IDs */
#define WM_MODE_EXP_PORT 0x20
#define WM_MODE_READ_DATA 0x21
#define WM_MODE_WRITE_DATA 0x22
#define WM_MODE_DEFAULT 0x30
#define WM_MODE_ACC 0x31
#define WM_MODE_IR 0x32
#define WM_MODE_ACC_IR 0x33
#define WM_MODE_EXT 0x34
#define WM_MODE_ACC_EXT 0x35
#define WM_MODE_IR_EXT 0x36
#define WM_MODE_ACC_IR_EXT 0x37
#define WM_MODE_FULL1 0x3e
#define WM_MODE_FULL2 0x3f

/* Continuous or non-continuous output mode */
#define WM_MODE_NONCONT 0x00
#define WM_MODE_CONT 0x04

/* Wiimote output report IDs */
#define WM_OUT_LEDFF 0x11
#define WM_OUT_REPORT_TYPE 0x12
#define WM_OUT_IRSENSE 0x13
#define WM_OUT_SPKR_ENABLE 0x14
#define WM_OUT_CTRLSTAT 0x15
#define WM_OUT_WRITE_DATA 0x16
#define WM_OUT_READ_DATA 0x17
#define WM_OUT_SPKR_DATA 0x18
#define WM_OUT_SPKR_MUTE 0x19
#define WM_OUT_IRSENSE2 0x1a

/* Wiimote rumble mask */
#define WM_OUT_RUMBLE 0x01

/* Wiimote button masks -
Note that these are word masks, so combine
the second and third byte of a read packet 
that has button information and apply the 
corresponding mask */
#define WM_BUT_TWO 0x0001
#define WM_BUT_ONE 0x0002
#define WM_BUT_B 0x0004
#define WM_BUT_A 0x0008
#define WM_BUT_MINUS 0x0010
#define WM_UNKNOWN0 0x0020 /* Unknown, TBD */
#define WM_XACC_LSB 0x0040 /* X acceleration LSB */
#define WM_BUT_HOME 0x0080
#define WM_BUT_LEFT 0x0100
#define WM_BUT_RIGHT 0x0200
#define WM_BUT_DOWN 0x0400
#define WM_BUT_UP 0x0800
#define WM_BUT_PLUS 0x1000
#define WM_YACC_LSB 0x2000
#define WM_ZACC_LSB 0x4000
#define WM_UNKNOWN1 0x8000 /* Unknown, TBD */
#define WM_CHUK_BUT_Z 0x0001
#define WM_CHUK_BUT_C 0x0002

/* Wiimote LED masks */
#define WM_LED_NONE 0x00
#define WM_LED_ONE 0x10
#define WM_LED_TWO 0x20
#define WM_LED_THREE 0x40
#define WM_LED_FOUR 0x80

class CWiimote
{
struct _byte3 {
	byte x;
	byte y;
	byte z;
};

struct _byte2 {
	byte x;
	byte y;
};

struct _directionalpad {
	bool left;
	bool right;
	bool up;
	bool down;
};

struct _mote_buttons {
	bool a;
	bool b;
	bool one;
	bool two;
	bool plus;
	bool minus;
	bool home;
};

struct _chuk_buttons {
	bool c;
	bool z;
};

struct _float2 {
	float x;
	float y;
};

struct _float3 {
	float x;
	float y;
	float z;
};

struct _wiichuk {
	_byte2 stickAxis; /* Min/Max/Center determined by calibration data */
	_byte2 stickMin; /* Stick minimums calibration data */
	_byte2 stickMax; /* Stick maximums calibration data */
	_byte2 stickCenter; /* Stick centers calibration data */
	_float2 stick;	/* Center is 0.0f, Min is -1, Max is +1 */
	_chuk_buttons button;
	_byte3 axis; /* G's are relative to calibration data */
	_byte3 scale; /* Calibration for each axis (what +1G equal to) */
	_byte3 zero; /* Calibration for each axis (what 0G is equal to) */
	_float3 force; /* Calibrated force in G's */
	_float3 tilt; /* Calibrated tilt in degrees */
	bool connected; /* Is the nunchuk connected to the mote? */
};

struct _wiimote {
	BOOL connected; /* Are we connected and talking to this mote? */
	BOOL rumbling; /* Is the mote rumbling? */
	int battery; /* Battery level, 0 to 100 */
	_wiichuk chuk; /* Nunchuk controller */
	_directionalpad dpad; /* Up/Down/Left/Right */
	_mote_buttons button; /* A, B, One, Two, Plus, Minus, Home */
	_byte3 axis; /* G's are relative to calibration data */
	_byte3 scale; /* Calibration for each axis (what +1G equal to) */
	_byte3 zero; /* Calibration for each axis (what 0G is equal to) */
	_float3 force; /* Calibrated force in G's */
	_float3 tilt; /* Calibrated tilt in degrees */
};

struct _packet {
	BOOL success;
	DWORD bytesTransferred;
	byte buffer[WM_PACKET_SIZE];
};
public:
	CWiimote(void);
	int DebugLoop();
	BOOL Rumble(bool);
	BOOL EnableLED(byte);
	HANDLE HIDHandle;
	WCHAR sManuf[WM_STRING_SIZE];
	WCHAR sProd[WM_STRING_SIZE];
	BOOL disconnect;
	_wiimote mote;
public:
	~CWiimote(void);
private:
	HANDLE AttachToPnP();
	HANDLE GetDeviceHandle();
	BOOL Initialize();
	void ClearPackets();
	void ReadPacket();
	void WritePacket();
	void ParseReport();
	BOOL SetReportMode(byte, byte = NULL);
	void CalcForce();
	void CalcTilt();
	void CalcStick();
	UINT KeyboardEvent(byte, DWORD = NULL);
	UINT MouseEvent(DWORD, DWORD = NULL, DWORD = NULL, DWORD = NULL, ULONG_PTR = NULL);
	byte WiiDecrypt(byte);
	struct _GUID GUID;
	HANDLE PnPHandle;
	HKL kbLayout;
	_packet rdPkt;
	_packet wrPkt;
};
