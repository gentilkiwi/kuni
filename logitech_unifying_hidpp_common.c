/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_hidpp_common.h"

BOOL KUNI_CO_AllocReports(KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;

	pHandle->Input.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Input.ByteLength);
	pHandle->Output.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Output.ByteLength);
	pHandle->OutputOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (pHandle->Input.Buffer && pHandle->Output.Buffer && pHandle->OutputOverlapped.hEvent) {
		ret = TRUE;
	}
	else PRINT_ERROR_AUTO(L"LocalAlloc/CreateEvent");

	return ret;
}

/*	Searches the collection for a given ReportID and returns its collection
 *	ByteLength. Unlike a "single button cap" match, this handles a collection that
 *	carries several reports (the two DJ reports 0x20/0x21 share one PDO whose
 *	ByteLength is the MAXIMUM of the two) - so every collection opens the same way.
 */
static BOOL KUNI_CO_ReportIdPresent(PHIDP_PREPARSED_DATA PreparsedData, HIDP_REPORT_TYPE ReportType, BYTE WantedId, USHORT* pByteLength) {
	BOOL ret = FALSE;
	HIDP_CAPS Caps;
	PHIDP_BUTTON_CAPS pButtonCaps;
	USHORT nb = 0, i;

	if (HidP_GetCaps(PreparsedData, &Caps) == HIDP_STATUS_SUCCESS) {
		switch (ReportType) {
		case HidP_Input:
			nb = Caps.NumberInputButtonCaps;
			*pByteLength = Caps.InputReportByteLength;
			break;
		case HidP_Output:
			nb = Caps.NumberOutputButtonCaps;
			*pByteLength = Caps.OutputReportByteLength;
			break;
		case HidP_Feature:
			nb = Caps.NumberFeatureButtonCaps;
			*pByteLength = Caps.FeatureReportByteLength;
			break;
		default:
			break; // nb reste 0 : ret reste FALSE
		}

		if (nb) {
			pButtonCaps = (PHIDP_BUTTON_CAPS)LocalAlloc(LPTR, nb * sizeof(HIDP_BUTTON_CAPS));
			if (pButtonCaps) {
				if (HidP_GetButtonCaps(ReportType, pButtonCaps, &nb, PreparsedData) == HIDP_STATUS_SUCCESS) {
					for (i = 0; i < nb; i++) {
						if (pButtonCaps[i].ReportID == WantedId) {
							ret = TRUE;
							break;
						}
					}
				}
				LocalFree(pButtonCaps);
			}
		}
	}

	return ret;
}

