/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_bootloader.h"

BOOL KUNI_BL_FindDevice(KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;

	RtlZeroMemory(pHandle, sizeof(*pHandle));

	if (KUNI_CO_FindDevice(LOGITECH_VID, LOGITECH_UNIFYING_BL_PID, REPORT_ID_HIDPP_BL, REPORT_HIDPP_LENGTH_BL, pHandle)) {
		if (KUNI_CO_StartRecv(pHandle)) {
			ret = TRUE;
		}
	}

	if (!ret) {
		KUNI_BL_CloseDevice(pHandle);
	}

	return ret;
}

void KUNI_BL_CloseDevice(KUNI_CO_HANDLE* pHandle) {
	KUNI_CO_StopRecv(pHandle);
	KUNI_CO_CloseHandle(pHandle);
}

void KUNI_BL_DescribeErrorMessage(const BYTE *pcbBuffer) {
	if (pcbBuffer) {
		kprintf(L"[ERR] Command: 0x%02hhx, Error: 0x%02hhx (Addr: 0x%02hhx%02hhx, Cb: 0x%02hhx)\n", pcbBuffer[1] & 0xf0, pcbBuffer[1] & 0x0f, pcbBuffer[2], pcbBuffer[3], pcbBuffer[4]);
	}
}

BOOL CALLBACK KUNI_BL_Filter(const BYTE* buffer, const DWORD cbBuffer, PVOID pvArg) {
	BOOL ret = FALSE;
	const BYTE* pFilterRef;
	BYTE maskedCmd;

	UNREFERENCED_PARAMETER(cbBuffer);

	pFilterRef = (const BYTE*)pvArg;
	if (!buffer[0]) {
		maskedCmd = buffer[1] & 0xf0;
		if ((maskedCmd == pFilterRef[1]) || !maskedCmd) {
			ret = TRUE;
		}
	}

	return ret;
}

BOOL KUNI_BL_WaitForSingleReceiveSignal(KUNI_CO_HANDLE* pHandle, DWORD Timeout, PVOID* ppErrorMessage) {
	BOOL ret = FALSE;
	DWORD dwWait;

	if (ppErrorMessage) {
		*ppErrorMessage = NULL;
	}

	dwWait = WaitForSingleObject(pHandle->hSignalToRead, Timeout);
	switch (dwWait) {
	case WAIT_OBJECT_0:
		if (!pHandle->Input.Buffer[1] || (pHandle->Input.Buffer[1] & 0x0f)) {
			if (ppErrorMessage) {
				*ppErrorMessage = (PVOID)pHandle->Input.Buffer;
			}
			else KUNI_BL_DescribeErrorMessage(pHandle->Input.Buffer);
		}
		else {
			ret = TRUE;
		}

		break;

	case WAIT_ABANDONED_0:
		PRINT_ERROR(L"Abandonned ?\n");

		break;

	case WAIT_TIMEOUT:

		break;

	case WAIT_FAILED:
	default:
		PRINT_ERROR_AUTO(L"WaitForMultipleObjects");
	}

	return ret;
}

BOOL KUNI_BL_SendRecv(KUNI_CO_HANDLE *pHandle, DWORD Timeout) {
	BOOL ret = FALSE;
	
	KUNI_CO_ResetReceiveSignal(pHandle, KUNI_BL_Filter, pHandle->Output.Buffer);
	if (KUNI_CO_SendOutput(pHandle, Timeout)) {
		if (KUNI_BL_WaitForSingleReceiveSignal(pHandle, Timeout, NULL)) {
			KUNI_CO_ResetReceiveSignal(pHandle, NULL, NULL);
			ret = TRUE;
		}
	}

	return ret;
}

BOOL KUNI_BL_Read(KUNI_CO_HANDLE* pHandle, WORD Addr, BYTE *pbBuffer, BYTE cbToRead) {
	BOOL ret = FALSE;
	BYTE inCb, outCb;

	inCb = min(cbToRead, BOOTLOADER_MAX_DATA);

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_READ;
	pHandle->Output.Buffer[2] = HIBYTE(Addr);
	pHandle->Output.Buffer[3] = LOBYTE(Addr);
	pHandle->Output.Buffer[4] = inCb;

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb == inCb) {
			RtlCopyMemory(pbBuffer, pHandle->Input.Buffer + 5, outCb);
			ret = TRUE;
		}
		else {
			PRINT_ERROR(L"outCb (%hhu) != inCb (%hhu)\n", outCb, inCb);
			kprinthex(pHandle->Input.Buffer, pHandle->Input.ByteLength);
		}
	}

	return ret;
}

