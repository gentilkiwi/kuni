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

#define REGISTER_KIWI					'k'

#define KIWI_FLASH_ADDRESS_FLAG			0b00000000
#define KIWI_INFOPAGE_ADDRESS_FLAG		0b10000000

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
BOOL KUNI_FW_GetRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[3], DWORD Timeout);
BOOL KUNI_FW_SetLongRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Value[16], DWORD Timeout);
BOOL KUNI_FW_GetLongRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[16], DWORD Timeout);

// ICP      - In-Circuit Programming (Bootloader)
// LT{slot} - 
// LTu      - Free pairing
// XIT      - Stop pairing

#define KUNI_FW_EnterBootloader(pHandles)	KUNI_FW_SetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_UPDATE, (BYTE[]) { 'I', 'C', 'P' }, 0)