BOOL KUNI_CO_OpenHandle_with_checks(LPCWSTR DevicePath, USHORT VID, USHORT PID, BYTE ReportId, BYTE ReportByteLength, KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;
	PHIDP_PREPARSED_DATA PreparsedData;

	RtlZeroMemory(pHandle, sizeof(*pHandle));

	pHandle->hDevice = CreateFile(DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (pHandle->hDevice && (pHandle->hDevice != INVALID_HANDLE_VALUE)) {
		if (HidD_GetAttributes(pHandle->hDevice, &pHandle->Attributes)) {
			if ((pHandle->Attributes.VendorID == VID) && (pHandle->Attributes.ProductID == PID)) {
				if (HidD_GetPreparsedData(pHandle->hDevice, &PreparsedData)) {
					if (KUNI_CO_ReportIdPresent(PreparsedData, HidP_Input, ReportId, &pHandle->Input.ByteLength) && KUNI_CO_ReportIdPresent(PreparsedData, HidP_Output, ReportId, &pHandle->Output.ByteLength)) {
						if ((pHandle->Input.ByteLength == ReportByteLength) && (pHandle->Output.ByteLength == ReportByteLength)) {
							pHandle->Input.Id = ReportId;
							pHandle->Output.Id = ReportId;

							kprintf(L"Device: %s\n", DevicePath);
							kprintf(L"  [A] VID: 0x%04hx - PID: 0x%04hx - Ver: 0x%04hx\n", pHandle->Attributes.VendorID, pHandle->Attributes.ProductID, pHandle->Attributes.VersionNumber);
							kprintf(L"  [C] ReportByteLength {Input: %2hu ; Output: %2hu}\n  [I] ReportId {Input: 0x%02hhx ; Output: 0x%02hhx}\n", pHandle->Input.ByteLength, pHandle->Output.ByteLength, pHandle->Input.Id, pHandle->Output.Id);

							ret = KUNI_CO_AllocReports(pHandle);
						}
					}

					HidD_FreePreparsedData(PreparsedData);
				}
				else PRINT_ERROR_AUTO(L"HidD_GetPreparsedData");
			}
			// else not valid
		}
	}
	// else PRINT_ERROR_AUTO(L"CreateFile");

	if (!ret) {
		KUNI_CO_CloseHandle(pHandle);
	}

	return ret;
}

void KUNI_CO_CloseHandle(KUNI_CO_HANDLE* pHandle) {
	DWORD dwNumberOfBytesWritten;

	if (pHandle) {
		/*	A fire-and-forget write (DFU enter, reboot: Timeout 0) is still in flight
		 *	here. Cancelling it outright is how a fast host loses the command entirely
		 *	- the close beats the USB write onto the wire, so the dongle never sees the
		 *	DFU request (stays in the firmware) or the reboot (stays in the bootloader).
		 *	Wait for the write to actually reach the device first; only a genuinely
		 *	stuck write (device already gone) is cancelled, bounded so close never hangs.
		 */
		if (pHandle->OutputPending) {
			if (WaitForSingleObject(pHandle->OutputOverlapped.hEvent, 1000) != WAIT_OBJECT_0) {
				CancelIoEx(pHandle->hDevice, &pHandle->OutputOverlapped);
			}
			GetOverlappedResult(pHandle->hDevice, &pHandle->OutputOverlapped, &dwNumberOfBytesWritten, TRUE);
			pHandle->OutputPending = FALSE;
		}

		if (pHandle->OutputOverlapped.hEvent) {
			CloseHandle(pHandle->OutputOverlapped.hEvent);
		}

		if (pHandle->Input.Buffer) {
			LocalFree(pHandle->Input.Buffer);
		}

		if (pHandle->Output.Buffer) {
			LocalFree(pHandle->Output.Buffer);
		}

		if (pHandle->hDevice && (pHandle->hDevice != INVALID_HANDLE_VALUE)) {
			CloseHandle(pHandle->hDevice);
		}

		RtlZeroMemory(pHandle, sizeof(*pHandle));
	}
}

BOOL KUNI_CO_SendRaw(HANDLE hDevice, const BYTE* pcbFrame, USHORT cbFrame, OVERLAPPED* pOverlapped, DWORD Timeout) {
	BOOL ret;
	DWORD dwNumberOfBytesWritten, dwRes;

	ResetEvent(pOverlapped->hEvent);

	ret = WriteFile(hDevice, pcbFrame, cbFrame, &dwNumberOfBytesWritten, pOverlapped);
	if (!ret) {
		dwRes = GetLastError();
		if (dwRes == ERROR_IO_PENDING) {
			if (!Timeout) {
				ret = TRUE; // left in flight on purpose, the caller reaps it
			}
			else if (WaitForSingleObject(pOverlapped->hEvent, Timeout) == WAIT_OBJECT_0) {
				ret = GetOverlappedResult(hDevice, pOverlapped, &dwNumberOfBytesWritten, FALSE);
			}
			else {
				// a real deadline was missed: THAT is when cancelling is right
				CancelIoEx(hDevice, pOverlapped);
				GetOverlappedResult(hDevice, pOverlapped, &dwNumberOfBytesWritten, TRUE);
			}
		}
	}

	return ret;
}

BOOL KUNI_CO_SendOutput(KUNI_CO_HANDLE* pHandle, DWORD Timeout) {
	BOOL ret;
	DWORD dwNumberOfBytesWritten;

	if (pHandle->OutputPending) { // reap the previous fire-and-forget
		GetOverlappedResult(pHandle->hDevice, &pHandle->OutputOverlapped, &dwNumberOfBytesWritten, TRUE);
		pHandle->OutputPending = FALSE;
	}

	pHandle->Output.Buffer[0] = pHandle->Output.Id;

	ret = KUNI_CO_SendRaw(pHandle->hDevice, pHandle->Output.Buffer, pHandle->Output.ByteLength, &pHandle->OutputOverlapped, Timeout);
	if (ret && !Timeout) {
		pHandle->OutputPending = TRUE;
	}

	return ret;
}

void KUNI_CO_ResetReceiveSignal(KUNI_CO_HANDLE* pHandle, KUNI_CO_FILTER_CALLBACK FilterCallback, PVOID FilterArg) {
	if (FilterCallback) {
		ResetEvent(pHandle->hSignalToRead);
	}
	pHandle->FilterArg = FilterArg;
	pHandle->FilterCallback = FilterCallback;
}

unsigned __stdcall KUNI_CO_RecvThread(void* pParam) {
	BOOL ret;
	KUNI_CO_HANDLE* pHandle = (KUNI_CO_HANDLE*)pParam;
	DWORD dwNumberOfBytesRead, dwRes;
	OVERLAPPED Overlapped = { 0 };
	HANDLE hEvents[2];
	BYTE* pTmpBuffer;

	pTmpBuffer = (BYTE*)LocalAlloc(LPTR, pHandle->Input.ByteLength);
	if (pTmpBuffer) {
		Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (Overlapped.hEvent) {
			hEvents[0] = Overlapped.hEvent;
			hEvents[1] = pHandle->hStopEvent;

			while (WaitForSingleObject(pHandle->hStopEvent, 0) == WAIT_TIMEOUT) {
				ResetEvent(Overlapped.hEvent);
				ret = ReadFile(pHandle->hDevice, pTmpBuffer, pHandle->Input.ByteLength, &dwNumberOfBytesRead, &Overlapped);
				if (!ret) {
					dwRes = GetLastError();
					if (dwRes == ERROR_IO_PENDING) {
						dwRes = WaitForMultipleObjects(ARRAYSIZE(hEvents), hEvents, FALSE, INFINITE);
						if (dwRes == WAIT_OBJECT_0) {
							if (GetOverlappedResult(pHandle->hDevice, &Overlapped, &dwNumberOfBytesRead, FALSE)) {
								ret = TRUE;
							}
							else PRINT_ERROR_AUTO(L"GetOverlappedResult");
						}
						else if (dwRes == WAIT_OBJECT_0 + 1) { // STOP Event
							break;
						}
						else if (dwRes == WAIT_FAILED) {
							PRINT_ERROR_AUTO(L"WaitForMultipleObjects");
						}
						else {
							CancelIoEx(pHandle->hDevice, &Overlapped);
						}
					}
					else {
						if ((dwRes != ERROR_DEVICE_NOT_CONNECTED) && (dwRes != ERROR_OPERATION_ABORTED)) { // the device is gone; retrying would spin and print forever
							PRINT_ERROR_NUMBER(L"ReadFile", dwRes);
						}

						break;
					}
				}

				if (ret && pHandle->FilterCallback) {
					if (pHandle->FilterCallback(pTmpBuffer, dwNumberOfBytesRead, pHandle->FilterArg)) {
						//kprintf(L"< ");
						//kprinthex(pTmpBuffer, dwNumberOfBytesRead);
						RtlCopyMemory(pHandle->Input.Buffer, pTmpBuffer, dwNumberOfBytesRead);
						SetEvent(pHandle->hSignalToRead);
					}
				}
			}

			CloseHandle(Overlapped.hEvent);
		}
		else PRINT_ERROR_AUTO(L"CreateEvent");

		LocalFree(pTmpBuffer);
	}
	else PRINT_ERROR_AUTO(L"LocalAlloc");

	return 0;
}

BOOL KUNI_CO_StartRecv(KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;

	pHandle->hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (pHandle->hStopEvent) {
		pHandle->hSignalToRead = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (pHandle->hSignalToRead) {
			pHandle->hThread = (HANDLE)_beginthreadex(NULL, 0, KUNI_CO_RecvThread, pHandle, 0, NULL);
			if (pHandle->hThread) {
				ret = TRUE;
			}
			else {
				PRINT_ERROR_AUTO(L"_beginthreadex");
				CloseHandle(pHandle->hStopEvent);
			}
		}
	}
	else PRINT_ERROR_AUTO(L"CreateEvent");

	return ret;
}

void KUNI_CO_StopRecv(KUNI_CO_HANDLE* pHandle) {
	if (pHandle->hThread) {
		SetEvent(pHandle->hStopEvent);
		WaitForSingleObject(pHandle->hThread, INFINITE);
		CloseHandle(pHandle->hThread);
		CloseHandle(pHandle->hSignalToRead);
		CloseHandle(pHandle->hStopEvent);
		pHandle->hThread = NULL;
	}
}

BOOL KUNI_CO_FindDevice(USHORT VID, USHORT PID, BYTE ReportId, BYTE ReportByteLength, KUNI_CO_HANDLE* pHandle) {
	BOOL ret = FALSE;

	GUID guidHid = GUID_NULL;
	HDEVINFO hDevInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
	BOOL enumStatus;
	DWORD enumIndex, dwRequired;
	PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData;

	HidD_GetHidGuid(&guidHid);
	hDevInfo = SetupDiGetClassDevs(&guidHid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE) {
		for (enumIndex = 0, enumStatus = TRUE; enumStatus && !ret; enumIndex++) {
			DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
			enumStatus = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &guidHid, enumIndex, &DeviceInterfaceData);
			if (enumStatus) {
				dwRequired = 0;
				if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, NULL, 0, &dwRequired, NULL) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER)) {
					DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, dwRequired);
					if (DeviceInterfaceDetailData) {
						DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
						if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, DeviceInterfaceDetailData, dwRequired, &dwRequired, NULL)) {
							if (KUNI_CO_OpenHandle_with_checks(DeviceInterfaceDetailData->DevicePath, VID, PID, ReportId, ReportByteLength, pHandle)) {
								ret = TRUE;
							}
						}
						else PRINT_ERROR_AUTO(L"SetupDiGetDeviceInterfaceDetail(data)");

						LocalFree(DeviceInterfaceDetailData);
					}
				}
				else PRINT_ERROR_AUTO(L"SetupDiGetDeviceInterfaceDetail(size)");
			}
			else if(GetLastError() != ERROR_NO_MORE_ITEMS) PRINT_ERROR_AUTO(L"SetupDiEnumDeviceInterfaces");
		}

		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
	else PRINT_ERROR_AUTO(L"SetupDiGetClassDevs");

	return ret;
}