BOOL KUNI_BL_Write(KUNI_CO_HANDLE* pHandle, WORD Addr, const BYTE *pcbBuffer, BYTE cbToWrite) {
	BOOL ret = FALSE;
	BYTE inCb, outCb;

	inCb = min(cbToWrite, BOOTLOADER_MAX_DATA);

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_WRITE;
	pHandle->Output.Buffer[2] = HIBYTE(Addr);
	pHandle->Output.Buffer[3] = LOBYTE(Addr);
	pHandle->Output.Buffer[4] = inCb;
	RtlCopyMemory(pHandle->Output.Buffer + 5, pcbBuffer, inCb);

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb == inCb) {
			ret = TRUE;
		}
		else PRINT_ERROR(L"outCb (%hhu) != inCb (%hhu)\n", outCb, inCb);
	}

	return ret;
}

BOOL KUNI_BL_ErasePage(KUNI_CO_HANDLE* pHandle, WORD Addr) {
	BOOL ret = FALSE;
	BYTE outCb;

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_ERASE_PAGE;
	pHandle->Output.Buffer[2] = HIBYTE(Addr);
	pHandle->Output.Buffer[3] = LOBYTE(Addr);
	pHandle->Output.Buffer[4] = 0x01; // ? (it's not number of pages)

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb == pHandle->Output.Buffer[4]) {
			ret = TRUE;
		}
		else PRINT_ERROR(L"outCb (%hhu) != cbToTest (%hhu) - 1?\n", outCb, pHandle->Output.Buffer[4]);
	}

	return ret;
}

BOOL KUNI_BL_CheckSum(KUNI_CO_HANDLE* pHandle, WORD* pCRC16) {
	BOOL ret = FALSE;
	BYTE outCb;

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_CHECKSUM;

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb == sizeof(*pCRC16)) {
			*pCRC16 = MAKEWORD(pHandle->Input.Buffer[6], pHandle->Input.Buffer[5]);
			ret = TRUE;
		}
	}

	return ret;
}

void KUNI_BL_Reboot(KUNI_CO_HANDLE* pHandle) {
	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_REBOOT;

	KUNI_BL_SendRecv(pHandle, 0);
}

BOOL KUNI_BL_GetMemoryInfo(KUNI_CO_HANDLE* pHandle, BOOTLOADER_MEMORY_INFO* pInfo) {
	BOOL ret = FALSE;
	BYTE outCb;

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_GET_MEMORY_INFO;

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb == sizeof(*pInfo)) {
			pInfo->FirmwareFirst = MAKEWORD(pHandle->Input.Buffer[6], pHandle->Input.Buffer[5]);
			pInfo->FirmwareLatest = MAKEWORD(pHandle->Input.Buffer[8], pHandle->Input.Buffer[7]);
			pInfo->FirmwarePageSize = MAKEWORD(pHandle->Input.Buffer[10], pHandle->Input.Buffer[9]);

			ret = TRUE;
		}
		else PRINT_ERROR(L"outCb (%hhu) != szBOOTLOADER_MEMORY_INFO (%hhu)\n", outCb, (BYTE) sizeof(*pInfo));
	}

	return ret;
}

BOOL KUNI_BL_GetGenericVersionString(KUNI_CO_HANDLE* pHandle, BYTE Command, BYTE* pbBuffer, BYTE* pCbRead) {
	BOOL ret = FALSE;
	BYTE outCb;

	RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
	pHandle->Output.Buffer[1] = Command;

	if (KUNI_BL_SendRecv(pHandle, 1000)) {
		outCb = pHandle->Input.Buffer[4];
		if (outCb <= BOOTLOADER_MAX_DATA)
		{
			RtlCopyMemory(pbBuffer, pHandle->Input.Buffer + 5, outCb);
			if (pCbRead) {
				*pCbRead = outCb;
			}

			ret = TRUE;
		}
		else PRINT_ERROR(L"outCb (%hhu) > BOOTLOADER_MAX_DATA (%hhu) - Cmd is 0x%02hhx\n", outCb, BOOTLOADER_MAX_DATA, Command);
	}

	return ret;
}
