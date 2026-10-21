#include "project.h"
#include <fstream>
#include <iostream>

bool SaturnProject::CreateNew(const std::string &folderPath, const std::string &name)
{
    try
    {
        projectRoot = folderPath;
        projectName = name;
        manifestFilePath = projectRoot + "/" + projectName + ".saturn";
        startupScene = "assets/scenes/Main.scn";

        // Cria a estrutura física de pastas do projeto
        fs::create_directories(GetAssetsPath());
        fs::create_directories(GetTexturesPath());
        fs::create_directories(GetScriptsPath());
        fs::create_directories(GetScenesPath());
        fs::create_directories(GetBuildPath());

        // Salva o manifesto inicial
        if (!SaveManifest())
        {
            return false;
        }

        isLoaded = true;
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Erro ao criar projeto: " << e.what() << std::endl;
        return false;
    }
}

bool SaturnProject::SaveManifest()
{
    std::ofstream manifest(manifestFilePath, std::ios::trunc);
    if (!manifest.is_open()) return false;

    manifest << "project_name=" << projectName << "\n";
    manifest << "startup_scene=" << startupScene << "\n";
    manifest.close();
    return true;
}

bool SaturnProject::Load(const std::string &manifestPath)
{
    if (!fs::exists(manifestPath)) return false;

    std::ifstream manifest(manifestPath);
    if (!manifest.is_open()) return false;

    manifestFilePath = manifestPath;
    projectRoot = fs::path(manifestPath).parent_path().string();
    startupScene = "assets/scenes/Main.scn"; // Valor padrão caso não exista na chave

    std::string line;
    while (std::getline(manifest, line))
    {
        if (line.rfind("project_name=", 0) == 0)
        {
            projectName = line.substr(13);
        }
        else if (line.rfind("startup_scene=", 0) == 0)
        {
            startupScene = line.substr(14);
        }
    }
    manifest.close();

    isLoaded = !projectName.empty();
    return isLoaded;
}