/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_hidpp_common.h"

BOOL KUNI_CO_GetReportIdAndLength(PHIDP_PREPARSED_DATA PreparsedData, HIDP_REPORT_TYPE ReportType, UCHAR* pReportId, USHORT* pReportLength)
{
	BOOL ret = FALSE;
	HIDP_CAPS Caps;
	HIDP_BUTTON_CAPS ButtonCaps;
	USHORT nb;
	NTSTATUS status;


	status = HidP_GetCaps(PreparsedData, &Caps);
	if (status == HIDP_STATUS_SUCCESS)
	{
		switch (ReportType)
		{
		case HidP_Input:
			*pReportLength = Caps.InputReportByteLength;
			nb = Caps.NumberInputButtonCaps;

			break;
		case HidP_Output:
			*pReportLength = Caps.OutputReportByteLength;
			nb = Caps.NumberOutputButtonCaps;

			break;
		case HidP_Feature:
			*pReportLength = Caps.FeatureReportByteLength;
			nb = Caps.NumberFeatureButtonCaps;

			break;
		default:
			PRINT_ERROR(L"ReportType: %u\n", ReportType);
			goto KUNI_CO_FAIL;
		}

		if (nb == 1)
		{
			status = HidP_GetButtonCaps(ReportType, &ButtonCaps, &nb, PreparsedData);
			if (status == HIDP_STATUS_SUCCESS)
			{
				*pReportId = ButtonCaps.ReportID;
				ret = TRUE;
			}
			else PRINT_ERROR_NUMBER(L"HidP_GetButtonCaps", status);
		}
		// else invalid
	}
	else PRINT_ERROR_NUMBER(L"HidP_GetCaps", status);

KUNI_CO_FAIL:

	return ret;
}

BOOL KUNI_CO_OpenHandle(LPCWSTR DevicePath, KUNI_CO_HANDLE* pHandle)
{
	BOOL ret = FALSE;
	PHIDP_PREPARSED_DATA PreparsedData;

	RtlZeroMemory(pHandle, sizeof(*pHandle));

	kprintf(L"Device: %s\n", DevicePath);

	pHandle->hDevice = CreateFile(DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (pHandle->hDevice && (pHandle->hDevice != INVALID_HANDLE_VALUE))
	{
		if (HidD_GetAttributes(pHandle->hDevice, &pHandle->Attributes))
		{
			kprintf(L"  [A] VID: 0x%04hx - PID: 0x%04hx - Ver: 0x%04hx\n", pHandle->Attributes.VendorID, pHandle->Attributes.ProductID, pHandle->Attributes.VersionNumber);

			if (HidD_GetPreparsedData(pHandle->hDevice, &PreparsedData))
			{
				if (KUNI_CO_GetReportIdAndLength(PreparsedData, HidP_Input, &pHandle->Input.Id, &pHandle->Input.ByteLength) && KUNI_CO_GetReportIdAndLength(PreparsedData, HidP_Output, &pHandle->Output.Id, &pHandle->Output.ByteLength))
				{
					kprintf(L"  [C] ReportByteLength {Input: %2hu ; Output: %2hu}\n  [I] ReportId {Input: 0x%02hhx ; Output: 0x%02hhx}\n\n", pHandle->Input.ByteLength, pHandle->Output.ByteLength, pHandle->Input.Id, pHandle->Output.Id);

					pHandle->Input.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Input.ByteLength);
					if (pHandle->Input.Buffer)
					{
						pHandle->Output.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Output.ByteLength);
						if (pHandle->Output.Buffer)
						{
							ret = TRUE;
						}
						else PRINT_ERROR_AUTO(L"LocalAlloc(o)");
					}
					else PRINT_ERROR_AUTO(L"LocalAlloc(i)");
				}

				HidD_FreePreparsedData(PreparsedData);
			}
			else PRINT_ERROR_AUTO(L"HidD_GetPreparsedData");
		}
	}
	else PRINT_ERROR_AUTO(L"CreateFile");

	if (!ret)
	{
		KUNI_CO_CloseHandle(pHandle);
	}

	return ret;
}

