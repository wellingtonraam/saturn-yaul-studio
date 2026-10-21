#pragma once

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

struct SaturnProject
{
    bool isLoaded = false;
    std::string projectRoot = "";
    std::string projectName = "";
    std::string manifestFilePath = "";
    std::string startupScene = "assets/scenes/Main.scn";

    std::string GetAssetsPath() const { return projectRoot + "/assets"; }
    std::string GetTexturesPath() const { return projectRoot + "/assets/textures"; }
    std::string GetScenesPath() const { return projectRoot + "/assets/scenes"; }
    std::string GetBuildPath() const { return projectRoot + "/build"; }
    std::string GetScriptsPath() const { return projectRoot + "/assets/scripts"; }

    bool CreateNew(const std::string &folderPath, const std::string &name);
    bool Load(const std::string &manifestPath);
    bool SaveManifest();
};