#include "app_icon.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>

void SetAppIconFromResource(void* nativeWindowHandle, const char* resourceName) {
    HWND hwnd = (HWND)nativeWindowHandle;
    if (hwnd != NULL) {
        HINSTANCE hInstance = GetModuleHandle(NULL);
        
        // Tenta pelo nome da string ou ordinal 1
        HICON hIcon = (HICON)LoadImageA(hInstance, resourceName, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        if (!hIcon) {
            hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        }
        
        if (hIcon) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
    }
}
#else
void SetAppIconFromResource(void*, const char*) {}
#endif