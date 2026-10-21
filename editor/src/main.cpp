
#define IMGUI_DEFINE_MATH_OPERATORS

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_icon.h"
#include "imgui.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "rlImGui.h"
#include "platform_dialogs.h"
#include "scene.h"
#include "scene_serializer.h"
#include "gizmo.h"
#include "yaul_exporter.h"
#include "project.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <algorithm>

namespace fs = std::filesystem;

static bool isSceneDirty = false;
static std::string pendingSceneToLoad = "";
static bool showUnsavedChangesModal = false;

// Sistema de Log Seguro para Threads (Console Interno)
struct EditorConsole
{
    std::mutex mtx;
    std::vector<std::pair<std::string, ImVec4>> logs;

    void Log(const std::string &msg, ImVec4 color = ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
    {
        std::lock_guard<std::mutex> lock(mtx);
        logs.push_back({msg, color});
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mtx);
        logs.clear();
    }
};

static EditorConsole g_Console;

void CreateDefaultScene(std::vector<Entity> &entities, int &nextId, int &selectedId)
{
    entities.clear();
    nextId = 1;

    Entity cam;
    cam.id = nextId++;
    cam.name = "Main Camera";
    cam.type = EntityType::Camera;
    cam.position[0] = 0.0f;
    cam.position[1] = 0.0f;
    cam.position[2] = 0.0f;
    cam.visible = true;
    entities.push_back(cam);

    selectedId = cam.id;
}