DWORD WINAPI KUNI_CO_WatchCallback(HCMNOTIFICATION hNotify, PVOID Context, CM_NOTIFY_ACTION Action, PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize) {
	UNREFERENCED_PARAMETER(hNotify);
	UNREFERENCED_PARAMETER(EventData);
	UNREFERENCED_PARAMETER(EventDataSize);

	if ((Action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) || (Action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)) { // removal counts too: a reset makes the dongle leave before it comes back
		SetEvent((HANDLE)Context);
	}

	return ERROR_SUCCESS;
}

BOOL KUNI_CO_WatchStart(KUNI_CO_WATCH* pWatch) {
	BOOL ret = FALSE;
	CM_NOTIFY_FILTER Filter = { 0 };
	CONFIGRET cr;

	RtlZeroMemory(pWatch, sizeof(*pWatch));

	pWatch->hArrival = CreateEvent(NULL, FALSE, FALSE, NULL); // auto-reset
	if (!pWatch->hArrival) {
		PRINT_ERROR_AUTO(L"CreateEvent");
	}
	else {
		Filter.cbSize = sizeof(Filter);
		Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
		HidD_GetHidGuid(&Filter.u.DeviceInterface.ClassGuid);

		cr = CM_Register_Notification(&Filter, pWatch->hArrival, KUNI_CO_WatchCallback, (HCMNOTIFICATION*)&pWatch->hNotify);
		if (cr != CR_SUCCESS) {
			PRINT_ERROR(L"CM_Register_Notification: 0x%08x\n", cr);
			CloseHandle(pWatch->hArrival);
			pWatch->hArrival = NULL;
		}
		else {
			ret = TRUE;
		}
	}

	return ret;
}

BOOL KUNI_CO_WatchWait(KUNI_CO_WATCH* pWatch, DWORD Timeout) {
	return pWatch->hArrival && (WaitForSingleObject(pWatch->hArrival, Timeout) == WAIT_OBJECT_0);
}

void KUNI_CO_WatchStop(KUNI_CO_WATCH* pWatch) {
	if (pWatch->hNotify)
	{
		CM_Unregister_Notification((HCMNOTIFICATION)pWatch->hNotify);
	}

	if (pWatch->hArrival)
	{
		CloseHandle(pWatch->hArrival);
	}

	RtlZeroMemory(pWatch, sizeof(*pWatch));
}