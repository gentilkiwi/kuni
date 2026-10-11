/*	Benjamin DELPY `gentilkiwi`
    https://blog.gentilkiwi.com
    benjamin@gentilkiwi.com
    Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include <windows.h>
#include <stdio.h>
#include "logitech_unifying_firmware.h"
#include "logitech_unifying_bootloader.h"
#include "logitech_unifying_memory.h"

BOOL KUNI_FW_HELPER_Print_Informations(HID_HANDLES* pHandles, BOOL bDevices);
BOOL DumpDevicesKeys();

int wmain(int argc, wchar_t* argv[]) {
    int ret = EXIT_SUCCESS;
    HID_HANDLES hHids;
    KUNI_CO_HANDLE hBL;

    BYTE Buffer[BOOTLOADER_MAX_DATA], cbBuffer;
    BOOTLOADER_MEMORY_INFO Info;
    WORD Checksum;
    KUNI_CO_WATCH Watch;
    ULONGLONG Start;
    BOOL bBootloader = FALSE;

    setvbuf(stdout, NULL, _IONBF, 0); // progress dots and live hex must reach the console as they happen, not at flush

    if (GET_CLI_ARG_PRESENT(L"keys")) {
        ret = DumpDevicesKeys() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else {
        if (KUNI_FW_FindDevice(&hHids)) {
            KUNI_FW_HELPER_Print_Informations(&hHids, TRUE);

            if (KUNI_CO_WatchStart(&Watch)) {

                KUNI_FW_EnterBootloader(&hHids);
                KUNI_FW_CloseDevice(&hHids);

                for (Start = GetTickCount64(); !(bBootloader = KUNI_BL_FindDevice(&hBL)) && ((GetTickCount64() - Start) < 30000); ) {
                    KUNI_CO_WatchWait(&Watch, 30000);
                }

                KUNI_CO_WatchStop(&Watch);
            }
        }
        else {
            bBootloader = KUNI_BL_FindDevice(&hBL);
        }

        if (bBootloader) {
            if (KUNI_BL_GetChipVersionString(&hBL, Buffer, &cbBuffer)) {
                kprintf(L"Integrated chip     : %.*S\n", cbBuffer, Buffer);
            }
            if (KUNI_BL_GetBootloaderVersionString(&hBL, Buffer, &cbBuffer)) {
                kprintf(L"Bootloader version  : %.*S\n", cbBuffer, Buffer);
            }
            if (KUNI_BL_GetFirmware_FW_VersionString(&hBL, Buffer, &cbBuffer)) {
                kprintf(L"Firmware version(FW): %.*S\n", cbBuffer, Buffer);
            }
            if (KUNI_BL_GetFirmware_BL_VersionString(&hBL, Buffer, &cbBuffer)) {
                kprintf(L"Firmware version(BL): %.*S\n", cbBuffer, Buffer);
            }
            if (KUNI_BL_GetMemoryInfo(&hBL, &Info)) {
                kprintf(L"Firmware memory     : 0x%04hx - 0x%04hx (page size 0x%04hx)\n", Info.FirmwareFirst, Info.FirmwareLatest, Info.FirmwarePageSize);
            }
            if (KUNI_BL_CheckSum(&hBL, &Checksum)) {
                kprintf(L"  Checksum          : 0x%04hx\n", Checksum);
            }

            if (KUNI_BL_Read(&hBL, 0x0000, Buffer, 0x10)) {
                kprintf(L"[%04hx] ", 0x0000);
                kprinthex(Buffer, 0x10);
            }

            kprintf(L"Nordic\'s PageInfo:\n");
            for (WORD Offset = 0; Offset < BOOTLOADER_NORDIC_PAGE_SIZE; Offset += 0x10) {
                if (KUNI_BL_Read(&hBL, BOOTLOADER_NORDIC_PAGE_INFO_ADDR + Offset, Buffer, 0x10)) {
                    kprintf(L"  [0x%04hx] ", Offset);
                    kprinthex(Buffer, 0x10);
                }
            }

            kprintf(L"> Reboot (goto FW)\n");
            KUNI_BL_Reboot(&hBL);

            KUNI_BL_CloseDevice(&hBL);
        }
    }

    return ret;
}

BOOL KUNI_FW_HELPER_Print_Informations(HID_HANDLES* pHandles, BOOL bDevices) {
    BYTE Value[3], LongValue[16];
    KUNI_FW_DEVICE Device;

    kprintf(L"Firmware Info\n");

    kprintf(L"  Firmware Version  : ");
    if (KUNI_FW_GetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_INFO, (BYTE[]) { 0x01, 0x00, 0x00 }, Value, 1000)) {
        kprintf(L"%02hhx.%02hhx\n", Value[1], Value[2]);
    }

    kprintf(L"  Firmware Build    : ");
    if (KUNI_FW_GetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_INFO, (BYTE[]) { 0x02, 0x00, 0x00 }, Value, 1000)) {
        kprintf(L"%04hx\n", MAKEWORD(Value[2], Value[1]));
    }

    kprintf(L"  Bootloader PID(?) : ");
    if (KUNI_FW_GetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_INFO, (BYTE[]) { 0x03, 0x00, 0x00 }, Value, 1000)) {
        kprintf(L"0x%02hhx%02hhx\n", Value[1], Value[2]);
    }

    kprintf(L"  Bootloader Version: ");
    if (KUNI_FW_GetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_FIRMWARE_INFO, (BYTE[]) { 0x04, 0x00, 0x00 }, Value, 1000)) {
        kprintf(L"%02hhx.%02hhx\n", Value[1], Value[2]);
    }

    kprintf(L"Pairing Information (0x02)\n");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_PAIRING_INFORMATION, (BYTE[]) { 0x02, 0x00, 0x00 }, LongValue, 1000)) {
        kprintf(L"  Firmware Version  : %02hhx.%02hhx\n", LongValue[1], LongValue[2]);
        kprintf(L"  Firmware Build    : %02hhx%02hhx\n", LongValue[3], LongValue[4]);
        kprintf(L"  WPID              : 0x%02hhx%02hhx\n", LongValue[5], LongValue[6]);
        kprintf(L"  unk               : ");
        kprinthex(LongValue + 7, sizeof(LongValue) - 7);
    }

    kprintf(L"Pairing Information (0x03)\n");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_PAIRING_INFORMATION, (BYTE[]) { 0x03, 0x00, 0x00 }, LongValue, 1000)) {
        kprintf(L"  Device Serial     : ");
        kprinthex(LongValue + 1, 4); // from Nordic CHIPID, minus last byte
        kprintf(L"  unk               : ");
        kprinthex(LongValue + 5, sizeof(LongValue) - 5);
    }

    kprintf(L"Connected devices: ");
    if (KUNI_FW_GetRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_CONNECTION_STATE, (BYTE[]) { 0x00, 0x00, 0x00 }, Value, 1000)) {
        kprintf(L"%hhu\n", Value[1]);
    }

    kprintf(L"Device Activity  : ");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_DEVICE_ACTIVITY, (BYTE[]) { 0x00, 0x00, 0x00 }, LongValue, 1000)) {
        kprinthex(LongValue, 16);
    }

    if (bDevices) {
        kprintf(L"Devices\n");
        for (BYTE i = 0; i < 6; i++) {
            KUNI_FW_ReadDevice(pHandles, i, &Device);
            KUNI_FW_PrintDevice(i, &Device);
        }
    }

    return TRUE;
}

BOOL DumpDevicesKeys() {
    BOOL ret = FALSE;
    HID_HANDLES hHids;

    /*  /keys only runs against a receiver in normal firmware mode: it no longer resets, so it never
     *  touches the bootloader and has nothing to wait for. Not found = nothing to do (run `kuni` with
     *  no argument to reboot one that is stuck in the bootloader).
     */
    if (KUNI_FW_FindDevice(&hHids)) {
        KUNI_FW_HELPER_Print_Informations(&hHids, FALSE); // the devices are listed with their keys
        KUNI_MEM_DumpKeys(&hHids);
        KUNI_FW_CloseDevice(&hHids);
        ret = TRUE;
    }
    else PRINT_ERROR(L"no Unifying receiver found\n");

    return ret;
}
