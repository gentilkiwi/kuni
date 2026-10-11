/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_dj.h"

BOOL KUNI_DJ_FindDevice(KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;

	RtlZeroMemory(pHandle, sizeof(*pHandle));

	// id 0x20 (the SHORT report, the one we send) but the LONG byte length: both DJ reports
	// (0x20/0x21) share one Windows collection, whose ReportByteLength is the max of the two = 32
	if (KUNI_CO_FindDevice(LOGITECH_VID, LOGITECH_UNIFYING_FW_PID, REPORT_ID_DJ_SHORT, REPORT_DJ_LENGTH_LONG, pHandle)) {
		if (KUNI_CO_StartRecv(pHandle)) {
			ret = TRUE;
		}
	}

	if (!ret) {
		KUNI_DJ_CloseDevice(pHandle);
	}

	return ret;
}

void KUNI_DJ_CloseDevice(KUNI_CO_HANDLE* pHandle) {
	KUNI_CO_StopRecv(pHandle);
	KUNI_CO_CloseHandle(pHandle);
}

void KUNI_DJ_BuildWriteFrame(BYTE Frame[REPORT_DJ_LENGTH_SHORT], WORD Address, BYTE Value) {
	RtlZeroMemory(Frame, REPORT_DJ_LENGTH_SHORT);
	Frame[0] = REPORT_ID_DJ_SHORT;
	Frame[1] = KUNI_DJ_DeviceIndexForAddress(Address);
	Frame[2] = DJ_TYPE_LEDS;
	Frame[3] = Value;
}
