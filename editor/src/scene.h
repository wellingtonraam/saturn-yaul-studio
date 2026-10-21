#pragma once

#include <string>
#include <vector>
#include <filesystem>

enum class EntityType
{
    VDP1_Quad,
    VDP1_Sprite,
    VDP2_Plane,
    Camera,
    Empty
};

const char *GetEntityTypeName(EntityType type);

struct Entity
{
    Entity() = default; // Permite instanciar Entity e empurrar para o vector sem conflito

    int id = 1;
    std::string name = "New_Entity";
    EntityType type = EntityType::VDP1_Quad;

    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};

    float color[3] = {1.0f, 1.0f, 1.0f};
    int priority = 0;
    bool visible = true;

    // Configurações de Câmera da Entidade
    bool camFollowX = true;
    bool camFollowY = true;
    float camSmoothness = 0.125f;
    bool camUseBounds = false;
    float camBoundsMin[2] = {-50.0f, -50.0f};
    float camBoundsMax[2] = {50.0f, 50.0f};
    float camDeadzone[2] = {0.25f, 0.25f};
    float camClearColor[3] = {0.1f, 0.1f, 0.2f};
    // Configurações do VDP2 Plane
    float parallax[2] = {1.0f, 1.0f};
    bool tilemapColliderActive = true;
    bool wrapRepeat = false;  // false = para nas bordas do mapa (512x512); true = repete infinito
    int solidTileMin = 1;     // ID mínimo sólido (padrão 1)
    int solidTileMax = 1024;  // ID máximo sólido (padrão: cobre todos > 0)
    
    std::string mapPath = "";

    std::string texturePath = "";
    int texWidth = 0;
    int texHeight = 0;
    int colorMode = 0;

    std::string scriptName = "";
    // --- Física 2D (AABB) ---
    bool hasCollider = false;
    bool isTrigger = false;
    float colliderSize[2] = {1.0f, 1.0f};
    float colliderOffset[2] = {0.0f, 0.0f};
};
