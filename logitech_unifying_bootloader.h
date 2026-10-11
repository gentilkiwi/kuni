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
#define BOOTLOADER_COMMAND_SIGNATURE_READ					0xb0 // read back the RSA signature staging buffer
#define BOOTLOADER_COMMAND_SIGNATURE_WRITE					0xc0 // upload the RSA signature, by chunks

#define BOOTLOADER_MAX_DATA									(REPORT_HIDPP_LENGTH_BL - 5) // 28
#define BOOTLOADER_SIGNATURE_SIZE							0x100

#define BOOTLOADER_NORDIC_PAGE_SIZE							0x200
#define BOOTLOADER_NORDIC_PAGE_INFO_ADDR					0xfe00 // BL READ address alias for MCU InfoPage

#define BOOTLOADER_ERR_SUCCESS								0x00
#define BOOTLOADER_ERR_FAILED								0x01
#define BOOTLOADER_ERR_VERIFY_MISMATCH						0x02 // 0x22 - flash read-back differs from what was written
#define BOOTLOADER_ERR_NOT_INVALIDATED						0x03
#define BOOTLOADER_ERR_INVALID_SIGNATURE					0x04 // 0x24 - RSA-2048 / PKCS#1 v1.5 / SHA-256 check failed

/*	/!\ SILENT ADDRESS FOLDING /!\
 *
 *	A prologue runs BEFORE the opcode dispatch, for EVERY command:
 *
 *	    while ((WORD)(Addr + Cb - 1) >= 0x6800 && HIBYTE(Addr) < 0x6c)
 *	        Addr -= 0x400;
 *
 *	It hides the pairing key page behind the device data page. The second test is
 *	on the START address, which makes it airtight - and it is SILENT: status
 *	0x10, correct Cb, the requested address echoed back. A naive dump of
 *	0x6800-0x6bff returns a copy of 0x6400-0x67ff with nothing to signal it.
 */
#define BOOTLOADER_FOLD_FIRST								0x6800
#define BOOTLOADER_FOLD_AFTER								0x6c00
#define BOOTLOADER_FOLD_DELTA								0x0400

#pragma pack(push, 1)
typedef struct _BOOTLOADER_MEMORY_INFO {
	WORD FirmwareFirst;
	WORD FirmwareLatest;
	WORD FirmwarePageSize;
} BOOTLOADER_MEMORY_INFO, *PBOOTLOADER_MEMORY_INFO;
#pragma pack(pop)

BOOL KUNI_BL_FindDevice(KUNI_CO_HANDLE* pHandle);
void KUNI_BL_CloseDevice(KUNI_CO_HANDLE* pHandle);

WORD KUNI_BL_FoldedAddress(WORD Addr, BYTE Cb);
#define KUNI_BL_IsFolded(Addr, Cb)	(KUNI_BL_FoldedAddress(Addr, Cb) != (Addr))

BOOL KUNI_BL_Read(KUNI_CO_HANDLE* pHandle, WORD Addr, BYTE* pbBuffer, BYTE cbWanted);
BOOL KUNI_BL_Write(KUNI_CO_HANDLE* pHandle, WORD Addr, const BYTE* pcbBuffer, BYTE cbToWrite);
BOOL KUNI_BL_ErasePage(KUNI_CO_HANDLE* pHandle, WORD Addr);

BOOL KUNI_BL_SignatureWrite(KUNI_CO_HANDLE* pHandle, const BYTE pcbSignature[BOOTLOADER_SIGNATURE_SIZE]);
BOOL KUNI_BL_SignatureRead(KUNI_CO_HANDLE* pHandle, BYTE pbSignature[BOOTLOADER_SIGNATURE_SIZE]);

/*	Update sequence: erase the firmware pages, program the body from offset 1,
 *	upload the 256-byte RSA signature, then program byte 0 with Cb == 1. That
 *	last write is the ONLY one the signature gates (Addr == 0 && Cb == 1);
 *	skipping the signature step makes it answer 0x24.
 */
BOOL KUNI_BL_ProgramFirmware(KUNI_CO_HANDLE* pHandle, const BYTE* pcbFirmware, DWORD cbFirmware, const BYTE* pcbSignature);
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