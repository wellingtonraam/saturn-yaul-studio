#include "platform_dialogs.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <thread>
#include <atomic>

std::string OpenFileDialog(const char* filter) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

std::string SaveFileDialog(const char* filter, const char* defaultExt) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = defaultExt;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

std::string ChooseFolderDialog(const char* title) {
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != 0) {
        CHAR path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            IMalloc* imalloc = 0;
            if (SUCCEEDED(SHGetMalloc(&imalloc))) {
                imalloc->Free(pidl);
                imalloc->Release();
            }
            return std::string(path);
        }
    }
    return "";
}

static std::atomic<bool> s_IsBuilding(false);

void ExecuteCommandAsync(
    const std::string& cmd, 
    const std::string& workingDir, 
    std::function<void(const std::string&)> onOutputLine, 
    std::function<void(int)> onComplete) 
{
    if (s_IsBuilding) {
        if (onOutputLine) onOutputLine("[Warning] A build process is already in progress.");
        return;
    }

    s_IsBuilding = true;
    std::thread([cmd, workingDir, onOutputLine, onComplete]() {
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        HANDLE hChildStd_OUT_Rd = NULL;
        HANDLE hChildStd_OUT_Wr = NULL;

        if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
            if (onOutputLine) onOutputLine("[Error] Failed to create system pipe.");
            s_IsBuilding = false;
            if (onComplete) onComplete(-1);
            return;
        }

        SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

        PROCESS_INFORMATION piProcInfo;
        STARTUPINFOA siStartInfo;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));

        siStartInfo.cb = sizeof(STARTUPINFOA);
        siStartInfo.hStdError = hChildStd_OUT_Wr;
        siStartInfo.hStdOutput = hChildStd_OUT_Wr;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        char cmdBuffer[1024];
        strncpy(cmdBuffer, cmd.c_str(), sizeof(cmdBuffer));
        cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

        if (onOutputLine) onOutputLine(">> Executing: " + cmd);

        BOOL bSuccess = CreateProcessA(
            NULL, cmdBuffer, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
            workingDir.empty() ? NULL : workingDir.c_str(),
            &siStartInfo, &piProcInfo
        );

        CloseHandle(hChildStd_OUT_Wr);

        if (!bSuccess) {
            if (onOutputLine) onOutputLine("[Error] Could not start build process. Verify make path in Preferences.");
            CloseHandle(hChildStd_OUT_Rd);
            s_IsBuilding = false;
            if (onComplete) onComplete(-1);
            return;
        }

        DWORD dwRead;
        CHAR chBuf[512];
        std::string currentLine = "";

        while (ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf) - 1, &dwRead, NULL) && dwRead > 0) {
            chBuf[dwRead] = '\0';
            for (DWORD i = 0; i < dwRead; ++i) {
                if (chBuf[i] == '\n') {
                    if (!currentLine.empty() && currentLine.back() == '\r') currentLine.pop_back();
                    if (onOutputLine) onOutputLine(currentLine);
                    currentLine.clear();
                } else {
                    currentLine += chBuf[i];
                }
            }
        }

        if (!currentLine.empty() && onOutputLine) {
            onOutputLine(currentLine);
        }

        WaitForSingleObject(piProcInfo.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(piProcInfo.hProcess, &exitCode);

        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        CloseHandle(hChildStd_OUT_Rd);

        s_IsBuilding = false;
        if (onComplete) onComplete((int)exitCode);
    }).detach();
}

bool LaunchEmulator(const std::string& emulatorPath, const std::string& isoOrCuePath) {
    if (emulatorPath.empty()) return false;

    std::string cmd = "\"" + emulatorPath + "\" \"" + isoOrCuePath + "\"";
    char cmdBuffer[1024];
    strncpy(cmdBuffer, cmd.c_str(), sizeof(cmdBuffer));
    cmdBuffer[sizeof(cmdBuffer) - 1] = '\0';

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    std::string emuDir = "";
    size_t lastSlash = emulatorPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        emuDir = emulatorPath.substr(0, lastSlash);
    }

    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE, 0, NULL, emuDir.empty() ? NULL : emuDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

#else

std::string OpenFileDialog(const char* filter) { return ""; }
std::string SaveFileDialog(const char* filter, const char* defaultExt) { return ""; }
std::string ChooseFolderDialog(const char* title) { return ""; }
void ExecuteCommandAsync(const std::string&, const std::string&, std::function<void(const std::string&)>, std::function<void(int)> onComplete) { if (onComplete) onComplete(-1); }
bool LaunchEmulator(const std::string&, const std::string&) { return false; }

#endif