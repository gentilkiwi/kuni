/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#pragma once
#include <windows.h>
#include <process.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cfgmgr32.h>
#include "utils.h"

#define LOGITECH_VID				0x046d
#define LOGITECH_UNIFYING_FW_PID	0xc52b
//#define LOGITECH_UNIFYING_FW_PID	0xc531
//#define LOGITECH_UNIFYING_FW_PID	0xc53f
//#define LOGITECH_UNIFYING_FW_PID	0xc548
#define LOGITECH_UNIFYING_BL_PID	0xaaaa
//#define LOGITECH_UNIFYING_BL_PID	0xaade

typedef BOOL(CALLBACK* KUNI_CO_FILTER_CALLBACK) (const BYTE *buffer, const DWORD cbBuffer, PVOID pvArg);

typedef struct _KUNI_CO_REPORT {
	BYTE Id;
	USHORT ByteLength;
	BYTE* Buffer;
} KUNI_CO_REPORT, *PKUNI_CO_REPORT;

typedef struct _KUNI_CO_HANDLE {
	HANDLE hDevice;
	HIDD_ATTRIBUTES Attributes;
	KUNI_CO_REPORT Input;
	KUNI_CO_REPORT Output;
	OVERLAPPED OutputOverlapped; // created once, not per frame
	BOOL OutputPending;
	HANDLE hThread;
	HANDLE hSignalToRead;
	HANDLE hStopEvent;
	KUNI_CO_FILTER_CALLBACK FilterCallback;
	PVOID FilterArg;
} KUNI_CO_HANDLE, * PKUNI_CO_HANDLE;

void KUNI_CO_CloseHandle(KUNI_CO_HANDLE* pHandle);

BOOL KUNI_CO_SendRaw(HANDLE hDevice, const BYTE* pcbFrame, USHORT cbFrame, OVERLAPPED* pOverlapped, DWORD Timeout);
BOOL KUNI_CO_SendOutput(KUNI_CO_HANDLE* pHandle, DWORD Timeout);

void KUNI_CO_ResetReceiveSignal(KUNI_CO_HANDLE* pHandle, KUNI_CO_FILTER_CALLBACK FilterCallback, PVOID FilterArg);

BOOL KUNI_CO_StartRecv(KUNI_CO_HANDLE* pHandle);
void KUNI_CO_StopRecv(KUNI_CO_HANDLE* pHandle);

BOOL KUNI_CO_FindDevice(USHORT VID, USHORT PID, BYTE ReportId, BYTE ReportByteLength, KUNI_CO_HANDLE* pHandle);

/*	Device arrival and removal, without polling: waiting for a dongle to come
 *	back becomes a wait instead of a sequence of guessed Sleep().
 */
typedef struct _KUNI_CO_WATCH {
	PVOID hNotify; // HCMNOTIFICATION, opaque here to keep cfgmgr32.h out of this header
	HANDLE hArrival;
} KUNI_CO_WATCH, * PKUNI_CO_WATCH;

BOOL KUNI_CO_WatchStart(KUNI_CO_WATCH* pWatch);
BOOL KUNI_CO_WatchWait(KUNI_CO_WATCH* pWatch, DWORD Timeout);
void KUNI_CO_WatchStop(KUNI_CO_WATCH* pWatch);
