/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#pragma once
#include "logitech_unifying_hidpp_common.h"

#define REPORT_ID_HIDPP_SHORT           0x10 // 7b
#define REPORT_ID_HIDPP_LONG			0x11 // 20b
#define REPORT_ID_HIDPP_VERY_LONG		0x12 // 64b

#define REPORT_HIDPP_LENGTH_SHORT		7
#define REPORT_HIDPP_LENGTH_LONG		20
#define REPORT_HIDPP_LENGTH_VERY_LONG	64

#define HIDPP_MSG_ID_SET_REGISTER       0x80
#define HIDPP_MSG_ID_GET_REGISTER       0x81
#define HIDPP_MSG_ID_SET_LONG_REGISTER  0x82
#define HIDPP_MSG_ID_GET_LONG_REGISTER  0x83
#define HIDPP_MSG_ID_ERROR_MSG			0x8f

#define HIDPP_ERR_SUCCESS				0x00
#define HIDPP_ERR_INVALID_SUBID			0x01
#define HIDPP_ERR_INVALID_ADDRESS		0x02
#define HIDPP_ERR_INVALID_VALUE			0x03
#define HIDPP_ERR_CONNECT_FAIL			0x04
#define HIDPP_ERR_TOO_MANY_DEVICES		0x05
#define HIDPP_ERR_ALREADY_EXISTS		0x06
#define HIDPP_ERR_BUSY					0x07
#define HIDPP_ERR_UNKNOWN_DEVICE		0x08
#define HIDPP_ERR_RESOURCE_ERROR		0x09
#define HIDPP_ERR_REQUEST_UNAVAILABLE	0x0a
#define HIDPP_ERR_INVALID_PARAM_VALUE	0x0b
#define HIDPP_ERR_WRONG_PIN_CODE		0x0c

#define DEVICE_INDEX_RECEIVER			0xff

#define REGISTER_WIRELESS_NOTIFICATIONS	0x00
#define REGISTER_01						0x01
#define REGISTER_CONNECTION_STATE       0x02
#define REGISTER_03						0x03
#define REGISTER_PAIRING                0xb2
#define REGISTER_DEVICE_ACTIVITY        0xb3
#define REGISTER_PAIRING_INFORMATION    0xb5
#define REGISTER_D0						0xd0
#define REGISTER_D1						0xd1
#define REGISTER_D4						0xd4
#define REGISTER_D7						0xd7
#define REGISTER_E4						0xe4
#define REGISTER_E6						0xe6
#define REGISTER_FIRMWARE_UPDATE        0xf0
#define REGISTER_FIRMWARE_INFO          0xf1

#pragma pack(push, 1)
typedef struct _KUNI_FW_MESSAGE {
	BYTE ReportId;
	BYTE DeviceIndex;
	BYTE SubId;
	BYTE Register;
	BYTE Parameters[ANYSIZE_ARRAY];
} KUNI_FW_MESSAGE, *PKUNI_FW_MESSAGE;
#pragma pack(pop)

typedef struct _HID_HANDLES {
	KUNI_CO_HANDLE Short;
	KUNI_CO_HANDLE Long;
} HID_HANDLES, *PHID_HANDLES;

BOOL KUNI_FW_FindDevice(HID_HANDLES* pHandles);
void KUNI_FW_CloseDevice(HID_HANDLES* pHandles);

BOOL KUNI_FW_SetRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Value[3], DWORD Timeout);
BOOL KUNI_FW_SetLongRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Value[16], DWORD Timeout);
BOOL KUNI_FW_GetRegisterEx(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[3], DWORD Timeout, PKUNI_FW_MESSAGE* ppErrorMessage);
#define KUNI_FW_GetRegister(...)		KUNI_FW_GetRegisterEx(__VA_ARGS__, NULL)		// variadic: les args portent des litterals {a, b, c} dont les virgules casseraient une macro classique
BOOL KUNI_FW_GetLongRegisterEx(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[16], DWORD Timeout, PKUNI_FW_MESSAGE* ppErrorMessage);
#define KUNI_FW_GetLongRegister(...)	KUNI_FW_GetLongRegisterEx(__VA_ARGS__, NULL)

// ICP      - In-Circuit Programming (Bootloader)
// LT{slot} - 
// LTu      - Free pairing
// XIT      - Stop pairing

#define KUNI_FW_EnterBootloader(pHandles)	KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_UPDATE, (BYTE[]) { 'I', 'C', 'P' }, 0)
#define KUNI_FW_StopPairing(pHandles)		KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_UPDATE, (BYTE[]) { 'X', 'I', 'T' }, 1000)
#define KUNI_FW_FreePairing(pHandles)		KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_UPDATE, (BYTE[]) { 'L', 'T', 'u' }, 1000)
#define KUNI_FW_PairSlot(pHandles, slot)	KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_UPDATE, (BYTE[]) { 'L', 'T', (slot) }, 1000)

#define KUNI_FW_EnterFactoryMode(pHandles)	KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_D0, (BYTE[]) { 0x01, 0x00, 0x00 }, 1000)

#define PAIRING_INFO_CAPABILITIES		8
#define PAIRING_CAPABILITY_ENCRYPTED	0x01

/*	A paired device as the receiver stores it: the 0x20|slot pairing record, plus the
 *	0x30|slot extended block and the 0x40|slot name when they read back. KUNI_FW_ReadDevice
 *	fills it and KUNI_FW_PrintDevice renders it; both the info dump and the key dump use them.
 */
typedef struct _KUNI_FW_DEVICE {
	BOOL bPaired;		// the 0x20|slot record exists
	BOOL bUnreadable;	// it failed for another reason than "no such record"
	BOOL bExtended;
	BOOL bName;
	BYTE Pairing[16];
	BYTE Extended[16];
	BYTE Name[16];
} KUNI_FW_DEVICE, * PKUNI_FW_DEVICE;

BOOL KUNI_FW_ReadPairing(HID_HANDLES* pHandles, BYTE SubId, BYTE Value[16], PKUNI_FW_MESSAGE* ppError);
void KUNI_FW_ReadDevice(HID_HANDLES* pHandles, BYTE Device, KUNI_FW_DEVICE* pDevice);
void KUNI_FW_PrintDevice(BYTE Device, const KUNI_FW_DEVICE* pDevice);