void LoadProjectStartupScene(SaturnProject &project, std::vector<Entity> &entities, int &nextEntityId, int &selectedEntityId, std::string &currentScenePath)
{
    fs::path root(project.projectRoot);
    fs::path startupRel(project.startupScene.empty() ? "assets/scenes/Main.scn" : project.startupScene);
    fs::path fullTargetPath = root / startupRel;

    bool sceneLoaded = false;

    // 1. Tenta carregar a startup_scene definida no manifesto
    if (fs::exists(fullTargetPath))
    {
        if (SceneSerializer::Load(fullTargetPath.string(), entities, nextEntityId))
        {
            currentScenePath = fullTargetPath.string();
            selectedEntityId = entities.empty() ? -1 : entities[0].id;
            g_Console.Log("Auto-loaded startup scene: " + startupRel.string(), ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
            sceneLoaded = true;
        }
    }

    // 2. Fallback: Se o usuário deletou a cena inicial, varre por qualquer outro .scn
    if (!sceneLoaded)
    {
        fs::path scenesDir = project.GetScenesPath();
        if (fs::exists(scenesDir))
        {
            for (const auto &entry : fs::directory_iterator(scenesDir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".scn")
                {
                    if (SceneSerializer::Load(entry.path().string(), entities, nextEntityId))
                    {
                        currentScenePath = entry.path().string();
                        selectedEntityId = entities.empty() ? -1 : entities[0].id;

                        // Auto-repara o manifesto salvando a nova cena encontrada
                        project.startupScene = "assets/scenes/" + entry.path().filename().string();
                        project.SaveManifest();

                        g_Console.Log("[Auto-Repair] Startup scene restored to: " + project.startupScene, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                        sceneLoaded = true;
                        break;
                    }
                }
            }
        }
    }

    // 3. Fallback de emergência: se a pasta estiver completamente vazia, recria a Main.scn
    if (!sceneLoaded)
    {
        CreateDefaultScene(entities, nextEntityId, selectedEntityId);
        std::string defaultSceneFile = project.projectRoot + "/assets/scenes/Main.scn";
        SceneSerializer::Save(defaultSceneFile, entities);

        project.startupScene = "assets/scenes/Main.scn";
        project.SaveManifest();

        currentScenePath = defaultSceneFile;
        g_Console.Log("[Auto-Repair] Recreated missing Main.scn with default Camera.", ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    }
}

// --- GERENCIAMENTO DE PROJETOS RECENTES ---
static std::vector<std::string> g_RecentProjects;
static const size_t MAX_RECENT_PROJECTS = 8;
static const std::string RECENT_PROJECTS_FILE = "recent_projects.txt";

void LoadRecentProjects()
{
    g_RecentProjects.clear();
    std::ifstream file(RECENT_PROJECTS_FILE);
    if (file.is_open())
    {
        std::string path;
        while (std::getline(file, path))
        {
            if (!path.empty() && fs::exists(path))
            {
                g_RecentProjects.push_back(path);
            }
        }
        file.close();
    }
}

void SaveRecentProjects()
{
    std::ofstream file(RECENT_PROJECTS_FILE, std::ios::trunc);
    if (file.is_open())
    {
        for (const auto &path : g_RecentProjects)
        {
            file << path << "\n";
        }
        file.close();
    }
}

void AddRecentProject(const std::string &projectFilePath)
{
    if (projectFilePath.empty())
        return;

    std::string cleanPath = fs::path(projectFilePath).lexically_normal().string();

    auto it = std::remove(g_RecentProjects.begin(), g_RecentProjects.end(), cleanPath);
    g_RecentProjects.erase(it, g_RecentProjects.end());

    g_RecentProjects.insert(g_RecentProjects.begin(), cleanPath);

    if (g_RecentProjects.size() > MAX_RECENT_PROJECTS)
    {
        g_RecentProjects.resize(MAX_RECENT_PROJECTS);
    }

    SaveRecentProjects();
}

static std::vector<std::vector<Entity>> g_UndoStack;
static std::vector<std::vector<Entity>> g_RedoStack;
const size_t MAX_UNDO_STEPS = 50;

void PushUndoState(const std::vector<Entity> &currentEntities)
{
    g_UndoStack.push_back(currentEntities);
    if (g_UndoStack.size() > MAX_UNDO_STEPS)
        g_UndoStack.erase(g_UndoStack.begin());
    g_RedoStack.clear();
    isSceneDirty = true;
}

void PerformUndo(std::vector<Entity> &entities, int &selectedId)
{
    if (g_UndoStack.empty())
        return;
    g_RedoStack.push_back(entities);
    entities = g_UndoStack.back();
    g_UndoStack.pop_back();
    isSceneDirty = true;
    g_Console.Log("Undo performed.", ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
}

void PerformRedo(std::vector<Entity> &entities, int &selectedId)
{
    if (g_RedoStack.empty())
        return;
    g_UndoStack.push_back(entities);
    entities = g_RedoStack.back();
    g_RedoStack.pop_back();
    isSceneDirty = true;
    g_Console.Log("Redo performed.", ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
}

// 4. Configurações Globais do Editor
struct EditorSettings
{
    std::string makePath = "make";
    std::string emulatorPath = "";

    void Load()
    {
        std::ifstream file("editor_settings.ini");
        if (!file.is_open())
            return;
        std::string line;
        while (std::getline(file, line))
        {
            size_t eq = line.find('=');
            if (eq != std::string::npos)
            {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "MakePath")
                    makePath = v;
                if (k == "EmulatorPath")
                    emulatorPath = v;
            }
        }
        file.close();
    }

    void Save()
    {
        std::ofstream file("editor_settings.ini", std::ios::trunc);
        if (!file.is_open())
            return;
        file << "MakePath=" << makePath << "\n";
        file << "EmulatorPath=" << emulatorPath << "\n";
        file.close();
    }
};

static EditorSettings g_Settings;

// 5. AssetManager Centralizado
class AssetManager
{
public:
    std::unordered_map<std::string, Texture2D> textures;

    Texture2D *GetTexture(const std::string &fullPath)
    {
        if (fullPath.empty())
            return nullptr;

        auto it = textures.find(fullPath);
        if (it != textures.end())
        {
            return &it->second;
        }

        if (fs::exists(fullPath))
        {
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.id > 0)
            {
                SetTextureFilter(tex, TEXTURE_FILTER_POINT);
                textures[fullPath] = tex;
                return &textures[fullPath];
            }
        }
        return nullptr;
    }

    void Clear()
    {
        for (auto &pair : textures)
        {
            UnloadTexture(pair.second);
        }
        textures.clear();
    }
};

// --- INTERSEÇÃO 3D ---

static void DrawVdp1Quad(Texture2D *tex, Color tint)
{
    if (tex != nullptr && tex->id > 0)
    {
        rlSetTexture(tex->id);
        rlBegin(RL_QUADS);
        rlColor4ub(tint.r, tint.g, tint.b, tint.a);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(-0.5f, -0.5f, 0.0f);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(0.5f, -0.5f, 0.0f);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(0.5f, 0.5f, 0.0f);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(-0.5f, 0.5f, 0.0f);

        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(0.5f, -0.5f, 0.0f);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(-0.5f, -0.5f, 0.0f);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(-0.5f, 0.5f, 0.0f);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(0.5f, 0.5f, 0.0f);
        rlEnd();
        rlSetTexture(0);
    }
    else
    {
        DrawCube(Vector3{0.0f, 0.0f, 0.0f}, 1.0f, 1.0f, 1.0f, tint);
    }
}

// Converte um mapa exportado pelo Tiled (formato JSON simples) para o .bin binário de 8192 bytes do VDP2
bool ConvertTiledJsonToSaturnBin(const std::string &jsonPath, const std::string &outBinPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Localiza a propriedade "data": [ ... ] da primeira camada (layer)
    size_t dataPos = content.find("\"data\":");
    if (dataPos == std::string::npos)
        return false;

    size_t startBracket = content.find('[', dataPos);
    size_t endBracket = content.find(']', startBracket);
    if (startBracket == std::string::npos || endBracket == std::string::npos)
        return false;

    std::string dataStr = content.substr(startBracket + 1, endBracket - startBracket - 1);
    std::stringstream ss(dataStr);
    std::string token;

    std::vector<uint16_t> mapIndices(64 * 64, 0);
    int count = 0;

    while (std::getline(ss, token, ',') && count < (64 * 64))
    {
        // Limpa espaços e quebras de linha
        token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
        if (!token.empty())
        {
            try
            {
                int tileVal = std::stoi(token);
                // No Tiled: 0 = vazio/transparente. Os tiles do PNG começam no índice 1.
                // Como adicionamos o Tile 0 transparente no CPD do Saturn, o valor casa 1:1.
                if (tileVal > 0)
                {
                    mapIndices[count] = (uint16_t)(tileVal * 2);
                }
                else
                {
                    mapIndices[count] = 0; // 0 = Tile 0 Transparente
                }
            }
            catch (...)
            {
                mapIndices[count] = 0;
            }
            count++;
        }
    }

    // Grava exatamente 8192 bytes (64x64 células * 2 bytes por palavra PND)
    std::ofstream binOut(outBinPath, std::ios::binary | std::ios::trunc);
    if (!binOut.is_open())
        return false;

    for (uint16_t val : mapIndices)
    {
        // 1-Word PND nativo do Saturn em Big-Endian (High Byte primeiro, depois Low Byte)
        uint16_t pndEntry = val & 0x0FFF;
        uint8_t high = (pndEntry >> 8) & 0xFF;
        uint8_t low = pndEntry & 0xFF;

        binOut.write((char *)&high, 1);
        binOut.write((char *)&low, 1);
    }
    binOut.close();

    return true;
}

// Converte o tileset PNG para 256 cores (8 bpp) nativo do VDP2 com Tile 0 transparente
bool ConvertTilesetPngToSaturnBin(const std::string &pngPath, const std::string &outCpdPath, const std::string &outPalPath)
{
    Image img = LoadImage(pngPath.c_str());
    if (img.data == nullptr)
        return false;

    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color *pixels = (Color *)img.data;

    std::vector<Color> palette;
    palette.reserve(256);
    // Cor 0 reservada para transparência
    palette.push_back(Color{0, 0, 0, 0});

    // 1. Coleta todas as cores reais do PNG (até 255 cores)
    for (int i = 0; i < img.width * img.height; ++i)
    {
        Color c = pixels[i];
        if (c.a < 128)
            continue; // Transparente

        c.r = (c.r >> 3) << 3;
        c.g = (c.g >> 3) << 3;
        c.b = (c.b >> 3) << 3;
        c.a = 255;

        bool exists = false;
        for (const auto &p : palette)
        {
            if (p.r == c.r && p.g == c.g && p.b == c.b && p.a > 0)
            {
                exists = true;
                break;
            }
        }
        if (!exists && palette.size() < 256)
        {
            palette.push_back(c);
        }
    }

    while (palette.size() < 256)
    {
        palette.push_back(Color{0, 0, 0, 255});
    }

    int tilesX = img.width / 8;
    int tilesY = img.height / 8;

    std::vector<uint8_t> cpdData;
    // Em 8 bpp: 1 tile de 8x8 pixels = 64 bytes (1 byte por pixel)
    // Tile 0 Vazio (64 bytes de 0x00 para transparência completa)
    cpdData.assign(64, 0);

    auto getPalIdx = [&](Color c) -> uint8_t
    {
        if (c.a < 128)
            return 0;
        uint8_t r = (c.r >> 3) << 3;
        uint8_t g = (c.g >> 3) << 3;
        uint8_t b = (c.b >> 3) << 3;
        for (size_t i = 1; i < palette.size(); ++i)
        {
            if (palette[i].r == r && palette[i].g == g && palette[i].b == b)
                return (uint8_t)i;
        }
        return 0;
    };

    // Empacota em 8 bpp (64 bytes por tile)
    for (int ty = 0; ty < tilesY; ++ty)
    {
        for (int tx = 0; tx < tilesX; ++tx)
        {
            for (int py = 0; py < 8; ++py)
            {
                for (int px = 0; px < 8; ++px)
                {
                    int x = (tx * 8) + px;
                    int y = (ty * 8) + py;
                    Color c = pixels[y * img.width + x];
                    cpdData.push_back(getPalIdx(c));
                }
            }
        }
    }

    UnloadImage(img);

    std::ofstream cpdOut(outCpdPath, std::ios::binary | std::ios::trunc);
    if (!cpdOut.is_open())
        return false;
    cpdOut.write((char *)cpdData.data(), cpdData.size());
    cpdOut.close();

    // Paleta Binária Big-Endian de 256 cores (512 bytes = 256 cores * 2 bytes)
    std::ofstream palOut(outPalPath, std::ios::binary | std::ios::trunc);
    if (!palOut.is_open())
        return false;
    for (int i = 0; i < 256; ++i)
    {
        uint16_t satColor = 0x0000;
        if (i > 0)
        {
            uint16_t r = (palette[i].r >> 3) & 0x1F;
            uint16_t g = (palette[i].g >> 3) & 0x1F;
            uint16_t b = (palette[i].b >> 3) & 0x1F;
            satColor = 0x8000 | (b << 10) | (g << 5) | r;
        }

        uint8_t high = (satColor >> 8) & 0xFF;
        uint8_t low = satColor & 0xFF;
        palOut.write((char *)&high, 1);
        palOut.write((char *)&low, 1);
    }
    palOut.close();

    return true;
}

int main()
{

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);

    const int screenWidth = 1280;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Saturn YAUL Studio - [Editor]");
    SetAppIconFromResource(GetWindowHandle(), "IDI_ICON1");

    SetTargetFPS(60);

    rlImGuiSetup(true);

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    RenderTexture2D saturnScreen = LoadRenderTexture(320, 224);

    Camera3D camera = {0};
    camera.position = Vector3{0.0f, 3.0f, 6.0f};
    camera.target = Vector3{0.0f, 0.0f, 0.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SaturnProject project;
    AssetManager assetMgr;

    std::vector<Entity> entities;
    int nextEntityId = 1;
    int selectedEntityId = -1;
    std::string currentScenePath = "";

    EditorToolMode currentTool = EditorToolMode::Translate;
    GizmoAxis activeGizmoAxis = GizmoAxis::None;
    GizmoAxis lastModifiedAxis = GizmoAxis::None;
    bool isDraggingGizmo = false;

    const float GIZMO_FIXED_SIZE = 1.4f;

    bool showNewProjectModal = false;
    bool showPreferencesModal = false;
    char newProjectName[64] = "MySaturnGame";
    char newProjectFolder[256] = "C:/";

    char prefMakePath[256] = "";
    char prefEmuPath[256] = "";

    g_Settings.Load();
    strncpy(prefMakePath, g_Settings.makePath.c_str(), sizeof(prefMakePath));
    strncpy(prefEmuPath, g_Settings.emulatorPath.c_str(), sizeof(prefEmuPath));

    LoadRecentProjects();

    g_Console.Log("Saturn Studio initialized.", ImVec4(0.4f, 0.8f, 0.4f, 1.0f));

    while (!WindowShouldClose())
    {
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z))
        {
            if (IsKeyDown(KEY_LEFT_SHIFT))
                PerformRedo(entities, selectedEntityId);
            else
                PerformUndo(entities, selectedEntityId);
        }

        Entity *activeEnt = nullptr;
        for (auto &ent : entities)
        {
            if (ent.id == selectedEntityId)
            {
                activeEnt = &ent;
                break;
            }
        }

        // --- 1. VIEWPORT SATURN ---
        BeginTextureMode(saturnScreen);
        ClearBackground(Color{20, 24, 30, 255});
        BeginMode3D(camera);
        DrawGrid(10, 1.0f);

        for (const auto &ent : entities)
        {
            if (!ent.visible)
                continue;

            rlPushMatrix();
            rlTranslatef(ent.position[0], ent.position[1], ent.position[2]);
            rlRotatef(ent.rotation[0], 1.0f, 0.0f, 0.0f);
            rlRotatef(ent.rotation[1], 0.0f, 1.0f, 0.0f);
            rlRotatef(ent.rotation[2], 0.0f, 0.0f, 1.0f);
            rlScalef(ent.scale[0], ent.scale[1], ent.scale[2]);

            Color rColor = {
                (unsigned char)(ent.color[0] * 255),
                (unsigned char)(ent.color[1] * 255),
                (unsigned char)(ent.color[2] * 255),
                255};

            Texture2D *activeTex = nullptr;
            // Não tenta carregar arquivos .bin de VDP2 como textura padrão do Raylib
            if (!ent.texturePath.empty() && project.isLoaded && ent.type != EntityType::VDP2_Plane)
            {
                activeTex = assetMgr.GetTexture(project.projectRoot + "/" + ent.texturePath);
            }

            if (ent.type == EntityType::VDP1_Quad || ent.type == EntityType::VDP1_Sprite)
            {
                DrawVdp1Quad(activeTex, rColor);
            }

            else if (ent.type == EntityType::VDP2_Plane)
            {
                const float tileSize = 0.5f;
                const int cells = 64;
                const float totalW = cells * tileSize;
                const float totalH = cells * tileSize;
                const float halfW = totalW * 0.5f;
                const float halfH = totalH * 0.5f;

                // Moldura externa da tela NBG0
                DrawCubeWires(Vector3{0.0f, 0.0f, -0.01f}, totalW, totalH, 0.01f, Color{0, 180, 255, 180});

                // Carrega a textura original PNG do tileset
                Texture2D *tilesetTex = nullptr;
                if (!ent.texturePath.empty() && project.isLoaded)
                {
                    std::string pngRel = ent.texturePath;
                    size_t cpdPos = pngRel.find(".cpd.bin");
                    if (cpdPos != std::string::npos)
                        pngRel.replace(cpdPos, 8, ".png");
                    tilesetTex = assetMgr.GetTexture(project.projectRoot + "/" + pngRel);
                }

                std::vector<uint16_t> currentMap(cells * cells, 0);
                bool hasMap = false;
                if (!ent.mapPath.empty())
                {
                    std::string fullMapPath = project.projectRoot + "/" + ent.mapPath;
                    std::ifstream mFile(fullMapPath, std::ios::binary);
                    if (mFile.is_open())
                    {
                        std::vector<uint8_t> rawBytes(cells * cells * 2, 0);
                        mFile.read((char *)rawBytes.data(), rawBytes.size());
                        // Desempacota Big-Endian (High Byte primeiro, depois Low Byte)
                        for (size_t i = 0; i < cells * cells; ++i)
                        {
                            uint8_t high = rawBytes[i * 2 + 0];
                            uint8_t low = rawBytes[i * 2 + 1];
                            currentMap[i] = ((uint16_t)high << 8) | (uint16_t)low;
                        }
                        hasMap = true;
                    }
                }

                int tilesPerRow = (tilesetTex && tilesetTex->width > 0) ? (tilesetTex->width / 8) : 10;

                for (int y = 0; y < cells; ++y)
                {
                    for (int x = 0; x < cells; ++x)
                    {
                        float px = -halfW + (x * tileSize) + (tileSize * 0.5f);
                        float py = halfH - (y * tileSize) - (tileSize * 0.5f);

                        uint16_t tileId = hasMap ? currentMap[y * cells + x] : 0;

                        if (tileId > 0 && tilesetTex && tilesetTex->id > 0)
                        {
                            // tileId 1 corresponde ao primeiro tile do PNG (índice 0 no PNG)
                            int realTile = tileId / 2;
                            int rawTileIdx = realTile - 1;
                            int srcX = (rawTileIdx % tilesPerRow) * 8;
                            int srcY = (rawTileIdx / tilesPerRow) * 8;
                            Rectangle srcRec = {(float)srcX, (float)srcY, 8.0f, 8.0f};

                            // Desenha na viewport
                            rlSetTexture(tilesetTex->id);
                            rlBegin(RL_QUADS);
                            rlColor4ub(255, 255, 255, 255);
                            rlTexCoord2f(srcRec.x / (float)tilesetTex->width, srcRec.y / (float)tilesetTex->height);
                            rlVertex3f(px - tileSize * 0.5f, py + tileSize * 0.5f, 0.0f);

                            rlTexCoord2f(srcRec.x / (float)tilesetTex->width, (srcRec.y + srcRec.height) / (float)tilesetTex->height);
                            rlVertex3f(px - tileSize * 0.5f, py - tileSize * 0.5f, 0.0f);

                            rlTexCoord2f((srcRec.x + srcRec.width) / (float)tilesetTex->width, (srcRec.y + srcRec.height) / (float)tilesetTex->height);
                            rlVertex3f(px + tileSize * 0.5f, py - tileSize * 0.5f, 0.0f);

                            rlTexCoord2f((srcRec.x + srcRec.width) / (float)tilesetTex->width, srcRec.y / (float)tilesetTex->height);
                            rlVertex3f(px + tileSize * 0.5f, py + tileSize * 0.5f, 0.0f);
                            rlEnd();
                            rlSetTexture(0);
                        }

                        // Desenho do Tilemap Collider 
                        if (ent.tilemapColliderActive && tileId > 0)
                        {
                            int realTile = tileId / 2;
                            if (realTile >= ent.solidTileMin && realTile <= ent.solidTileMax)
                            {
                                DrawRectangleLinesEx(
                                    Rectangle{ px - tileSize * 0.5f, -py - tileSize * 0.5f, tileSize, tileSize },
                                    0.02f,
                                    Color{ 0, 255, 64, 180 }
                                );
                            }
                        }
                    }
                }
            }

            else if (ent.type == EntityType::Camera)
            {
                // Enquadramento da TV do Sega Saturn (320x224 com escala 16px por unidade)
                const float camW = 320.0f / 16.0f; // 20.0f
                const float camH = 224.0f / 16.0f; // 14.0f
                DrawCubeWires(Vector3{0.0f, 0.0f, 0.0f}, camW, camH, 0.1f, Color{0, 200, 255, 200});
                DrawCube(Vector3{0.0f, 0.0f, 0.0f}, 0.6f, 0.6f, 0.6f, SKYBLUE);
            }

            else if (ent.type == EntityType::Empty)
            {
                DrawCubeWires(Vector3{0.0f, 0.0f, 0.0f}, 0.2f, 0.2f, 0.2f, YELLOW);
            }

            if (activeEnt && activeEnt->id == ent.id)
            {
                DrawCubeWires(Vector3{0.0f, 0.0f, 0.0f}, 1.02f, 1.02f, 1.02f, ORANGE);
            }

            // Desenha o Colisor AABB visível
            if (ent.hasCollider)
            {
                rlPushMatrix();
                rlTranslatef(ent.colliderOffset[0], ent.colliderOffset[1], 0.0f);
                // Como já estamos dentro do rlScalef(ent.scale), o cubo base (1.0) * colliderSize cobre o objeto
                DrawCubeWires(Vector3{0.0f, 0.0f, 0.0f}, ent.colliderSize[0], ent.colliderSize[1], 0.05f, GREEN);
                rlPopMatrix();
            }

            rlPopMatrix();
        }
        EndMode3D();

        // --- 2. GIZMO VISÍVEL ---
        if (activeEnt != nullptr && activeEnt->visible && currentTool != EditorToolMode::Hand)
        {
            DrawGizmo(currentTool, *activeEnt, camera, activeGizmoAxis, lastModifiedAxis, GIZMO_FIXED_SIZE);
        }

        EndTextureMode();

        // --- 3. INTERFACE (IMGUI) ---
        BeginDrawing();
        ClearBackground(DARKGRAY);

        rlImGuiBegin();

        ImGuiViewport *mainViewport = ImGui::GetMainViewport();
        ImGui::DockSpaceOverViewport(mainViewport->ID, mainViewport, ImGuiDockNodeFlags_PassthruCentralNode);

        // --- START SCREEN / PROJECT HUB ---
        if (!project.isLoaded)
        {
            ImGui::SetNextWindowSize(ImVec2(720, 440), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoResize;

            if (ImGui::Begin("Welcome to Saturn YAUL Studio", nullptr, hubFlags))
            {
                ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "SATURN YAUL STUDIO");
                ImGui::TextDisabled("Sega Saturn 2D/3D Engine & Toolchain Environment");
                ImGui::Separator();
                ImGui::Spacing();

                // Coluna Esquerda: Ações Rápidas
                ImGui::BeginChild("HubLeftPane", ImVec2(220, 0), true);
                {
                    ImGui::Text("Actions");
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("+ New Project", ImVec2(-1, 40)))
                    {
                        showNewProjectModal = true;
                    }

                    ImGui::Spacing();

                    if (ImGui::Button("Open Project...", ImVec2(-1, 40)))
                    {
                        std::string projFile = OpenFileDialog("Saturn Studio Project (*.saturnproj)\0*.saturnproj\0All Files (*.*)\0*.*\0");
                        if (!projFile.empty())
                        {
                            assetMgr.Clear();
                            if (project.Load(projFile))
                            {
                                AddRecentProject(projFile);
                                SaveRecentProjects();
                                g_Console.Log("Project Opened: " + project.projectName, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                                LoadProjectStartupScene(project, entities, nextEntityId, selectedEntityId, currentScenePath);
                            }
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Preferences", ImVec2(-1, 30)))
                    {
                        showPreferencesModal = true;
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                // Coluna Direita: Projetos Recentes
                ImGui::BeginChild("HubRightPane", ImVec2(0, 0), true);
                {
                    ImGui::Text("Recent Projects");
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (g_RecentProjects.empty())
                    {
                        ImGui::TextDisabled("No recent projects found.");
                    }
                    else
                    {
                        for (const auto &recentPath : g_RecentProjects)
                        {
                            std::string projName = fs::path(recentPath).parent_path().filename().string();

                            ImGui::PushID(recentPath.c_str());
                            if (ImGui::Selectable(projName.c_str(), false, ImGuiSelectableFlags_DontClosePopups, ImVec2(0, 36)))
                            {
                                if (fs::exists(recentPath))
                                {
                                    assetMgr.Clear();
                                    if (project.Load(recentPath))
                                    {
                                        AddRecentProject(recentPath);
                                        SaveRecentProjects();
                                        g_Console.Log("Project Opened: " + project.projectName, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                                        LoadProjectStartupScene(project, entities, nextEntityId, selectedEntityId, currentScenePath);
                                    }
                                }
                                else
                                {
                                    g_Console.Log("[Error] File not found: " + recentPath, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                }
                            }
                            ImGui::TextDisabled("  %s", recentPath.c_str());
                            ImGui::Separator();
                            ImGui::PopID();
                        }
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        // Barra de Menus
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Project..."))
                {
                    showNewProjectModal = true;
                }
                if (ImGui::MenuItem("Open Project (.saturnproj)..."))
                {
                    std::string projFile = OpenFileDialog("Saturn Studio Project (*.saturnproj)\0*.saturnproj\0All Files (*.*)\0*.*\0");
                    if (!projFile.empty())
                    {
                        assetMgr.Clear();
                        if (project.Load(projFile))
                        {
                            AddRecentProject(projFile);
                            SaveRecentProjects();
                            g_Console.Log("Project Opened: " + project.projectName, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

                            // Carrega a cena inicial do projeto
                            LoadProjectStartupScene(project, entities, nextEntityId, selectedEntityId, currentScenePath);
                        }
                    }
                }

                // Submenu: Recent Projects
                if (ImGui::BeginMenu("Open Recent", !g_RecentProjects.empty()))
                {
                    for (const auto &recentPath : g_RecentProjects)
                    {
                        std::string projName = fs::path(recentPath).parent_path().filename().string();
                        std::string label = projName + "  (" + recentPath + ")";
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            if (fs::exists(recentPath))
                            {
                                assetMgr.Clear();
                                if (project.Load(recentPath))
                                {
                                    AddRecentProject(recentPath);
                                    SaveRecentProjects();
                                    g_Console.Log("Project Opened: " + project.projectName, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

                                    // Carrega a cena inicial do projeto
                                    LoadProjectStartupScene(project, entities, nextEntityId, selectedEntityId, currentScenePath);
                                }
                                else
                                {
                                    g_Console.Log("[Error] Failed to load project: " + recentPath, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                }
                            }
                            else
                            {
                                g_Console.Log("[Error] File not found: " + recentPath, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Projects"))
                    {
                        g_RecentProjects.clear();
                        SaveRecentProjects();
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::MenuItem("New Scene", nullptr, false, project.isLoaded))
                {
                    std::string savePath = SaveFileDialog("Sega Saturn Scene (*.scn)\0*.scn\0All Files (*.*)\0*.*\0", "scn");
                    if (!savePath.empty())
                    {
                        CreateDefaultScene(entities, nextEntityId, selectedEntityId);
                        SceneSerializer::Save(savePath, entities);
                        currentScenePath = savePath;
                        g_Console.Log("Created and saved new scene: " + fs::path(savePath).filename().string(), ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                    }
                }
                if (ImGui::MenuItem("Save Scene", nullptr, false, project.isLoaded))
                {
                    if (currentScenePath.empty())
                    {
                        std::string defaultFolder = project.isLoaded ? project.GetScenesPath() : "";
                        currentScenePath = SaveFileDialog("Sega Saturn Scene (*.scn)\0*.scn\0All Files (*.*)\0*.*\0", "scn");
                    }
                    if (!currentScenePath.empty())
                    {
                        if (SceneSerializer::Save(currentScenePath, entities))
                        {
                            g_Console.Log("Saved scene: " + currentScenePath, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                        }
                    }
                }
                if (ImGui::MenuItem("Save Scene As...", nullptr, false, project.isLoaded))
                {
                    std::string selectedPath = SaveFileDialog("Sega Saturn Scene (*.scn)\0*.scn\0All Files (*.*)\0*.*\0", "scn");
                    if (!selectedPath.empty())
                    {
                        currentScenePath = selectedPath;
                        SceneSerializer::Save(currentScenePath, entities);
                        ;
                        g_Console.Log("Saved scene as: " + currentScenePath, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                    }
                }
                // 2. Abrir uma cena existente do disco
                if (ImGui::MenuItem("Open Scene (.scn)...", nullptr, false, project.isLoaded))
                {
                    std::string selectedPath = OpenFileDialog("Sega Saturn Scene (*.scn)\0*.scn\0All Files (*.*)\0*.*\0");
                    if (!selectedPath.empty())
                    {
                        if (SceneSerializer::Load(selectedPath, entities, nextEntityId))
                        {
                            currentScenePath = selectedPath;
                            selectedEntityId = entities.empty() ? -1 : entities[0].id;
                            isSceneDirty = false;
                            g_Console.Log("Loaded scene: " + currentScenePath, ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                    break;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Saturn Build"))
            {
                if (ImGui::MenuItem("Export YAUL Build Files", nullptr, false, project.isLoaded))
                {
                    if (!project.isLoaded)
                    {
                        g_Console.Log("[Error] Open or Create a Project before exporting.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                    else if (entities.empty())
                    {
                        g_Console.Log("[Warning] Scene is empty. Add entities before building.", ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
                    }
                    else
                    {
                        if (ExportYaulSceneSource(project, entities))
                        {
                            g_Console.Log("Exported YAUL Build successfully to: " + project.GetBuildPath(), ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
                        }
                    }
                }

                // 1. APENAS COMPILA A ISO (Sem abrir emulador)
                if (ImGui::MenuItem("Compile ISO (make only)"))
                {
                    if (!project.isLoaded)
                    {
                        g_Console.Log("[Error] Open a Project first.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                    else
                    {
                        if (ExportYaulSceneSource(project, entities))
                        {
                            g_Console.Log("Exported YAUL Build successfully to: " + project.GetBuildPath(), ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
                        }
                        std::string cmd = g_Settings.makePath;
                        std::string buildDir = project.GetBuildPath();

                        ExecuteCommandAsync(cmd, buildDir, [](const std::string &line)
                                            {
                                ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                                if (line.find("error:") != std::string::npos || line.find("Error") != std::string::npos) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                                else if (line.find("warning:") != std::string::npos) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                                else if (line.find(".iso") != std::string::npos || line.find("Built") != std::string::npos) col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                                g_Console.Log(line, col); }, [](int exitCode)
                                            {
                                if (exitCode == 0) {
                                    g_Console.Log("[Success] ISO compiled successfully! (No emulator launched)", ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
                                } else {
                                    g_Console.Log("[Error] Compilation failed with code: " + std::to_string(exitCode), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                } });
                    }
                }

                // 2. COMPILA E EXECUTA NO EMULADOR (Build & Run)
                if (ImGui::MenuItem("Run in Emulator (Build & Run)", "F5"))
                {
                    if (!project.isLoaded)
                    {
                        g_Console.Log("[Error] Open a Project first.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                    }
                    else
                    {
                        ExportYaulSceneSource(project, entities);
                        std::string cmd = g_Settings.makePath;
                        std::string projName = project.projectName;
                        std::string buildDir = project.GetBuildPath();

                        ExecuteCommandAsync(cmd, buildDir, [](const std::string &line)
                                            {
                                ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                                if (line.find("error:") != std::string::npos) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                                else if (line.find("warning:") != std::string::npos) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                                g_Console.Log(line, col); }, [projName, buildDir](int exitCode)
                                            {
                                if (exitCode == 0) {
                                    std::string isoPath = buildDir + "/" + projName + ".iso";
                                    std::string cuePath = buildDir + "/" + projName + ".cue";
                                    std::string targetDisc = fs::exists(cuePath) ? cuePath : isoPath;
                                    if (LaunchEmulator(g_Settings.emulatorPath, targetDisc)) {
                                        g_Console.Log("[Emulator] Started running: " + targetDisc, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
                                    } else {
                                        g_Console.Log("[Error] Failed to start emulator. Check Preferences.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                    }
                                } else {
                                    g_Console.Log("[Abort] Run cancelled due to compilation errors.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                } });
                    }
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Preferences / Toolchain Paths..."))
                {
                    showPreferencesModal = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Modal para Criar Novo Projeto
        if (showNewProjectModal)
        {
            ImGui::OpenPopup("Create New Saturn Project");
        }
        if (ImGui::BeginPopupModal("Create New Saturn Project", &showNewProjectModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Configure seu novo projeto do Sega Saturn:");
            ImGui::Separator();
            ImGui::InputText("Project Name", newProjectName, sizeof(newProjectName));

            ImGui::InputText("Parent Directory", newProjectFolder, sizeof(newProjectFolder));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                std::string folder = ChooseFolderDialog("Selecione a pasta onde o projeto sera criado:");
                if (!folder.empty())
                {
                    std::strncpy(newProjectFolder, folder.c_str(), sizeof(newProjectFolder));
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Create Project", ImVec2(120, 0)))
            {
                if (strlen(newProjectName) > 0 && strlen(newProjectFolder) > 0)
                {
                    // Garante que o projeto fique dentro de uma pasta própria com seu nome
                    fs::path targetFolder = fs::path(newProjectFolder) / newProjectName;

                    assetMgr.Clear();
                    if (project.CreateNew(targetFolder.string(), newProjectName))
                    {
                        // 1. Gera cena com Main Camera
                        CreateDefaultScene(entities, nextEntityId, selectedEntityId);

                        // 2. Salva o Main.scn físico
                        std::string defaultSceneFile = project.projectRoot + "/" + project.startupScene;
                        SceneSerializer::Save(defaultSceneFile, entities);

                        currentScenePath = defaultSceneFile;
                        AddRecentProject(project.manifestFilePath);
                        SaveRecentProjects();

                        g_Console.Log("Project created: " + project.projectName + " at " + targetFolder.string(), ImVec4(0.4f, 0.8f, 1.0f, 1.0f));

                        // Fecha a janela modal
                        showNewProjectModal = false;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                showNewProjectModal = false;
            }
            ImGui::EndPopup();
        }

        // Modal: Preferências
        if (showPreferencesModal)
        {
            ImGui::OpenPopup("Saturn Studio - Preferences");
        }
        if (ImGui::BeginPopupModal("Saturn Studio - Preferences", &showPreferencesModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Configuracao dos Utilitarios de Build e Emulador:");
            ImGui::Separator();

            ImGui::InputText("Make Command / Executable", prefMakePath, sizeof(prefMakePath));
            ImGui::SameLine();
            if (ImGui::Button("Browse##Make"))
            {
                std::string p = OpenFileDialog("Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
                if (!p.empty())
                    strncpy(prefMakePath, p.c_str(), sizeof(prefMakePath));
            }

            ImGui::InputText("Saturn Emulator Executable", prefEmuPath, sizeof(prefEmuPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse##Emu"))
            {
                std::string p = OpenFileDialog("Emulator Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
                if (!p.empty())
                    strncpy(prefEmuPath, p.c_str(), sizeof(prefEmuPath));
            }

            ImGui::Separator();
            if (ImGui::Button("Save Preferences", ImVec2(150, 0)))
            {
                g_Settings.makePath = prefMakePath;
                g_Settings.emulatorPath = prefEmuPath;
                g_Settings.Save();
                g_Console.Log("[Settings] Preferences saved.", ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
                showPreferencesModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)))
            {
                showPreferencesModal = false;
            }
            ImGui::EndPopup();
        }

        // Modal: Confirmação de Mudanças Não Salvas
        if (showUnsavedChangesModal)
        {
            ImGui::OpenPopup("Unsaved Changes##Modal");
        }
        if (ImGui::BeginPopupModal("Unsaved Changes##Modal", &showUnsavedChangesModal, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("You have unsaved changes in the current scene.\nDo you want to save them before loading another scene?");
            ImGui::Separator();

            if (ImGui::Button("Save & Open", ImVec2(120, 0)))
            {
                if (!currentScenePath.empty())
                {
                    SceneSerializer::Save(currentScenePath, entities);
                }
                SceneSerializer::Load(pendingSceneToLoad, entities, nextEntityId);
                currentScenePath = pendingSceneToLoad;
                selectedEntityId = entities.empty() ? -1 : entities[0].id;
                isSceneDirty = false;
                showUnsavedChangesModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(100, 0)))
            {
                SceneSerializer::Load(pendingSceneToLoad, entities, nextEntityId);
                currentScenePath = pendingSceneToLoad;
                selectedEntityId = entities.empty() ? -1 : entities[0].id;
                isSceneDirty = false;
                showUnsavedChangesModal = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)))
            {
                showUnsavedChangesModal = false;
            }
            ImGui::EndPopup();
        }

        // Painel: Hierarchy
        ImGui::Begin("Hierarchy");
        if (!project.isLoaded)
        {
            ImGui::TextDisabled("Open or Create a project to view hierarchy.");
            ImGui::End();
        }
        else
        {
            if (ImGui::Button("+ Add Entity", ImVec2(-1, 0)))
            {
                Entity newEnt;
                newEnt.id = nextEntityId++;
                newEnt.name = "Entity_" + std::to_string(newEnt.id);
                newEnt.type = EntityType::VDP1_Quad;

                entities.push_back(newEnt);
                selectedEntityId = entities.back().id;
            }

            ImGui::Separator();

            if (ImGui::Selectable("Root Scene", selectedEntityId == -1))
            {
                selectedEntityId = -1;
            }

            if (ImGui::TreeNodeEx("Entities", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const auto &ent : entities)
                {
                    ImGui::PushID(ent.id);
                    bool isSelected = (selectedEntityId == ent.id);
                    std::string label = (ent.visible ? "[V] " : "[H] ") + ent.name;

                    if (ImGui::Selectable(label.c_str(), isSelected))
                    {
                        selectedEntityId = ent.id;
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::End();
        }

        // Painel: Viewport com Aspect Ratio Fixo (Letterboxing)
        ImGui::Begin("Scene / Saturn Screen (320x224)");
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 winPos = ImGui::GetCursorScreenPos();

            // Calcula proporção 320x224 fixa
            const float targetAspect = 320.0f / 224.0f;
            float viewW = avail.x;
            float viewH = viewW / targetAspect;

            if (viewH > avail.y)
            {
                viewH = avail.y;
                viewW = viewH * targetAspect;
            }

            // Centraliza a imagem no painel disponível
            ImVec2 offset = ImVec2((avail.x - viewW) * 0.5f, (avail.y - viewH) * 0.5f);
            ImVec2 viewPos = ImVec2(winPos.x + offset.x, winPos.y + offset.y);

            ImGui::SetCursorScreenPos(viewPos);
            ImGui::Image((ImTextureID)(intptr_t)saturnScreen.texture.id, ImVec2(viewW, viewH), ImVec2(0, 1), ImVec2(1, 0));
            bool isHovered = ImGui::IsItemHovered();

            ImVec2 mousePos = ImGui::GetMousePos();
            Vector2 viewMouse = {
                (mousePos.x - viewPos.x) / viewW * 320.0f,
                (mousePos.y - viewPos.y) / viewH * 224.0f};

            Ray ray = GetScreenToWorldRayEx(viewMouse, camera, 320, 224);

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                isDraggingGizmo = false;
                activeGizmoAxis = GizmoAxis::None;
            }

            if (isHovered)
            {
                float wheel = io.MouseWheel;
                if (wheel != 0.0f)
                {
                    Vector3 forward = Vector3Subtract(camera.target, camera.position);
                    float distance = Vector3Length(forward);
                    if (distance > 0.001f)
                    {
                        forward = Vector3Scale(forward, 1.0f / distance);
                        float step = wheel * 0.8f;
                        if (distance - step > 0.5f)
                        {
                            camera.position = Vector3Add(camera.position, Vector3Scale(forward, step));
                        }
                    }
                }

                Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
                Vector3 camUp = Vector3CrossProduct(right, forward);

                bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

                if (!isDraggingGizmo && activeEnt != nullptr && currentTool != EditorToolMode::Hand)
                {
                    Vector3 entCenter = {activeEnt->position[0], activeEnt->position[1], activeEnt->position[2]};
                    activeGizmoAxis = DetectGizmoHover(currentTool, entCenter, GIZMO_FIXED_SIZE, ray, shiftDown);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && activeGizmoAxis != GizmoAxis::None && currentTool != EditorToolMode::Hand)
                {
                    PushUndoState(entities);
                    isDraggingGizmo = true;
                    lastModifiedAxis = activeGizmoAxis;
                }

                if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && lastModifiedAxis != GizmoAxis::None && activeEnt != nullptr)
                {
                    isDraggingGizmo = true;
                    activeGizmoAxis = lastModifiedAxis;
                }

                if (!isDraggingGizmo)
                {
                    if ((currentTool == EditorToolMode::Hand && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
                    {
                        float dist = Vector3Distance(camera.position, camera.target);
                        float panSpeed = 0.0025f * dist;
                        Vector3 panOffset = Vector3Add(
                            Vector3Scale(right, -io.MouseDelta.x * panSpeed),
                            Vector3Scale(camUp, io.MouseDelta.y * panSpeed));
                        camera.position = Vector3Add(camera.position, panOffset);
                        camera.target = Vector3Add(camera.target, panOffset);
                    }

                    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                    {
                        float orbitSpeed = 0.005f;
                        Vector3 offset = Vector3Subtract(camera.position, camera.target);
                        offset = Vector3Transform(offset, MatrixRotateY(-io.MouseDelta.x * orbitSpeed));
                        offset = Vector3Transform(offset, MatrixRotate(right, -io.MouseDelta.y * orbitSpeed));
                        camera.position = Vector3Add(camera.target, offset);
                    }
                }

                if (isDraggingGizmo && activeEnt != nullptr)
                {
                    float dist = Vector3Distance(camera.position, camera.target);
                    float moveFactor = 0.003f * dist;

                    if (currentTool == EditorToolMode::Translate)
                    {
                        float dx = io.MouseDelta.x * moveFactor;
                        float dy = -io.MouseDelta.y * moveFactor;

                        if (activeGizmoAxis == GizmoAxis::X)
                        {
                            activeEnt->position[0] += (dx * right.x + dy * camUp.x);
                        }
                        else if (activeGizmoAxis == GizmoAxis::Y)
                        {
                            activeEnt->position[1] += dy;
                        }
                        else if (activeGizmoAxis == GizmoAxis::Z)
                        {
                            activeEnt->position[2] += (dx * right.z + dy * camUp.z);
                        }
                        else if (activeGizmoAxis == GizmoAxis::PlaneXZ)
                        {
                            activeEnt->position[0] += (dx * right.x + dy * camUp.x);
                            activeEnt->position[2] += (dx * right.z + dy * camUp.z);
                        }
                        else if (activeGizmoAxis == GizmoAxis::PlaneXY)
                        {
                            activeEnt->position[0] += (dx * right.x + dy * camUp.x);
                            activeEnt->position[1] += dy;
                        }
                        else if (activeGizmoAxis == GizmoAxis::PlaneYZ)
                        {
                            activeEnt->position[1] += dy;
                            activeEnt->position[2] += (dx * right.z + dy * camUp.z);
                        }
                        else if (activeGizmoAxis == GizmoAxis::ScreenSpace)
                        {
                            activeEnt->position[0] += (right.x * dx + camUp.x * dy);
                            activeEnt->position[1] += (right.y * dx + camUp.y * dy);
                            activeEnt->position[2] += (right.z * dx + camUp.z * dy);
                        }
                    }
                    else if (currentTool == EditorToolMode::Rotate)
                    {
                        float rotSpeed = 0.8f;
                        if (activeGizmoAxis == GizmoAxis::X)
                        {
                            activeEnt->rotation[0] += io.MouseDelta.y * rotSpeed;
                        }
                        else if (activeGizmoAxis == GizmoAxis::Y)
                        {
                            activeEnt->rotation[1] += io.MouseDelta.x * rotSpeed;
                        }
                        else if (activeGizmoAxis == GizmoAxis::Z)
                        {
                            activeEnt->rotation[2] += io.MouseDelta.x * rotSpeed;
                        }
                        else if (activeGizmoAxis == GizmoAxis::ScreenSpace)
                        {
                            activeEnt->rotation[2] += (io.MouseDelta.x - io.MouseDelta.y) * rotSpeed * 0.7f;
                        }
                    }
                    else if (currentTool == EditorToolMode::Scale)
                    {
                        float sDelta = (io.MouseDelta.x - io.MouseDelta.y) * 0.02f;
                        if (activeGizmoAxis == GizmoAxis::X)
                        {
                            activeEnt->scale[0] = fmaxf(0.05f, activeEnt->scale[0] + sDelta);
                        }
                        else if (activeGizmoAxis == GizmoAxis::Y)
                        {
                            activeEnt->scale[1] = fmaxf(0.05f, activeEnt->scale[1] + sDelta);
                        }
                        else if (activeGizmoAxis == GizmoAxis::Z)
                        {
                            activeEnt->scale[2] = fmaxf(0.05f, activeEnt->scale[2] + sDelta);
                        }
                        else if (activeGizmoAxis == GizmoAxis::Uniform)
                        {
                            activeEnt->scale[0] = fmaxf(0.05f, activeEnt->scale[0] + sDelta);
                            activeEnt->scale[1] = fmaxf(0.05f, activeEnt->scale[1] + sDelta);
                            activeEnt->scale[2] = fmaxf(0.05f, activeEnt->scale[2] + sDelta);
                        }
                    }
                }
            }

            // Toolbar Horizontal
            ImGui::SetCursorScreenPos(ImVec2(viewPos.x + 10, viewPos.y + 10));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.17f, 0.90f));

            ImGui::BeginChild("UnityToolbar", ImVec2(220, 42), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

            auto DrawToolButton = [&](const char *icon, EditorToolMode mode, const char *tooltip)
            {
                bool isSelected = (currentTool == mode);
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.44f, 0.72f, 1.0f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.22f, 0.25f, 0.6f));
                }

                if (ImGui::Button(icon, ImVec2(30, 26)))
                {
                    currentTool = mode;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", tooltip);
                }

                ImGui::PopStyleColor();
            };

            DrawToolButton("H", EditorToolMode::Hand, "Hand Tool (Pan)");
            ImGui::SameLine();
            DrawToolButton("T", EditorToolMode::Translate, "Move Tool (Translate)");
            ImGui::SameLine();
            DrawToolButton("R", EditorToolMode::Rotate, "Rotate Tool");
            ImGui::SameLine();
            DrawToolButton("S", EditorToolMode::Scale, "Scale Tool");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Botão para travar a visão na Câmera do Saturn
            if (ImGui::Button("2D", ImVec2(30, 26)))
            {
                Entity *camEnt = nullptr;
                for (auto &e : entities)
                {
                    if (e.type == EntityType::Camera)
                    {
                        camEnt = &e;
                        break;
                    }
                }

                if (camEnt != nullptr)
                {
                    // Alinha exatamente de frente (ortogonal) para a posição da câmera do jogo
                    camera.target = Vector3{camEnt->position[0], camEnt->position[1], camEnt->position[2]};
                    camera.position = Vector3{camEnt->position[0], camEnt->position[1], camEnt->position[2] + 19.3f};
                    camera.up = Vector3{0.0f, 1.0f, 0.0f};
                    g_Console.Log("Viewport aligned to Main Camera view (Saturn 320x224).", ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Align viewport directly to the Sega Saturn Camera");
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
        ImGui::End();

        // Painel: Inspector
        ImGui::Begin("Inspector");
        if (activeEnt != nullptr)
        {
            char nameBuf[64];
            std::strncpy(nameBuf, activeEnt->name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                activeEnt->name = nameBuf;
            }

            ImGui::Checkbox("Visible", &activeEnt->visible);

            int currentTypeIndex = static_cast<int>(activeEnt->type);
            const char *typeItems[] = {
                "VDP1 Quad (Distorted Sprite)",
                "VDP1 Normal Sprite",
                "VDP2 Scroll Plane",
                "Camera",
                "Empty Node"};

            if (ImGui::Combo("Entity Type", &currentTypeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
            {
                activeEnt->type = static_cast<EntityType>(currentTypeIndex);
            }

            ImGui::Separator();
            ImGui::Text("Transform");
            ImGui::DragFloat3("Position", activeEnt->position, 0.05f);
            ImGui::DragFloat3("Rotation", activeEnt->rotation, 1.0f);
            ImGui::DragFloat3("Scale", activeEnt->scale, 0.05f, 0.01f, 100.0f);

            ImGui::Separator();
            ImGui::Text("Saturn VDP1 Texture Component");

            std::vector<std::string> availTextures = {"(None)"};
            int selectedTexIdx = 0;

            if (project.isLoaded)
            {
                std::string texDir = project.GetTexturesPath();
                if (fs::exists(texDir))
                {
                    for (const auto &entry : fs::directory_iterator(texDir))
                    {
                        if (entry.is_regular_file())
                        {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".png" || ext == ".bmp")
                            {
                                std::string relPath = "assets/textures/" + entry.path().filename().string();
                                availTextures.push_back(relPath);
                                if (activeEnt->texturePath == relPath)
                                {
                                    selectedTexIdx = (int)availTextures.size() - 1;
                                }
                            }
                        }
                    }
                }
            }

            std::vector<const char *> comboItems;
            for (const auto &str : availTextures)
                comboItems.push_back(str.c_str());

            if (ImGui::Combo("Texture Asset", &selectedTexIdx, comboItems.data(), (int)comboItems.size()))
            {
                if (selectedTexIdx == 0)
                {
                    activeEnt->texturePath = "";
                    activeEnt->texWidth = 0;
                    activeEnt->texHeight = 0;
                }
                else
                {
                    activeEnt->texturePath = availTextures[selectedTexIdx];
                    Texture2D *loaded = assetMgr.GetTexture(project.projectRoot + "/" + activeEnt->texturePath);
                    if (loaded)
                    {
                        activeEnt->texWidth = loaded->width;
                        activeEnt->texHeight = loaded->height;
                    }
                }
            }

            if (!activeEnt->texturePath.empty())
            {
                ImGui::Text("Resolution: %dx%d px", activeEnt->texWidth, activeEnt->texHeight);

                if (activeEnt->texWidth % 8 != 0)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Warning: Width must be multiple of 8 for VDP1!");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "VDP1 Alignment: OK (Multiple of 8)");
                }

                const char *colorModes[] = {"RGB555 (16 bpp)", "256 Colors (8 bpp)", "16 Colors (4 bpp)"};
                ImGui::Combo("Color Mode", &activeEnt->colorMode, colorModes, 3);

                int vramBytes = 0;
                if (activeEnt->colorMode == 0)
                    vramBytes = activeEnt->texWidth * activeEnt->texHeight * 2;
                else if (activeEnt->colorMode == 1)
                    vramBytes = activeEnt->texWidth * activeEnt->texHeight;
                else
                    vramBytes = (activeEnt->texWidth * activeEnt->texHeight) / 2;

                ImGui::Text("VRAM Footprint: %.2f KB (%d bytes)", vramBytes / 1024.0f, vramBytes);
                ImGui::ProgressBar((float)vramBytes / (512.0f * 1024.0f), ImVec2(-1, 0), "VDP1 VRAM Share");
            }
            ImGui::Separator();
            ImGui::Text("Logic & Scripting (C Native)");

            std::vector<std::string> availScripts = {"(None)"};
            int selectedScriptIdx = 0;

            if (project.isLoaded)
            {
                std::string scriptsDir = project.GetScriptsPath();
                if (fs::exists(scriptsDir))
                {
                    for (const auto &entry : fs::directory_iterator(scriptsDir))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == ".c")
                        {
                            std::string stemName = entry.path().stem().string();
                            availScripts.push_back(stemName);
                            if (activeEnt->scriptName == stemName)
                            {
                                selectedScriptIdx = (int)availScripts.size() - 1;
                            }
                        }
                    }
                }
            }

            std::vector<const char *> scriptComboItems;
            for (const auto &s : availScripts)
                scriptComboItems.push_back(s.c_str());

            // --- LAYOUT RESPONSIVO ---
            float availWidth = ImGui::GetContentRegionAvail().x;
            const float btnWidth = 65.0f;
            const float minWidthForSameLine = 250.0f; // Largura mínima para caber lado a lado

            bool fitsSameLine = availWidth >= minWidthForSameLine;

            // Se couber lado a lado, ajusta a largura do combo para sobrar espaço para o botão
            if (fitsSameLine)
            {
                ImGui::SetNextItemWidth(availWidth - btnWidth - ImGui::GetStyle().ItemSpacing.x);
            }
            else
            {
                ImGui::SetNextItemWidth(-1.0f); // Ocupa a linha inteira
            }

            // Usamos ##BehaviorScript para esconder o texto da direita e dar espaço visual
            if (ImGui::Combo("##BehaviorScript", &selectedScriptIdx, scriptComboItems.data(), (int)scriptComboItems.size()))
            {
                if (selectedScriptIdx == 0)
                {
                    activeEnt->scriptName = "";
                }
                else
                {
                    activeEnt->scriptName = availScripts[selectedScriptIdx];
                }
                isSceneDirty = true;
            }

            // Se couber, fica na mesma linha; se não couber, quebra para baixo
            if (fitsSameLine)
            {
                ImGui::SameLine();
            }

            // Botão New (se quebrou a linha, expande para largura total; se está ao lado, usa btnWidth)
            ImVec2 actualBtnSize = fitsSameLine ? ImVec2(btnWidth, 0) : ImVec2(-1, 0);

            if (ImGui::Button("+ New", actualBtnSize))
            {
                std::string newScriptPath = project.GetScriptsPath() + "/player_controller.c";
                if (!fs::exists(newScriptPath))
                {
                    std::ofstream sc(newScriptPath);
                    if (sc.is_open())
                    {
                        sc << "/* Generated by Saturn Studio */\n";
                        sc << "#include \"scene_data.h\"\n";
                        sc << "#include <yaul.h>\n\n";
                        sc << "void player_controller_init(saturn_entity_t *self) {\n";
                        sc << "    (void)self;\n";
                        sc << "}\n\n";
                        sc << "void player_controller_update(saturn_entity_t *self, const smpc_peripheral_digital_t *pad) {\n";
                        sc << "    fix16_t speed = FIX16(0.15f);\n";
                        sc << "    if ((pad->pressed.raw & PERIPHERAL_DIGITAL_LEFT) != 0)  self->position.x -= speed;\n";
                        sc << "    if ((pad->pressed.raw & PERIPHERAL_DIGITAL_RIGHT) != 0) self->position.x += speed;\n";
                        sc << "    if ((pad->pressed.raw & PERIPHERAL_DIGITAL_UP) != 0)    self->position.y += speed;\n";
                        sc << "    if ((pad->pressed.raw & PERIPHERAL_DIGITAL_DOWN) != 0)  self->position.y -= speed;\n";
                        sc << "}\n";
                        sc.close();
                        activeEnt->scriptName = "player_controller";
                        isSceneDirty = true;
                        g_Console.Log("Created template script: player_controller.c", ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
                    }
                }
                else
                {
                    activeEnt->scriptName = "player_controller";
                    isSceneDirty = true;
                    g_Console.Log("Linked existing player_controller.c to entity.", ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Create/Assign player_controller.c");
            }

            if (activeEnt->type == EntityType::Camera)
            {
                ImGui::Separator();

                if (ImGui::CollapsingHeader("Camera Controller Settings", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Checkbox("Follow X", &activeEnt->camFollowX))
                        isSceneDirty = true;
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Follow Y", &activeEnt->camFollowY))
                        isSceneDirty = true;

                    if (ImGui::SliderFloat("Smoothness", &activeEnt->camSmoothness, 0.01f, 1.0f, "%.3f"))
                        isSceneDirty = true;

                    if (ImGui::DragFloat2("Deadzone (X, Y)", activeEnt->camDeadzone, 0.05f, 0.0f, 10.0f))
                        isSceneDirty = true;

                    if (ImGui::Checkbox("Use World Bounds", &activeEnt->camUseBounds))
                        isSceneDirty = true;
                    if (activeEnt->camUseBounds)
                    {
                        if (ImGui::DragFloat2("Min Bounds (X, Y)", activeEnt->camBoundsMin, 0.5f))
                            isSceneDirty = true;
                        if (ImGui::DragFloat2("Max Bounds (X, Y)", activeEnt->camBoundsMax, 0.5f))
                            isSceneDirty = true;
                    }
                    ImGui::Separator();
                    if (ImGui::ColorEdit3("Background Color", activeEnt->camClearColor))
                        isSceneDirty = true;
                }
            }

            if (activeEnt->type == EntityType::VDP2_Plane)
            {
                ImGui::Separator();
                if (ImGui::CollapsingHeader("VDP2 Background Plane Settings", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::DragFloat2("Parallax Factor (X, Y)", activeEnt->parallax, 0.05f, 0.0f, 2.0f, "%.2f"))
                        isSceneDirty = true;

                    ImGui::TextDisabled("1.0 = Velocidade normal | 0.5 = Metade da velocidade (distante)");
                    ImGui::Separator();

                    // --- 1. COMBO DE MAPAS TILED (.JSON) ---
                    std::vector<std::string> jsonMapFiles = {"(None - Procedural Grid)"};
                    std::string mapsDir = project.projectRoot + "/assets/maps";
                    fs::create_directories(mapsDir);

                    for (const auto &entry : fs::directory_iterator(mapsDir))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == ".json")
                        {
                            jsonMapFiles.push_back(entry.path().filename().string());
                        }
                    }

                    int currentMapIdx = 0;
                    if (!activeEnt->mapPath.empty())
                    {
                        std::string currentStem = fs::path(activeEnt->mapPath).stem().string();
                        for (size_t i = 1; i < jsonMapFiles.size(); ++i)
                        {
                            if (fs::path(jsonMapFiles[i]).stem().string() == currentStem)
                            {
                                currentMapIdx = (int)i;
                                break;
                            }
                        }
                    }

                    std::vector<const char *> mapComboItems;
                    for (const auto &m : jsonMapFiles)
                        mapComboItems.push_back(m.c_str());

                    if (ImGui::Combo("Tiled Map (.json)", &currentMapIdx, mapComboItems.data(), (int)mapComboItems.size()))
                    {
                        if (currentMapIdx == 0)
                        {
                            activeEnt->mapPath = "";
                        }
                        else
                        {
                            std::string jsonFilename = jsonMapFiles[currentMapIdx];
                            std::string baseStem = fs::path(jsonFilename).stem().string();
                            std::string jsonFull = mapsDir + "/" + jsonFilename;
                            std::string binFull = mapsDir + "/" + baseStem + ".bin";

                            ConvertTiledJsonToSaturnBin(jsonFull, binFull);
                            activeEnt->mapPath = "assets/maps/" + baseStem + ".bin";
                        }
                        isSceneDirty = true;
                    }

                    // --- 2. COMBO DE TILESETS (.PNG) ---
                    std::vector<std::string> pngTilesetFiles = {"(None - Default Palette/Pattern)"};
                    std::string tilesetsDir = project.projectRoot + "/assets/tilesets";
                    fs::create_directories(tilesetsDir);

                    for (const auto &entry : fs::directory_iterator(tilesetsDir))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == ".png")
                        {
                            pngTilesetFiles.push_back(entry.path().filename().string());
                        }
                    }

                    int currentTileIdx = 0;
                    if (!activeEnt->texturePath.empty())
                    {
                        std::string currentStem = fs::path(activeEnt->texturePath).stem().string();
                        if (currentStem.size() > 4 && currentStem.substr(currentStem.size() - 4) == ".cpd")
                            currentStem = currentStem.substr(0, currentStem.size() - 4);

                        for (size_t i = 1; i < pngTilesetFiles.size(); ++i)
                        {
                            if (fs::path(pngTilesetFiles[i]).stem().string() == currentStem)
                            {
                                currentTileIdx = (int)i;
                                break;
                            }
                        }
                    }

                    std::vector<const char *> tileComboItems;
                    for (const auto &t : pngTilesetFiles)
                        tileComboItems.push_back(t.c_str());

                    if (ImGui::Combo("Tileset Image (.png)", &currentTileIdx, tileComboItems.data(), (int)tileComboItems.size()))
                    {
                        if (currentTileIdx == 0)
                        {
                            activeEnt->texturePath = "";
                        }
                        else
                        {
                            std::string pngFilename = pngTilesetFiles[currentTileIdx];
                            std::string baseStem = fs::path(pngFilename).stem().string();
                            std::string pngFull = tilesetsDir + "/" + pngFilename;
                            std::string cpdFull = tilesetsDir + "/" + baseStem + ".cpd.bin";
                            std::string palFull = tilesetsDir + "/" + baseStem + ".pal.bin";

                            ConvertTilesetPngToSaturnBin(pngFull, cpdFull, palFull);
                            activeEnt->texturePath = "assets/tilesets/" + baseStem + ".cpd.bin";
                        }
                        isSceneDirty = true;
                    }

                    ImGui::Spacing();
                    // Botões para copiar arquivos externos direto para as pastas oficiais do projeto
                    if (ImGui::SmallButton("Copiar Map Tiled (.json) para o projeto..."))
                    {
                        std::string src = OpenFileDialog("Tiled JSON (*.json)\0*.json\0");
                        if (!src.empty())
                        {
                            std::string filename = fs::path(src).filename().string();
                            std::string dest = mapsDir + "/" + filename;
                            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);

                            std::string baseStem = fs::path(filename).stem().string();
                            ConvertTiledJsonToSaturnBin(dest, mapsDir + "/" + baseStem + ".bin");
                            activeEnt->mapPath = "assets/maps/" + baseStem + ".bin";
                            isSceneDirty = true;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copiar PNG para o projeto..."))
                    {
                        std::string src = OpenFileDialog("PNG Image (*.png)\0*.png\0");
                        if (!src.empty())
                        {
                            std::string filename = fs::path(src).filename().string();
                            std::string dest = tilesetsDir + "/" + filename;
                            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);

                            std::string baseStem = fs::path(filename).stem().string();
                            ConvertTilesetPngToSaturnBin(dest, tilesetsDir + "/" + baseStem + ".cpd.bin", tilesetsDir + "/" + baseStem + ".pal.bin");
                            activeEnt->texturePath = "assets/tilesets/" + baseStem + ".cpd.bin";
                            isSceneDirty = true;
                        }
                    }

                    if (!activeEnt->texturePath.empty())
                    {
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tileset Bin: %s", activeEnt->texturePath.c_str());
                    }

                    if (!activeEnt->mapPath.empty())
                    {
                        std::string fullPath = project.projectRoot + "/" + activeEnt->mapPath;
                        std::error_code ec;
                        if (fs::exists(fullPath, ec))
                        {
                            size_t fSize = fs::file_size(fullPath, ec);
                            ImGui::Text("Tamanho PND: %zu bytes (64x64 cells)", fSize);
                        }
                    }
                }

                ImGui::Separator();
                ImGui::Checkbox("Repeat / Wrap Map", &activeEnt->wrapRepeat);
                ImGui::Separator();

                ImGui::Checkbox("Tilemap Collider 2D", &activeEnt->tilemapColliderActive);
                if (activeEnt->tilemapColliderActive)
                {
                    ImGui::DragInt("Solid Tile Min", &activeEnt->solidTileMin, 1.0f, 1, 1024);
                    ImGui::DragInt("Solid Tile Max", &activeEnt->solidTileMax, 1.0f, 1, 1024);
                }
            }

            ImGui::Separator();
            ImGui::Text("Hardware Attributes");
            ImGui::ColorEdit3("Tint Color", activeEnt->color);
            ImGui::SliderInt("VDP1 Priority", &activeEnt->priority, 0, 7);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("2D Physics (AABB)");
            if (ImGui::Checkbox("Enable Box Collider", &activeEnt->hasCollider))
                isSceneDirty = true;

            if (activeEnt->hasCollider)
            {
                ImGui::SameLine();
                if (ImGui::Checkbox("Is Trigger", &activeEnt->isTrigger))
                    isSceneDirty = true;
            }

            if (activeEnt->hasCollider)
            {
                if (ImGui::DragFloat2("Size (W, H)", activeEnt->colliderSize, 0.05f, 0.1f, 100.0f))
                    isSceneDirty = true;
                if (ImGui::DragFloat2("Offset (X, Y)", activeEnt->colliderOffset, 0.05f))
                    isSceneDirty = true;
            }

            ImGui::Spacing();

            if (activeEnt->type != EntityType::Camera)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Button("Delete Entity", ImVec2(-1, 0)))
                {
                    for (auto it = entities.begin(); it != entities.end(); ++it)
                    {
                        if (it->id == activeEnt->id)
                        {
                            entities.erase(it);
                            selectedEntityId = -1;
                            break;
                        }
                    }
                }
                ImGui::PopStyleColor(2);
            }
            else
            {
                ImGui::TextDisabled("Main Camera cannot be deleted.");
            }
        }
        else
        {
            ImGui::TextDisabled("No entity selected.");
        }

        ImGui::End();

        // Painel: Project Files
        // Painel: Project Explorer (Dinâmico com Context Menu e Scripts)
        ImGui::Begin("Project");
        if (!project.isLoaded)
        {
            ImGui::TextDisabled("No project open.");
        }
        else
        {
            // Barra de Título / Informações do Projeto
            ImGui::Text("Project: %s", project.projectName.c_str());
            ImGui::TextDisabled("Path: %s", project.projectRoot.c_str());
            ImGui::SameLine();

            // Botão Atualizar (Refresh) no topo direito
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > 70.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 65.0f);
            }
            if (ImGui::Button("Refresh", ImVec2(60, 20)))
            {
                g_Console.Log("Project explorer refreshed.");
            }
            ImGui::Separator();

            // Função Lambda Recursiva para Desenhar Pastas e Arquivos de Forma Dinâmica
            std::function<void(const fs::path &)> DrawDirectoryTree = [&](const fs::path &currentDir)
            {
                if (!fs::exists(currentDir))
                    return;

                for (const auto &entry : fs::directory_iterator(currentDir))
                {
                    std::string filename = entry.path().filename().string();

                    // Se for Pasta (Directory)
                    if (entry.is_directory())
                    {
                        bool open = ImGui::TreeNodeEx(filename.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                        // Menu de Contexto na PASTA (Botão Direito)
                        std::string folderPopupId = "FolderCtx_" + entry.path().string();
                        if (ImGui::BeginPopupContextItem(folderPopupId.c_str()))
                        {
                            if (ImGui::MenuItem("+ New Subfolder"))
                            {
                                int subIdx = 1;
                                fs::path newFolder = entry.path() / ("NewFolder_" + std::to_string(subIdx));
                                while (fs::exists(newFolder))
                                {
                                    subIdx++;
                                    newFolder = entry.path() / ("NewFolder_" + std::to_string(subIdx));
                                }
                                fs::create_directories(newFolder);
                                g_Console.Log("Created folder: " + newFolder.filename().string());
                            }
                            if (ImGui::MenuItem("+ New C-Script (.c)"))
                            {
                                int scrIdx = 1;
                                fs::path newScript = entry.path() / ("custom_script_" + std::to_string(scrIdx) + ".c");
                                while (fs::exists(newScript))
                                {
                                    scrIdx++;
                                    newScript = entry.path() / ("custom_script_" + std::to_string(scrIdx) + ".c");
                                }
                                std::ofstream sc(newScript);
                                if (sc.is_open())
                                {
                                    sc << "/* Saturn Studio Script */\n#include \"scene_data.h\"\n#include <yaul.h>\n\n";
                                    sc << "void custom_script_" << scrIdx << "_init(saturn_entity_t *self) {\n    (void)self;\n}\n\n";
                                    sc << "void custom_script_" << scrIdx << "_update(saturn_entity_t *self, const smpc_peripheral_digital_t *pad) {\n    (void)self;\n    (void)pad;\n}\n";
                                    sc.close();
                                    g_Console.Log("Created script: " + newScript.filename().string(), ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                                }
                            }
                            if (ImGui::MenuItem("+ New Scene (.scn)"))
                            {
                                std::string scnPath = (entry.path() / "NewScene.scn").string();
                                CreateDefaultScene(entities, nextEntityId, selectedEntityId);
                                SceneSerializer::Save(scnPath, entities);
                                currentScenePath = scnPath;
                                g_Console.Log("Created new scene: " + scnPath, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Show in Explorer"))
                            {
                                std::string cmd = "explorer \"" + entry.path().string() + "\"";
                                system(cmd.c_str());
                            }
                            ImGui::EndPopup();
                        }

                        if (open)
                        {
                            DrawDirectoryTree(entry.path());
                            ImGui::TreePop();
                        }
                    }
                    // Se for Arquivo Regular
                    else if (entry.is_regular_file())
                    {
                        std::string ext = entry.path().extension().string();

                        // 1. Arquivos de Cena (.scn)
                        if (ext == ".scn")
                        {
                            std::string relPath = fs::relative(entry.path(), project.projectRoot).generic_string();
                            bool isStartup = (project.startupScene == relPath || project.startupScene == entry.path().string());
                            std::string label = (isStartup ? "[*] " : "    ") + filename;

                            bool isCurrentActive = (currentScenePath == entry.path().string());
                            if (ImGui::Selectable(label.c_str(), isCurrentActive))
                            {
                                if (currentScenePath != entry.path().string())
                                {
                                    if (isSceneDirty)
                                    {
                                        pendingSceneToLoad = entry.path().string();
                                        showUnsavedChangesModal = true;
                                    }
                                    else
                                    {
                                        if (SceneSerializer::Load(entry.path().string(), entities, nextEntityId))
                                        {
                                            currentScenePath = entry.path().string();
                                            selectedEntityId = entities.empty() ? -1 : entities[0].id;
                                            isSceneDirty = false;
                                            g_Console.Log("Loaded scene: " + filename);
                                        }
                                    }
                                }
                            }

                            // Menu de contexto na cena
                            std::string popupId = "SceneCtx_" + entry.path().string();
                            if (ImGui::BeginPopupContextItem(popupId.c_str()))
                            {
                                if (ImGui::MenuItem("Set as Startup Scene", nullptr, isStartup))
                                {
                                    project.startupScene = relPath;
                                    project.SaveManifest();
                                    g_Console.Log("Defined startup scene: " + relPath, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
                                }
                                ImGui::EndPopup();
                            }
                        }
                        // 2. Arquivos de Script C (.c / .h)
                        else if (ext == ".c" || ext == ".h")
                        {
                            std::string label = "  [C] " + filename;
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", label.c_str());

                            std::string scriptPopup = "ScriptCtx_" + entry.path().string();
                            if (ImGui::BeginPopupContextItem(scriptPopup.c_str()))
                            {
                                if (ImGui::MenuItem("Open with Default Editor"))
                                {
                                    std::string cmd = "start \"\" \"" + entry.path().string() + "\"";
                                    system(cmd.c_str());
                                }
                                ImGui::EndPopup();
                            }
                        }
                        // 3. Imagens / Texturas (.png / .bmp)
                        else if (ext == ".png" || ext == ".bmp")
                        {
                            std::string label = "  [T] " + filename;
                            ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "%s", label.c_str());
                        }
                        // 4. Outros arquivos
                        else
                        {
                            ImGui::TextDisabled("      %s", filename.c_str());
                        }
                    }
                }
            };

            // Inicia a árvore a partir da raiz de "assets"
            fs::path assetsRoot = project.GetAssetsPath();
            if (fs::exists(assetsRoot))
            {
                if (ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    DrawDirectoryTree(assetsRoot);
                    ImGui::TreePop();
                }
            }

            // Menu de Contexto na ÁREA VAZIA do painel (clique com botão direito em qualquer espaço vago)
            if (ImGui::BeginPopupContextWindow("ProjectWindowContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("+ Create Folder in Assets"))
                {
                    int fIdx = 1;
                    fs::path newDir = assetsRoot / ("Folder_" + std::to_string(fIdx));
                    while (fs::exists(newDir))
                    {
                        fIdx++;
                        newDir = assetsRoot / ("Folder_" + std::to_string(fIdx));
                    }
                    fs::create_directories(newDir);
                    g_Console.Log("Created folder: " + newDir.filename().string());
                }
                if (ImGui::MenuItem("Open in File Explorer"))
                {
                    std::string cmd = "explorer \"" + project.projectRoot + "\"";
                    system(cmd.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Refresh Assets"))
                {
                    g_Console.Log("Assets reloaded.");
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();

        // Painel: Console com Suporte a Cópia
        ImGui::Begin("Console");
        if (ImGui::Button("Clear"))
        {
            g_Console.Clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy All (Clipboard)"))
        {
            std::string fullText = "";
            {
                std::lock_guard<std::mutex> lock(g_Console.mtx);
                for (const auto &entry : g_Console.logs)
                {
                    fullText += entry.first + "\n";
                }
            }
            ImGui::SetClipboardText(fullText.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| Total entries: %d (Click on a line to copy it)", (int)g_Console.logs.size());
        ImGui::Separator();

        ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(g_Console.mtx);
            for (size_t i = 0; i < g_Console.logs.size(); ++i)
            {
                const auto &entry = g_Console.logs[i];
                ImGui::PushID((int)i);
                ImGui::PushStyleColor(ImGuiCol_Text, entry.second);

                if (ImGui::Selectable(entry.first.c_str(), false, ImGuiSelectableFlags_None))
                {
                    ImGui::SetClipboardText(entry.first.c_str());
                }

                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();

        rlImGuiEnd();
        EndDrawing();
    }

    assetMgr.Clear();
    UnloadRenderTexture(saturnScreen);
    rlImGuiShutdown();
    CloseWindow();

    return 0;
}