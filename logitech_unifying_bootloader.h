/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#pragma once
#include "logitech_unifying_hidpp_common.h"

#define REPORT_ID_HIDPP_BL				0x00 // 33

#define REPORT_HIDPP_LENGTH_BL			33

#define BOOTLOADER_COMMAND_READ							    0x10
#define BOOTLOADER_COMMAND_WRITE						    0x20
#define BOOTLOADER_COMMAND_ERASE_PAGE			            0x30
#define BOOTLOADER_COMMAND_GET_CHIP_VERSION_STRING			0x40
#define BOOTLOADER_COMMAND_GET_FIRMWARE_FW_VERSION_STRING	0x50
#define BOOTLOADER_COMMAND_CHECKSUM							0x60 // byte sum on 16 bits from FirmwareFirst to FirmwareLatest (inc)
#define BOOTLOADER_COMMAND_REBOOT                           0x70
#define BOOTLOADER_COMMAND_GET_MEMORY_INFO                  0x80
#define BOOTLOADER_COMMAND_GET_BOOTLOADER_VERSION_STRING	0x90
#define BOOTLOADER_COMMAND_GET_FIRMWARE_BL_VERSION_STRING	0xa0

#define BOOTLOADER_MAX_DATA									(REPORT_HIDPP_LENGTH_BL - 5) // 28

#define BOOTLOADER_NORDIC_PAGE_SIZE							0x200
#define BOOTLOADER_NORDIC_PAGE_INFO_ADDR					0xfe00 // BL READ address alias for MCU InfoPage

#pragma pack(push, 1)
typedef struct _BOOTLOADER_MEMORY_INFO {
	WORD FirmwareFirst;
	WORD FirmwareLatest;
	WORD FirmwarePageSize;
} BOOTLOADER_MEMORY_INFO, *PBOOTLOADER_MEMORY_INFO;
#pragma pack(pop)

BOOL KUNI_BL_FindDevice(KUNI_CO_HANDLE* pHandle);
void KUNI_BL_CloseDevice(KUNI_CO_HANDLE* pHandle);

BOOL KUNI_BL_Read(KUNI_CO_HANDLE* pHandle, WORD Addr, BYTE* pbBuffer, BYTE cbWanted);
BOOL KUNI_BL_Write(KUNI_CO_HANDLE* pHandle, WORD Addr, const BYTE* pcbBuffer, BYTE cbToWrite);
BOOL KUNI_BL_ErasePage(KUNI_CO_HANDLE* pHandle, WORD Addr);
#define KUNI_BL_GetChipVersionString(pHandle, pbBuffer, pCbRead)			KUNI_BL_GetGenericVersionString(pHandle, BOOTLOADER_COMMAND_GET_CHIP_VERSION_STRING, pbBuffer, pCbRead)
#define KUNI_BL_GetFirmware_FW_VersionString(pHandle, pbBuffer, pCbRead)	KUNI_BL_GetGenericVersionString(pHandle, BOOTLOADER_COMMAND_GET_FIRMWARE_FW_VERSION_STRING, pbBuffer, pCbRead)
BOOL KUNI_BL_CheckSum(KUNI_CO_HANDLE* pHandle, WORD* pCRC16);
void KUNI_BL_Reboot(KUNI_CO_HANDLE* pHandle);
BOOL KUNI_BL_GetMemoryInfo(KUNI_CO_HANDLE* pHandle, BOOTLOADER_MEMORY_INFO* pInfo);
BOOL KUNI_BL_GetGenericVersionString(KUNI_CO_HANDLE* pHandle, BYTE Command, BYTE* pbBuffer, BYTE* pCbRead);
#define KUNI_BL_GetBootloaderVersionString(pHandle, pbBuffer, pCbRead)		KUNI_BL_GetGenericVersionString(pHandle, BOOTLOADER_COMMAND_GET_BOOTLOADER_VERSION_STRING, pbBuffer, pCbRead)
#define KUNI_BL_GetFirmware_BL_VersionString(pHandle, pbBuffer, pCbRead)	KUNI_BL_GetGenericVersionString(pHandle, BOOTLOADER_COMMAND_GET_FIRMWARE_BL_VERSION_STRING, pbBuffer, pCbRead)

/* Notes: https://blog.gentilkiwi.com/electronic/mcu/nordic/Logitech-dongle-C-U0007.html
 *
 *     C-U0003 - NRF24LU1+
 *     C-U0004 - ATMEGA32U2 + NRF24L01+
 *     C-U0007 - NRF24LU1+ / TI CC2544 (?) - Nano: 16k ?
 *     C-U0008 - TI CC2544
 *     C-U0010 - NRF31562A (?)
 *     C-U0011 - NRF31562A
 *     C-U0012 - TI CC2544 (?)
 *     C-U0013 - NRF24LU1+ (?)
 *     C-U0014 - NRF31562A
 *     C-U0016 - TI CC2544 (?)
 *     C-U0018 - NRF52840
 *     C-U0019 - TLSR8366
 *     C-U0021 - NRF52820
 */