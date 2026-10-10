#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#define TARGET_OLD_VER 0x63090C38   // 旧版本 3.9.12.56
#define TARGET_NEW_VER 0x63090C51   // 伪装目标版本

static BOOL GetWeChatWinInfo(DWORD pid, DWORD_PTR* outBase, DWORD* outSize) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return FALSE;

    MODULEENTRY32 me;
    me.dwSize = sizeof(me);
    BOOL found = FALSE;
    if (Module32First(hSnap, &me)) {
        do {
            if (_stricmp(me.szModule, "WeChatWin.dll") == 0) {
                *outBase = (DWORD_PTR)me.modBaseAddr;
                *outSize = me.modBaseSize;
                found = TRUE;
                break;
            }
        } while (Module32Next(hSnap, &me));
    }
    CloseHandle(hSnap);
    return found;
}

static int SafePatchMemory(HANDLE hProcess, DWORD_PTR base, DWORD size, DWORD oldVer, DWORD newVer) {
    int patchCount = 0;
    DWORD_PTR currentAddr = base;
    DWORD_PTR endAddr = base + size;
    MEMORY_BASIC_INFORMATION mbi;

    while (currentAddr < endAddr && VirtualQueryEx(hProcess, (LPCVOID)currentAddr, &mbi, sizeof(mbi))) {
        DWORD protect = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE);
        BOOL isReadable = (protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_EXECUTE_READWRITE);

        if ((mbi.State == MEM_COMMIT) && isReadable) {
            BYTE* buffer = (BYTE*)malloc(mbi.RegionSize);
            if (buffer) {
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer, mbi.RegionSize, &bytesRead) && bytesRead >= 4) {
                    for (SIZE_T i = 0; i <= bytesRead - 4; i += 4) {
                        if (*(DWORD*)&buffer[i] == oldVer) {
                            LPVOID target = (LPVOID)((DWORD_PTR)mbi.BaseAddress + i);
                            DWORD oldProtect;
                            if (VirtualProtectEx(hProcess, target, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                                if (WriteProcessMemory(hProcess, target, &newVer, sizeof(DWORD), NULL)) {
                                    patchCount++;
                                    printf("[+] Successfully patched version at: 0x%p\n", target);
                                }
                                VirtualProtectEx(hProcess, target, sizeof(DWORD), oldProtect, &oldProtect);
                            }
                        }
                    }
                }
                free(buffer);
            }
        }
        currentAddr = (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize;
    }
    return patchCount;
}

int main(void) {
    printf("==========================================\n");
    printf("  WeChat Version Faker (Native C Edition) \n");
    printf("==========================================\n\n");

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* p = strrchr(exePath, '\\');
    if (p) *p = '\0';
    SetCurrentDirectoryA(exePath);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    printf("[*] Launching WeChat.exe...\n");
    if (!CreateProcessA("WeChat.exe", NULL, NULL, NULL, FALSE, 0, NULL, exePath, &si, &pi)) {
        printf("[-] Failed to start WeChat.exe! Error: %lu\n", GetLastError());
        printf("[-] Make sure this tool is placed in the WeChat directory.\n");
        system("pause");
        return 1;
    }

    printf("[*] Waiting for WeChatWin.dll to load...\n");
    DWORD_PTR dllBase = 0;
    DWORD dllSize = 0;
    for (int i = 0; i < 60; i++) {
        if (GetWeChatWinInfo(pi.dwProcessId, &dllBase, &dllSize)) break;
        Sleep(50);
    }

    if (!dllBase) {
        printf("[-] Timed out waiting for WeChatWin.dll.\n");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    printf("[+] Found WeChatWin.dll at 0x%p (Size: 0x%lX)\n", (void*)dllBase, dllSize);
    printf("[*] Patching memory signatures...\n");
    int count = SafePatchMemory(pi.hProcess, dllBase, dllSize, TARGET_OLD_VER, TARGET_NEW_VER);

    if (count > 0) {
        printf("\n[+] SUCCESS! Patched %d version records in memory.\n", count);
        printf("[+] You can now scan QR code to login without version prompt!\n\n");
    } else {
        printf("\n[-] Warning: No matching version signature found.\n");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    Sleep(2000);
    return 0;
}