BOOL KUNI_CO_OpenHandle_with_checks(LPCWSTR DevicePath, USHORT VID, USHORT PID, BYTE ReportId, BYTE ReportByteLength, KUNI_CO_HANDLE* pHandle)
{
	BOOL ret = FALSE;
	PHIDP_PREPARSED_DATA PreparsedData;

	RtlZeroMemory(pHandle, sizeof(*pHandle));

	pHandle->hDevice = CreateFile(DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (pHandle->hDevice && (pHandle->hDevice != INVALID_HANDLE_VALUE))
	{
		if (HidD_GetAttributes(pHandle->hDevice, &pHandle->Attributes))
		{
			if ((pHandle->Attributes.VendorID == VID) && (pHandle->Attributes.ProductID == PID))
			{
				if (HidD_GetPreparsedData(pHandle->hDevice, &PreparsedData))
				{
					if (KUNI_CO_GetReportIdAndLength(PreparsedData, HidP_Input, &pHandle->Input.Id, &pHandle->Input.ByteLength) && KUNI_CO_GetReportIdAndLength(PreparsedData, HidP_Output, &pHandle->Output.Id, &pHandle->Output.ByteLength))
					{
						if ((pHandle->Input.Id == ReportId) && (pHandle->Output.Id == ReportId) && (pHandle->Input.ByteLength == ReportByteLength) && (pHandle->Output.ByteLength == ReportByteLength))
						{
							kprintf(L"Device: %s\n", DevicePath);
							kprintf(L"  [A] VID: 0x%04hx - PID: 0x%04hx - Ver: 0x%04hx\n", pHandle->Attributes.VendorID, pHandle->Attributes.ProductID, pHandle->Attributes.VersionNumber);
							kprintf(L"  [C] ReportByteLength {Input: %2hu ; Output: %2hu}\n  [I] ReportId {Input: 0x%02hhx ; Output: 0x%02hhx}\n\n", pHandle->Input.ByteLength, pHandle->Output.ByteLength, pHandle->Input.Id, pHandle->Output.Id);

							pHandle->Input.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Input.ByteLength);
							if (pHandle->Input.Buffer)
							{
								pHandle->Output.Buffer = (BYTE*)LocalAlloc(LPTR, pHandle->Output.ByteLength);
								if (pHandle->Output.Buffer)
								{
									ret = TRUE;
								}
								else PRINT_ERROR_AUTO(L"LocalAlloc(o)");
							}
							else PRINT_ERROR_AUTO(L"LocalAlloc(i)");
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

	if (!ret)
	{
		KUNI_CO_CloseHandle(pHandle);
	}

	return ret;
}

void KUNI_CO_CloseHandle(KUNI_CO_HANDLE* pHandle)
{
	if (pHandle)
	{
		if (pHandle->Input.Buffer)
		{
			LocalFree(pHandle->Input.Buffer);
		}

		if (pHandle->Output.Buffer)
		{
			LocalFree(pHandle->Output.Buffer);
		}

		if (pHandle->hDevice && (pHandle->hDevice != INVALID_HANDLE_VALUE))
		{
			CloseHandle(pHandle->hDevice);
		}

		RtlZeroMemory(pHandle, sizeof(*pHandle));
	}
}

BOOL KUNI_CO_SendOutput(KUNI_CO_HANDLE* pHandle, DWORD Timeout)
{
	BOOL ret = FALSE;
	DWORD dwNumberOfBytesWritten, dwRes;
	OVERLAPPED Overlapped = { 0 };

	Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (Overlapped.hEvent)
	{
		pHandle->Output.Buffer[0] = pHandle->Output.Id;

		//kprintf(L"> ");
		//kprinthex(pHandle->Output.Buffer, pHandle->Output.ByteLength);
		ret = WriteFile(pHandle->hDevice, pHandle->Output.Buffer, pHandle->Output.ByteLength, &dwNumberOfBytesWritten, &Overlapped);
		if (!ret)
		{
			dwRes = GetLastError();
			if (dwRes == ERROR_IO_PENDING)
			{
				if (GetOverlappedResultEx(pHandle->hDevice, &Overlapped, &dwNumberOfBytesWritten, Timeout, FALSE))
				{
					ret = TRUE;
				}
				else
				{
					PRINT_ERROR_AUTO(L"GetOverlappedResultEx");
					CancelIoEx(pHandle->hDevice, &Overlapped);
				}
			}
			else PRINT_ERROR_NUMBER(L"WriteFile", dwRes);
		}

		CloseHandle(Overlapped.hEvent);
	}
	else PRINT_ERROR_AUTO(L"CreateEvent");

	return ret;
}

void KUNI_CO_ResetReceiveSignal(KUNI_CO_HANDLE* pHandle, KUNI_CO_FILTER_CALLBACK FilterCallback, PVOID FilterArg)
{
	if (FilterCallback)
	{
		ResetEvent(pHandle->hSignalToRead);
	}
	pHandle->FilterArg = FilterArg;
	pHandle->FilterCallback = FilterCallback;
}

unsigned __stdcall KUNI_CO_RecvThread(void* pParam)
{
	BOOL ret;
	KUNI_CO_HANDLE* pHandle = (KUNI_CO_HANDLE*)pParam;
	DWORD dwNumberOfBytesRead, dwRes;
	OVERLAPPED Overlapped = { 0 };
	HANDLE hEvents[2];
	BYTE* pTmpBuffer;

	pTmpBuffer = (BYTE*)LocalAlloc(LPTR, pHandle->Input.ByteLength);
	if (pTmpBuffer)
	{

		Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (Overlapped.hEvent)
		{
			hEvents[0] = Overlapped.hEvent;
			hEvents[1] = pHandle->hStopEvent;

			while (WaitForSingleObject(pHandle->hStopEvent, 0) == WAIT_TIMEOUT)
			{
				ResetEvent(Overlapped.hEvent);
				ret = ReadFile(pHandle->hDevice, pTmpBuffer, pHandle->Input.ByteLength, &dwNumberOfBytesRead, &Overlapped);
				if (!ret)
				{
					dwRes = GetLastError();
					if (dwRes == ERROR_IO_PENDING)
					{
						dwRes = WaitForMultipleObjects(ARRAYSIZE(hEvents), hEvents, FALSE, INFINITE);
						if (dwRes == WAIT_OBJECT_0)
						{
							if (GetOverlappedResult(pHandle->hDevice, &Overlapped, &dwNumberOfBytesRead, FALSE))
							{
								ret = TRUE;
							}
							else PRINT_ERROR_AUTO(L"GetOverlappedResult");
						}
						else if (dwRes == WAIT_OBJECT_0 + 1) // STOP Event
						{
							break;
						}
						else if (dwRes == WAIT_FAILED)
						{
							PRINT_ERROR_AUTO(L"WaitForMultipleObjects");
						}
						else
						{
							CancelIoEx(pHandle->hDevice, &Overlapped);
						}
					}
					else PRINT_ERROR_NUMBER(L"ReadFile", dwRes);
				}

				if (ret && pHandle->FilterCallback)
				{
					if (pHandle->FilterCallback(pTmpBuffer, dwNumberOfBytesRead, pHandle->FilterArg))
					{
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

BOOL KUNI_CO_StartRecv(KUNI_CO_HANDLE* pHandle)
{
	BOOL ret = FALSE;

	pHandle->hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (pHandle->hStopEvent)
	{
		pHandle->hSignalToRead = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (pHandle->hSignalToRead)
		{
			pHandle->hThread = (HANDLE)_beginthreadex(NULL, 0, KUNI_CO_RecvThread, pHandle, 0, NULL);
			if (pHandle->hThread)
			{
				ret = TRUE;
			}
			else
			{
				PRINT_ERROR_AUTO(L"_beginthreadex");
				CloseHandle(pHandle->hStopEvent);
			}
		}
	}
	else PRINT_ERROR_AUTO(L"CreateEvent");

	return ret;
}

void KUNI_CO_StopRecv(KUNI_CO_HANDLE* pHandle)
{
	if (pHandle->hThread)
	{
		SetEvent(pHandle->hStopEvent);
		WaitForSingleObject(pHandle->hThread, INFINITE);
		CloseHandle(pHandle->hThread);
		CloseHandle(pHandle->hSignalToRead);
		CloseHandle(pHandle->hStopEvent);
		pHandle->hThread = NULL;
	}
}

BOOL KUNI_CO_FindDevice(USHORT VID, USHORT PID, BYTE ReportId, BYTE ReportByteLength, KUNI_CO_HANDLE* pHandle)
{
	BOOL ret = FALSE;

	GUID guidHid = GUID_NULL;
	HDEVINFO hDevInfo;
	SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
	BOOL enumStatus;
	DWORD enumIndex, dwRequired;
	PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData;

	HidD_GetHidGuid(&guidHid);
	hDevInfo = SetupDiGetClassDevs(&guidHid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		for (enumIndex = 0, enumStatus = TRUE; enumStatus && !ret; enumIndex++)
		{
			DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
			enumStatus = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &guidHid, enumIndex, &DeviceInterfaceData);
			if (enumStatus)
			{
				dwRequired = 0;
				if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, NULL, 0, &dwRequired, NULL) && (GetLastError() == ERROR_INSUFFICIENT_BUFFER))
				{
					DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, dwRequired);
					if (DeviceInterfaceDetailData)
					{
						DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
						if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &DeviceInterfaceData, DeviceInterfaceDetailData, dwRequired, &dwRequired, NULL))
						{
							if (KUNI_CO_OpenHandle_with_checks(DeviceInterfaceDetailData->DevicePath, VID, PID, ReportId, ReportByteLength, pHandle))
							{
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