#include <windows.h>
#include <tlhelp32.h>

#define TARGET_OLD_VER 0x63090C38   // 3.9.12.56
#define TARGET_NEW_VER 0x6400061A   // 4.0.6.26

// 获取 WeChatWin.dll 基址
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

// 极速静默内存修补
static void FastSilentPatch(HANDLE hProcess, DWORD_PTR base, DWORD size, DWORD oldVer, DWORD newVer) {
    DWORD_PTR currentAddr = base;
    DWORD_PTR endAddr = base + size;
    MEMORY_BASIC_INFORMATION mbi;

    while (currentAddr < endAddr && VirtualQueryEx(hProcess, (LPCVOID)currentAddr, &mbi, sizeof(mbi))) {
        if ((mbi.State == MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            BYTE* buffer = (BYTE*)malloc(mbi.RegionSize);
            if (buffer) {
                SIZE_T bytesRead = 0;
                if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer, mbi.RegionSize, &bytesRead) && bytesRead >= 4) {
                    for (SIZE_T i = 0; i <= bytesRead - 4; i += 4) {
                        if (*(DWORD*)&buffer[i] == oldVer) {
                            LPVOID target = (LPVOID)((DWORD_PTR)mbi.BaseAddress + i);
                            DWORD oldProtect;
                            if (VirtualProtectEx(hProcess, target, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                                WriteProcessMemory(hProcess, target, &newVer, sizeof(DWORD), NULL);
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
}

// 使用 WinMain 彻底抹除控制台黑框
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. 内置注入兼容层环境变量（彻底消除 64 位系统拦截）
    SetEnvironmentVariableA("__COMPAT_LAYER", "~ ARM64WOWONAMD64");

    // 2. 修正当前工作目录
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* p = strrchr(exePath, '\\');
    if (p) *p = '\0';
    SetCurrentDirectoryA(exePath);

    // 3. 启动微信（继承环境变量，以普通用户权限启动）
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA("WeChat.exe", NULL, NULL, NULL, FALSE, 0, NULL, exePath, &si, &pi)) {
        return 1;
    }

    // 4. 极速轮询检测（每 10 毫秒一次，最多等 2 秒）
    DWORD_PTR dllBase = 0;
    DWORD dllSize = 0;
    for (int i = 0; i < 200; i++) {
        if (GetWeChatWinInfo(pi.dwProcessId, &dllBase, &dllSize)) break;
        Sleep(10);
    }

    // 5. 命中后毫秒级修补
    if (dllBase) {
        FastSilentPatch(pi.hProcess, dllBase, dllSize, TARGET_OLD_VER, TARGET_NEW_VER);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // 6. 瞬间退出自身，不留任何延迟，不占内存
    return 0;
}
