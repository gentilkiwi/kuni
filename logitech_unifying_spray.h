#pragma once
/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_dj.h"

/*	One async sprayer of DJ 0x0e write frames: a dedicated thread keeps a ring of
 *	OVERLAPPED transfers in flight and, once armed, hammers a single SRAM address
 *	with one chosen byte until disarmed. The memory race arms two of them (on the
 *	low and the high offset byte) and steers the pace through DelayUs; the sprayer
 *	borrows the DJ handle and knows nothing of the race above it.
 */
typedef struct _KUNI_SPRAY {
	HANDLE hDevice;		// borrowed from the DJ handle, not owned
	USHORT ByteLength;
	WORD Address;
	volatile LONG Value;	// < 0 disarms it
	volatile LONG DelayUs;	// the caller's pace knob, read afresh every frame
	volatile LONG Frames;
	volatile LONG Errors;
	HANDLE hArmed;		// held for the duration of a round
	HANDLE hIdle;		// set once the ring of transfers has drained
	HANDLE hStop;
	HANDLE hTimer;		// high resolution, paces the frames
	HANDLE hThread;
} KUNI_SPRAY, * PKUNI_SPRAY;

/*	hDevice and ByteLength are borrowed from the DJ handle; Address is fixed for
 *	the sprayer's life; DelayUs is only the initial pace, steered afterwards
 *	through pSpray->DelayUs. The thread starts idle - KUNI_SPRAY_Arm sets it off.
 */
BOOL KUNI_SPRAY_Start(KUNI_SPRAY* pSpray, HANDLE hDevice, USHORT ByteLength, WORD Address, LONG DelayUs);
void KUNI_SPRAY_Stop(KUNI_SPRAY* pSpray);

/*	Value >= 0 arms the sprayer on that byte; Value < 0 disarms it AND blocks
 *	until the ring has drained, so the next round never inherits stray frames.
 */
void KUNI_SPRAY_Arm(KUNI_SPRAY* pSpray, LONG Value);
