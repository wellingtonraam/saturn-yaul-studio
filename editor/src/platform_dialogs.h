#pragma once
#include <string>
#include <functional>

std::string OpenFileDialog(const char* filter);
std::string SaveFileDialog(const char* filter, const char* defaultExt);
std::string ChooseFolderDialog(const char* title);

// Funções de Processo do Sistema (Isoladas do Raylib)
void ExecuteCommandAsync(
    const std::string& cmd, 
    const std::string& workingDir, 
    std::function<void(const std::string&)> onOutputLine, 
    std::function<void(int)> onComplete
);

bool LaunchEmulator(const std::string& emulatorPath, const std::string& isoOrCuePath);