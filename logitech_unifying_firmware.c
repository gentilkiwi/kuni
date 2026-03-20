/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_firmware.h"

BOOL KUNI_FW_FindDevice(HID_HANDLES* pHandles) {
	BOOL ret = FALSE;

	RtlZeroMemory(&pHandles->Short, sizeof(pHandles->Short));
	RtlZeroMemory(&pHandles->Long, sizeof(pHandles->Long));

	if (KUNI_CO_FindDevice(LOGITECH_VID, LOGITECH_UNIFYING_FW_PID, REPORT_ID_HIDPP_SHORT, REPORT_HIDPP_LENGTH_SHORT, &pHandles->Short)) {
		if (KUNI_CO_FindDevice(LOGITECH_VID, LOGITECH_UNIFYING_FW_PID, REPORT_ID_HIDPP_LONG, REPORT_HIDPP_LENGTH_LONG, &pHandles->Long)) {
			if (KUNI_CO_StartRecv(&pHandles->Short)) {
				if (KUNI_CO_StartRecv(&pHandles->Long)) {
					ret = TRUE;
				}
			}
		}
	}

	if (!ret) {
		KUNI_FW_CloseDevice(pHandles);
	}

	return ret;
}

void KUNI_FW_CloseDevice(HID_HANDLES* pHandles) {
	KUNI_CO_StopRecv(&pHandles->Long);
	KUNI_CO_StopRecv(&pHandles->Short);
	KUNI_CO_CloseHandle(&pHandles->Long);
	KUNI_CO_CloseHandle(&pHandles->Short);
}

const PCSTR KUNI_FW_ERR_Names[] = {
	"HIDPP_ERR_SUCCESS", "HIDPP_ERR_INVALID_SUBID", "HIDPP_ERR_INVALID_ADDRESS", "HIDPP_ERR_INVALID_VALUE",
	"HIDPP_ERR_CONNECT_FAIL", "HIDPP_ERR_TOO_MANY_DEVICES", "HIDPP_ERR_ALREADY_EXISTS", "HIDPP_ERR_BUSY",
	"HIDPP_ERR_UNKNOWN_DEVICE", "HIDPP_ERR_RESOURCE_ERROR", "HIDPP_ERR_REQUEST_UNAVAILABLE", "HIDPP_ERR_INVALID_PARAM_VALUE",
	"HIDPP_ERR_WRONG_PIN_CODE",
};

void KUNI_FW_DescribeErrorMessage(const KUNI_FW_MESSAGE* pErrorMessage) {
	if (pErrorMessage && pErrorMessage->SubId == HIDPP_MSG_ID_ERROR_MSG) {
		kprintf(L"[ERR] DeviceIndex: 0x%02hhx, SubId: 0x%02hhx, Register: 0x%02hhx, Code: 0x%02hhx (%S)\n",
			pErrorMessage->DeviceIndex, pErrorMessage->Register, pErrorMessage->Parameters[0], pErrorMessage->Parameters[1],
			(pErrorMessage->Parameters[1] < ARRAYSIZE(KUNI_FW_ERR_Names)) ? (KUNI_FW_ERR_Names[pErrorMessage->Parameters[1]] + 10) : "?"
		);
	}
}

BOOL CALLBACK KUNI_FW_Filter(const BYTE* buffer, const DWORD cbBuffer, PVOID pvArg) {
	BOOL ret = FALSE;
	const KUNI_FW_MESSAGE *pTmpBuffer, *pFilter;

	UNREFERENCED_PARAMETER(cbBuffer);

	pTmpBuffer = (const KUNI_FW_MESSAGE*)buffer;
	pFilter = (const KUNI_FW_MESSAGE*)pvArg;

	if (!pFilter) {
		ret = TRUE;
	}
	else if (pTmpBuffer->DeviceIndex == pFilter->DeviceIndex) {
		if (
			((pTmpBuffer->SubId == pFilter->SubId) && (pTmpBuffer->Register == pFilter->Register))
			||
			((pTmpBuffer->SubId == HIDPP_MSG_ID_ERROR_MSG) && (pTmpBuffer->Register == pFilter->SubId) && (pTmpBuffer->Parameters[0] == pFilter->Register))
			) {
			ret = TRUE;
		}
	}

	return ret;
}

void KUNI_FW_ResetReceiveSignals(HID_HANDLES* pHandles, PVOID pSourceMessageForFilter, BYTE Target) {
	if (pSourceMessageForFilter) {
		KUNI_CO_ResetReceiveSignal(&pHandles->Short, KUNI_FW_Filter, pSourceMessageForFilter);
		if (Target == REPORT_ID_HIDPP_LONG) {
			KUNI_CO_ResetReceiveSignal(&pHandles->Long, KUNI_FW_Filter, pSourceMessageForFilter);
		}
	}
	else {
		KUNI_CO_ResetReceiveSignal(&pHandles->Short, NULL, NULL);
		KUNI_CO_ResetReceiveSignal(&pHandles->Long, NULL, NULL);
	}
}

