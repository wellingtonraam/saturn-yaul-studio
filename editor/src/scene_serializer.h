#pragma once

#include "scene.h"
#include <string>
#include <vector>

class SceneSerializer
{
public:
    static bool Save(const std::string &filepath, const std::vector<Entity> &entities);
    static bool Load(const std::string &filepath, std::vector<Entity> &outEntities, int &outNextId);
};