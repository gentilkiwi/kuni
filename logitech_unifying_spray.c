/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_spray.h"

#define SPRAY_INFLIGHT		4 // transfers a sprayer keeps outstanding

/*	Several transfers in flight, on purpose: waiting for each frame to complete
 *	caps a sprayer at one frame per transfer latency (~580 us) whatever the
 *	pacing says, and the two bytes then never land in the same window.
 */
static unsigned __stdcall KUNI_SPRAY_Thread(void* pvArg) {
	KUNI_SPRAY* pSpray = (KUNI_SPRAY*)pvArg;
	BYTE Frame[SPRAY_INFLIGHT][REPORT_DJ_LENGTH_LONG];
	OVERLAPPED Overlapped[SPRAY_INFLIGHT];
	BOOL Pending[SPRAY_INFLIGHT];
	HANDLE Idle[2], Tick[2];
	LARGE_INTEGER DueTime;
	DWORD dwNumberOfBytesWritten;
	LONG Value;
	BYTE i, Slot = 0;

	RtlZeroMemory(Overlapped, sizeof(Overlapped));
	RtlZeroMemory(Pending, sizeof(Pending));

	for (i = 0; i < SPRAY_INFLIGHT; i++) {
		RtlZeroMemory(Frame[i], sizeof(Frame[i])); // full output length; the tail past the short frame stays zero
		KUNI_DJ_BuildWriteFrame(Frame[i], pSpray->Address, 0); // Value overwritten per frame below

		Overlapped[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (!Overlapped[i].hEvent) {
			PRINT_ERROR_AUTO(L"CreateEvent");
			goto KUNI_SPRAY_ThreadDone; // les handles deja crees seront fermes la-bas
		}
	}

	Idle[0] = pSpray->hStop;
	Idle[1] = pSpray->hArmed;
	Tick[0] = pSpray->hStop;
	Tick[1] = pSpray->hTimer;

	while (WaitForMultipleObjects(ARRAYSIZE(Idle), Idle, FALSE, INFINITE) != WAIT_OBJECT_0) {
		while (WaitForSingleObject(pSpray->hArmed, 0) == WAIT_OBJECT_0) {
			Value = pSpray->Value;
			if (Value < 0) {
				break;
			}

			if (Pending[Slot]) {
				GetOverlappedResult(pSpray->hDevice, &Overlapped[Slot], &dwNumberOfBytesWritten, TRUE);
				Pending[Slot] = FALSE;
			}

			Frame[Slot][3] = (BYTE)Value;

			if (KUNI_CO_SendRaw(pSpray->hDevice, Frame[Slot], pSpray->ByteLength, &Overlapped[Slot], 0)) {
				Pending[Slot] = TRUE;
			}
			else {
				pSpray->Errors++;
			}
			pSpray->Frames++;
			Slot = (Slot + 1) % SPRAY_INFLIGHT;

			DueTime.QuadPart = -((LONGLONG)pSpray->DelayUs * 10); // relative, 100 ns units
			SetWaitableTimer(pSpray->hTimer, &DueTime, 0, NULL, NULL, FALSE);

			if (WaitForMultipleObjects(ARRAYSIZE(Tick), Tick, FALSE, INFINITE) == WAIT_OBJECT_0) {
				goto KUNI_SPRAY_ThreadDone;
			}
		}

		/*	Drain before saying so: frames still in flight carry the value of the
		 *	round that just ended and would vote for the wrong address in the next.
		 */
		for (i = 0; i < SPRAY_INFLIGHT; i++) {
			if (Pending[i]) {
				GetOverlappedResult(pSpray->hDevice, &Overlapped[i], &dwNumberOfBytesWritten, TRUE);
				Pending[i] = FALSE;
			}
		}

		SetEvent(pSpray->hIdle);
	}

KUNI_SPRAY_ThreadDone:

	for (i = 0; i < SPRAY_INFLIGHT; i++) { // Frame[] and Overlapped[] are on this stack
		if (Pending[i]) {
			CancelIoEx(pSpray->hDevice, &Overlapped[i]);
			GetOverlappedResult(pSpray->hDevice, &Overlapped[i], &dwNumberOfBytesWritten, TRUE);
		}
		CloseHandle(Overlapped[i].hEvent);
	}

	SetEvent(pSpray->hIdle);

	return 0;
}

void KUNI_SPRAY_Arm(KUNI_SPRAY* pSpray, LONG Value) {
	if (Value < 0) {
		ResetEvent(pSpray->hArmed);
		pSpray->Value = -1;
		/*	INFINITE on purpose: the thread always ends up signalling, and giving
		 *	up early would start the next round with this one still spraying -
		 *	every vote then goes to the previous address, silently.
		 */
		WaitForSingleObject(pSpray->hIdle, INFINITE);
	}
	else {
		ResetEvent(pSpray->hIdle);
		pSpray->Value = Value;
		SetEvent(pSpray->hArmed);
	}
}

BOOL KUNI_SPRAY_Start(KUNI_SPRAY* pSpray, HANDLE hDevice, USHORT ByteLength, WORD Address, LONG DelayUs) {
	BOOL ret = FALSE;

	pSpray->hDevice = hDevice;
	pSpray->ByteLength = ByteLength;
	pSpray->Address = Address;
	pSpray->Value = -1;
	pSpray->DelayUs = DelayUs;
	pSpray->Frames = 0;
	pSpray->Errors = 0;

	pSpray->hArmed = CreateEvent(NULL, TRUE, FALSE, NULL);
	pSpray->hIdle = CreateEvent(NULL, TRUE, TRUE, NULL);
	pSpray->hStop = CreateEvent(NULL, TRUE, FALSE, NULL);

	/*	A plain waitable timer follows the 15.6 ms global resolution, useless
	 *	here; the high resolution flag gives sub-millisecond waits without
	 *	touching timeBeginPeriod.
	 */
	pSpray->hTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!pSpray->hTimer) {
		pSpray->hTimer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
		if (pSpray->hTimer) {
			PRINT_ERROR(L"no high resolution timer, the race will be much harder to win\n");
		}
	}

	if (pSpray->hArmed && pSpray->hIdle && pSpray->hStop && pSpray->hTimer) {
		pSpray->hThread = (HANDLE)_beginthreadex(NULL, 0, KUNI_SPRAY_Thread, pSpray, 0, NULL);
		if (pSpray->hThread) {
			ret = TRUE;
		}
		else PRINT_ERROR_AUTO(L"_beginthreadex");
	}
	else PRINT_ERROR_AUTO(L"CreateEvent/CreateWaitableTimerExW");

	return ret;
}

void KUNI_SPRAY_Stop(KUNI_SPRAY* pSpray) {
	if (pSpray->hStop) {
		KUNI_SPRAY_Arm(pSpray, -1);
		SetEvent(pSpray->hStop);
	}

	if (pSpray->hThread) {
		WaitForSingleObject(pSpray->hThread, INFINITE);
		CloseHandle(pSpray->hThread);
		pSpray->hThread = NULL;
	}

	if (pSpray->hTimer) {
		CloseHandle(pSpray->hTimer);
		pSpray->hTimer = NULL;
	}
	if (pSpray->hStop) {
		CloseHandle(pSpray->hStop);
		pSpray->hStop = NULL;
	}
	if (pSpray->hIdle) {
		CloseHandle(pSpray->hIdle);
		pSpray->hIdle = NULL;
	}
	if (pSpray->hArmed) {
		CloseHandle(pSpray->hArmed);
		pSpray->hArmed = NULL;
	}
}