BOOL KUNI_FW_WaitForSingleReceiveSignal(HID_HANDLES* pHandles, BYTE Target, DWORD Timeout, KUNI_FW_MESSAGE **ppErrorMessage) {
	BOOL ret = FALSE;

	HANDLE hEvents[2];
	DWORD cbEvents = 0, dwWait;

	hEvents[cbEvents++] = pHandles->Short.hSignalToRead;
	if (Target == REPORT_ID_HIDPP_LONG) {
		hEvents[cbEvents++] = pHandles->Long.hSignalToRead;
	}

	if (ppErrorMessage) {
		*ppErrorMessage = NULL;
	}

	dwWait = WaitForMultipleObjects(cbEvents, hEvents, FALSE, Timeout);
	switch (dwWait) {
	case WAIT_OBJECT_0: // Short
		if (((PKUNI_FW_MESSAGE) pHandles->Short.Input.Buffer)->SubId == HIDPP_MSG_ID_ERROR_MSG) {
			if (ppErrorMessage) {
				*ppErrorMessage = (PKUNI_FW_MESSAGE) pHandles->Short.Input.Buffer;
			}
			else {
				KUNI_FW_DescribeErrorMessage((PKUNI_FW_MESSAGE) pHandles->Short.Input.Buffer);
			}
		}
		else if (Target == REPORT_ID_HIDPP_SHORT) {
			ret = TRUE;
		}
		else PRINT_ERROR(L"Short report != error for not Short target?\n");

		break;

	case WAIT_OBJECT_0 + 1: // Long
		ret = TRUE;

		break;

	case WAIT_ABANDONED_0:
	case WAIT_ABANDONED_0 + 1:
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

BOOL KUNI_FW_SendRecv(HID_HANDLES* pHandles, PKUNI_CO_HANDLE pMedia, BYTE ToRecvType, DWORD Timeout) {
	BOOL ret = FALSE;

	pMedia->Output.Buffer[0] = pMedia->Output.Id;
	KUNI_FW_ResetReceiveSignals(pHandles, pMedia->Output.Buffer, ToRecvType);
	if (KUNI_CO_SendOutput(pMedia, Timeout)) {
		if (KUNI_FW_WaitForSingleReceiveSignal(pHandles, ToRecvType, Timeout, NULL)) {
			KUNI_FW_ResetReceiveSignals(pHandles, NULL, ToRecvType);
			ret = TRUE;
		}
	}

	return ret;
}

BOOL KUNI_FW_SetRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Value[3], DWORD Timeout) {
	BOOL ret = FALSE;
	PKUNI_CO_HANDLE pHandle = &pHandles->Short;

	pHandle->Output.Buffer[1] = DeviceIndex;
	pHandle->Output.Buffer[2] = HIDPP_MSG_ID_SET_REGISTER;
	pHandle->Output.Buffer[3] = Register;
	RtlCopyMemory(pHandle->Output.Buffer + 4, Value, 3);

	if (KUNI_FW_SendRecv(pHandles, pHandle, REPORT_ID_HIDPP_SHORT, Timeout))
	{
		ret = TRUE;
	}

	return ret;
}

BOOL KUNI_FW_GetRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[3], DWORD Timeout) {
	BOOL ret = FALSE;
	PKUNI_CO_HANDLE pHandle = &pHandles->Short;

	pHandle->Output.Buffer[1] = DeviceIndex;
	pHandle->Output.Buffer[2] = HIDPP_MSG_ID_GET_REGISTER;
	pHandle->Output.Buffer[3] = Register;
	RtlCopyMemory(pHandle->Output.Buffer + 4, Parameters, 3);

	if (KUNI_FW_SendRecv(pHandles, pHandle, REPORT_ID_HIDPP_SHORT, Timeout)) {
		if (Value) {
			RtlCopyMemory(Value, ((PKUNI_FW_MESSAGE) pHandles->Short.Input.Buffer)->Parameters, 3);
		}

		ret = TRUE;
	}

	return ret;
}

BOOL KUNI_FW_SetLongRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Value[16], DWORD Timeout) {
	BOOL ret = FALSE;
	PKUNI_CO_HANDLE pHandle = &pHandles->Long;

	pHandle->Output.Buffer[1] = DeviceIndex;
	pHandle->Output.Buffer[2] = HIDPP_MSG_ID_SET_LONG_REGISTER;
	pHandle->Output.Buffer[3] = Register;
	RtlCopyMemory(pHandle->Output.Buffer + 4, Value, 16);

	if (KUNI_FW_SendRecv(pHandles, pHandle, REPORT_ID_HIDPP_SHORT, Timeout)) {
		ret = TRUE;
	}

	return ret;
}

BOOL KUNI_FW_GetLongRegister(HID_HANDLES* pHandles, BYTE DeviceIndex, BYTE Register, BYTE Parameters[3], BYTE Value[16], DWORD Timeout) {
	BOOL ret = FALSE;
	PKUNI_CO_HANDLE pHandle = &pHandles->Short;

	pHandle->Output.Buffer[1] = DeviceIndex;
	pHandle->Output.Buffer[2] = HIDPP_MSG_ID_GET_LONG_REGISTER;
	pHandle->Output.Buffer[3] = Register;
	RtlCopyMemory(pHandle->Output.Buffer + 4, Parameters, 3);

	if (KUNI_FW_SendRecv(pHandles, pHandle, REPORT_ID_HIDPP_LONG, Timeout)) {
		if (Value) {
			RtlCopyMemory(Value, ((PKUNI_FW_MESSAGE) pHandles->Long.Input.Buffer)->Parameters, 16);
		}

		ret = TRUE;
	}

	return ret;
}
