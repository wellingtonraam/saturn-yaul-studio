#include "scene.h"
#include <fstream>
#include <filesystem>

const char *GetEntityTypeName(EntityType type)
{
    switch (type)
    {
    case EntityType::VDP1_Quad:   return "VDP1 Quad (Distorted Sprite / Poly)";
    case EntityType::VDP1_Sprite: return "VDP1 Normal Sprite";
    case EntityType::VDP2_Plane:  return "VDP2 Background Plane";
    case EntityType::Camera:      return "Camera (Game View)";
    case EntityType::Empty:       return "Empty Node";
    default:                      return "Unknown";
    }
}