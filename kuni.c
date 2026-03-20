/*	Benjamin DELPY `gentilkiwi`
	https://blog.gentilkiwi.com
	benjamin@gentilkiwi.com
	Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include <windows.h>
#include <stdio.h>
#include "logitech_unifying_firmware.h"
#include "logitech_unifying_bootloader.h"

BOOL KUNI_FW_HELPER_Print_Informations(HID_HANDLES* pHandles);
void KUNI_FW_HELPER_Print_SpecificDevice(HID_HANDLES* pHandles, BYTE Device);
void TestFirmware(KUNI_CO_HANDLE* pHandle, LPCWSTR filename);

int wmain(int argc, wchar_t *argv[]) {
    HID_HANDLES hHids;
    KUNI_CO_HANDLE hBL;
    
    BYTE Buffer[BOOTLOADER_MAX_DATA], cbBuffer;
    BOOTLOADER_MEMORY_INFO Info;
    WORD Checksum;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    if (KUNI_FW_FindDevice(&hHids)) {
        KUNI_FW_HELPER_Print_Informations(&hHids);
        KUNI_FW_EnterBootloader(&hHids);
        KUNI_FW_CloseDevice(&hHids);
        Sleep(4100);
    }

    if (KUNI_BL_FindDevice(&hBL)) {
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
        for (WORD Offset = 0; Offset < BOOTLOADER_NORDIC_PAGE_SIZE; Offset += 0x10)
        {
            if (KUNI_BL_Read(&hBL, BOOTLOADER_NORDIC_PAGE_INFO_ADDR + Offset, Buffer, 0x10))
            {
                kprintf(L"  [0x%04hx] ", Offset);
                kprinthex(Buffer, 0x10);
            }
        }
        
        //TestFirmware(&hBL, L"RQR12.10_B0032_patched.bin");

        kprintf(L"> Reboot (goto FW)\n");
        KUNI_BL_Reboot(&hBL);

        KUNI_BL_CloseDevice(&hBL);
    }

    return EXIT_SUCCESS;
}

BOOL KUNI_FW_HELPER_Print_Informations(HID_HANDLES* pHandles) {
    BYTE Value[3], LongValue[16];

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
    
    //kprintf(L"Kiwi !!  :\n");
    //for (WORD Addr = 0x0000; Addr < 0x8000; Addr += 0x10)
    //{
    //    kprintf(L"[0x%04hx] ", Addr);
    //
    //    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_KIWI, (BYTE[]) { HIBYTE(Addr), LOBYTE(Addr), KIWI_FLASH_ADDRESS_FLAG | 0x00 }, LongValue, 1000))
    //    {
    //        kprinthex(LongValue, sizeof(LongValue));
    //    }
    //    else
    //    {
    //        kprintf(L"\n");
    //    }
    //}
    
    kprintf(L"Devices\n");
    for (BYTE i = 0; i < 6; i++) {
        KUNI_FW_HELPER_Print_SpecificDevice(pHandles, i);
    }

    for (WORD Addr = 0x7000 + 1; Addr < 0x70c0 + 1; Addr += 0x20) {
        if(KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_KIWI, (BYTE[]) { HIBYTE(Addr), LOBYTE(Addr), KIWI_FLASH_ADDRESS_FLAG | 0x00 }, LongValue, 1000)) {
            kprintf(L"[  ] ");
            kprinthex(LongValue, sizeof(LongValue));
        }
    }

    WORD Addr = 0x0000;
    RtlZeroMemory(LongValue, sizeof(LongValue));
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_KIWI, (BYTE[]) { HIBYTE(Addr), LOBYTE(Addr), KIWI_FLASH_ADDRESS_FLAG | 0x00 }, LongValue, 1000)) {
        kprinthex(LongValue, sizeof(LongValue));
    }

    return TRUE;
}

void KUNI_FW_HELPER_Print_SpecificDevice(HID_HANDLES* pHandles, BYTE Device)
{
    BYTE LongValue[16];

    kprintf(L"  Device #%hhu\n", Device);

    kprintf(L"    ");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_PAIRING_INFORMATION, (BYTE[]) { 0x20 | Device, 0x00, 0x00 }, LongValue, 1000)) {
        kprintf(L"Destination Id : 0x%02hhx\n", LongValue[1]);
        kprintf(L"    Report Interval: 0x%02hhx (%hhu ms)\n", LongValue[2], LongValue[2]);
        kprintf(L"    WPID           : 0x%02hhx%02hhx\n", LongValue[3], LongValue[4]);
        kprintf(L"    unk            : ");
        kprinthex(LongValue + 5, 2);
        kprintf(L"    Device Type    : 0x%02hhx\n", LongValue[7]);
        kprintf(L"    Capabilities   : 0x%02hhx\n", LongValue[8]);
        kprintf(L"    unk            : ");
        kprinthex(LongValue + 9, sizeof(LongValue) - 9);
    }

    kprintf(L"    ");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_PAIRING_INFORMATION, (BYTE[]) { 0x30 | Device, 0x00, 0x00 }, LongValue, 1000)) {
        kprintf(L"Device Serial  : ");
        kprinthex(LongValue + 1, 4); // from Nordic CHIPID, minus last byte (??? to check, maybe in devices too !!!)
        kprintf(L"    Report types   : 0x%08x\n", _byteswap_ulong(*(DWORD*)(LongValue + 5)));
        kprintf(L"    Usability info : 0x%02hhx\n", LongValue[9]);
        kprintf(L"    unk            : ");
        kprinthex(LongValue + 10, sizeof(LongValue) - 10);
    }

    kprintf(L"    ");
    if (KUNI_FW_GetLongRegister(pHandles, DEVICE_INDEX_RECEIVER, REGISTER_PAIRING_INFORMATION, (BYTE[]) { 0x40 | Device, 0x00, 0x00 }, LongValue, 1000)) {
        kprintf(L"Device name    : ");
        if (LongValue[1] < 0xf)
        {
            kprintf(L"%.*S", LongValue[1], LongValue + 2);
        }
        else
        {
            kprintf(L"HEX: ");
            kprinthex(LongValue + 1, sizeof(LongValue) - 1);
        }
        kprintf(L"\n");
    }
}

void TestFirmware(KUNI_CO_HANDLE* pHandle, LPCWSTR filename) {
    BOOTLOADER_MEMORY_INFO Info;
    PBYTE pbData;
    DWORD cbData, offset, addr;
    BYTE chunk;

    if (KUNI_BL_GetMemoryInfo(pHandle, &Info)) {
        kprintf(L"Firmware memory     : 0x%04hx - 0x%04hx (page size 0x%04hx)\n", Info.FirmwareFirst, Info.FirmwareLatest, Info.FirmwarePageSize);
        if (kull_m_file_readData(filename, &pbData, &cbData)) {
            kprintf(L"Data is %u bytes\n", cbData);
            if ((Info.FirmwareFirst + cbData - 1) <= Info.FirmwareLatest) {
                
                kprintf(L"Page erase   : [");
                for (addr = Info.FirmwareFirst - (Info.FirmwareFirst % Info.FirmwarePageSize); addr <= (Info.FirmwareFirst + cbData - 1); addr += Info.FirmwarePageSize) {
                    kprintf(L"%c", KUNI_BL_ErasePage(pHandle, (WORD)addr) ? L'#' : L'x');
                }
                kprintf(L"]\n");

                kprintf(L"Chunk program: [");
                for (offset = 1, chunk = (BYTE)min(0x10 - 1, cbData - 1); offset < cbData; offset += chunk, chunk = (BYTE)min(0x10, cbData - offset)) {
                    kprintf(L"%c", KUNI_BL_Write(pHandle, (WORD)(Info.FirmwareFirst + offset), pbData + offset, chunk) ? L'.' : L'x');
                }
                kprintf(L"]\n");

                kprintf(L"Last: %c\n", KUNI_BL_Write(pHandle, Info.FirmwareFirst, pbData, 1) ? L'.' : L'x');
            }

            LocalFree(pbData);
        }
    }
}
