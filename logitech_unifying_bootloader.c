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

const PCSTR KUNI_BL_ERR_Names[] = {"SUCCESS", "FAILED", "VERIFY_MISMATCH", "NOT_INVALIDATED", "INVALID_SIGNATURE",};

void KUNI_BL_DescribeErrorMessage(const BYTE* pcbBuffer) {
	BYTE Error;

	if (pcbBuffer) {
		Error = pcbBuffer[1] & 0x0f;
		kprintf(L"[ERR] Command: 0x%02hhx, Error: 0x%02hhx (%S) (Addr: 0x%02hhx%02hhx, Cb: 0x%02hhx)\n", pcbBuffer[1] & 0xf0, Error, (Error < ARRAYSIZE(KUNI_BL_ERR_Names)) ? KUNI_BL_ERR_Names[Error] : "?", pcbBuffer[2], pcbBuffer[3], pcbBuffer[4]);
	}
}

/*	Replays the folding loop of the bootloader prologue, exactly.
 *	See the big comment in the header: it runs before EVERY opcode.
 */
WORD KUNI_BL_FoldedAddress(WORD Addr, BYTE Cb) {
	BYTE i;

	for (i = 0; i < 16; i++) {
		/*	The firmware compares the 16-bit sum (Addr + Cb) against 0x6801,
		 *	NOT (Addr + Cb - 1) against 0x6800: the SETB C before the two SUBB
		 *	makes the total subtrahend 0x6801. The two differ exactly when
		 *	((Addr + Cb) & 0xffff) == 0, e.g. (0x0000, Cb 0).
		 */
		if (((WORD)(Addr + Cb)) <= BOOTLOADER_FOLD_FIRST) {
			break;
		}

		if (HIBYTE(Addr) >= HIBYTE(BOOTLOADER_FOLD_AFTER)) {
			break;
		}

		Addr -= BOOTLOADER_FOLD_DELTA;
	}

	return Addr;
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
	WORD Folded;

	inCb = min(cbToRead, BOOTLOADER_MAX_DATA);

	Folded = KUNI_BL_FoldedAddress(Addr, inCb);
	if (Folded != Addr) {
		PRINT_ERROR(L"0x%04hx+%hhu is silently folded to 0x%04hx - data will NOT be the one you asked for\n", Addr, inCb, Folded);
	}

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
	/*	Buffer[4] is the number of bytes the common tail re-reads AT THE COMMAND
	 *	ADDRESS and echoes back in the reply - hence outCb == what we put here.
	 *	The erased page itself is HIBYTE(Addr) >> 1, and LOBYTE(Addr) is unused
	 *	for the erase. Only the HIGH byte is bounded (page < 0x32), so a
	 *	0x63ff with Cb 0x1c over-reads 27 bytes up to 0x641a, into the NVM page.
	 */
	pHandle->Output.Buffer[4] = 0x01;

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

BOOL KUNI_BL_SignatureWriteChunk(KUNI_CO_HANDLE* pHandle, BYTE Offset, const BYTE* pcbBuffer, BYTE cbToWrite) {
	BOOL ret = FALSE;
	BYTE inCb, outCb;

	inCb = min(cbToWrite, BOOTLOADER_MAX_DATA);

	if ((Offset + inCb) <= BOOTLOADER_SIGNATURE_SIZE) {
		RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
		pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_SIGNATURE_WRITE;
		pHandle->Output.Buffer[2] = 0x00;
		pHandle->Output.Buffer[3] = Offset;
		pHandle->Output.Buffer[4] = inCb;
		RtlCopyMemory(pHandle->Output.Buffer + 5, pcbBuffer, inCb);

		if (KUNI_BL_SendRecv(pHandle, 1000)) {
			outCb = pHandle->Input.Buffer[4];
			if (outCb == inCb) {
				ret = TRUE;
			}
			else PRINT_ERROR(L"outCb (%hhu) != inCb (%hhu)\n", outCb, inCb);
		}
	}
	else PRINT_ERROR(L"Offset (0x%02hhx) + Cb (%hhu) > 0x%03x\n", Offset, inCb, BOOTLOADER_SIGNATURE_SIZE);

	return ret;
}

BOOL KUNI_BL_SignatureReadChunk(KUNI_CO_HANDLE* pHandle, BYTE Offset, BYTE* pbBuffer, BYTE cbToRead) {
	BOOL ret = FALSE;
	BYTE inCb, outCb;

	inCb = min(cbToRead, BOOTLOADER_MAX_DATA);

	if ((Offset + inCb) <= BOOTLOADER_SIGNATURE_SIZE) {
		RtlZeroMemory(pHandle->Output.Buffer, pHandle->Output.ByteLength);
		pHandle->Output.Buffer[1] = BOOTLOADER_COMMAND_SIGNATURE_READ;
		pHandle->Output.Buffer[2] = 0x00;
		pHandle->Output.Buffer[3] = Offset;
		pHandle->Output.Buffer[4] = inCb;

		if (KUNI_BL_SendRecv(pHandle, 1000)) {
			outCb = pHandle->Input.Buffer[4];
			if (outCb == inCb) {
				RtlCopyMemory(pbBuffer, pHandle->Input.Buffer + 5, outCb);
				ret = TRUE;
			}
			else PRINT_ERROR(L"outCb (%hhu) != inCb (%hhu)\n", outCb, inCb);
		}
	}
	else PRINT_ERROR(L"Offset (0x%02hhx) + Cb (%hhu) > 0x%03x\n", Offset, inCb, BOOTLOADER_SIGNATURE_SIZE);

	return ret;
}

BOOL KUNI_BL_SignatureWrite(KUNI_CO_HANDLE* pHandle, const BYTE pcbSignature[BOOTLOADER_SIGNATURE_SIZE]) {
	BOOL ret = TRUE;
	WORD Offset;
	BYTE chunk;

	for (Offset = 0; ret && (Offset < BOOTLOADER_SIGNATURE_SIZE); Offset += chunk) {
		chunk = (BYTE)min(BOOTLOADER_MAX_DATA, BOOTLOADER_SIGNATURE_SIZE - Offset);
		ret = KUNI_BL_SignatureWriteChunk(pHandle, (BYTE)Offset, pcbSignature + Offset, chunk);
	}

	return ret;
}

BOOL KUNI_BL_SignatureRead(KUNI_CO_HANDLE* pHandle, BYTE pbSignature[BOOTLOADER_SIGNATURE_SIZE]) {
	BOOL ret = TRUE;
	WORD Offset;
	BYTE chunk;

	for (Offset = 0; ret && (Offset < BOOTLOADER_SIGNATURE_SIZE); Offset += chunk) {
		chunk = (BYTE)min(BOOTLOADER_MAX_DATA, BOOTLOADER_SIGNATURE_SIZE - Offset);
		ret = KUNI_BL_SignatureReadChunk(pHandle, (BYTE)Offset, pbSignature + Offset, chunk);
	}

	return ret;
}

BOOL KUNI_BL_ProgramFirmware(KUNI_CO_HANDLE* pHandle, const BYTE* pcbFirmware, DWORD cbFirmware, const BYTE* pcbSignature) {
	BOOL ret = FALSE;
	BOOTLOADER_MEMORY_INFO Info;
	DWORD addr, offset;
	BYTE chunk;

	if (KUNI_BL_GetMemoryInfo(pHandle, &Info)) {
		kprintf(L"Firmware memory     : 0x%04hx - 0x%04hx (page size 0x%04hx)\n", Info.FirmwareFirst, Info.FirmwareLatest, Info.FirmwarePageSize);

		if ((Info.FirmwareFirst + cbFirmware - 1) <= Info.FirmwareLatest) {
			/*	Page 0 goes first, and on purpose: erasing it is the only
			 *	unconditional erase, and it is what sets flash[0] to 0xff, which
			 *	is the precondition every other page erase and every body write
			 *	is checked against.
			 */
			kprintf(L"Page erase   : [");
			for (addr = Info.FirmwareFirst - (Info.FirmwareFirst % Info.FirmwarePageSize); addr <= (Info.FirmwareFirst + cbFirmware - 1); addr += Info.FirmwarePageSize) {
				kprintf(L"%c", KUNI_BL_ErasePage(pHandle, (WORD)addr) ? L'#' : L'x');
			}
			kprintf(L"]\n");

			kprintf(L"Chunk program: [");
			for (offset = 1, chunk = (BYTE)min(0x10 - 1, cbFirmware - 1); offset < cbFirmware; offset += chunk, chunk = (BYTE)min(0x10, cbFirmware - offset)) {
				kprintf(L"%c", KUNI_BL_Write(pHandle, (WORD)(Info.FirmwareFirst + offset), pcbFirmware + offset, chunk) ? L'.' : L'x');
			}
			kprintf(L"]\n");

			if (pcbSignature) {
				kprintf(L"Signature    : %c\n", KUNI_BL_SignatureWrite(pHandle, pcbSignature) ? L'#' : L'x');
			}
			else PRINT_ERROR(L"No signature given - the last write will answer 0x24 (INVALID_SIGNATURE)\n");

			ret = KUNI_BL_Write(pHandle, Info.FirmwareFirst, pcbFirmware, 1);
			kprintf(L"Byte 0       : %c\n", ret ? L'.' : L'x');
		}
		else PRINT_ERROR(L"Firmware is too big (%u bytes) for 0x%04hx - 0x%04hx\n", cbFirmware, Info.FirmwareFirst, Info.FirmwareLatest);
	}

	return ret;
}
