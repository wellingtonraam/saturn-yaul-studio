#include "scene_serializer.h"
#include <fstream>
#include <sstream>
#include <iomanip>

static std::string CleanToken(const std::string &s)
{
    std::string out;
    for (char c : s)
    {
        if (c != '\"' && c != ',' && c != ':' && c != '{' && c != '}' && c != '\r')
        {
            out += c;
        }
    }
    return out;
}

bool SceneSerializer::Save(const std::string &filepath, const std::vector<Entity> &entities)
{
    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << "# SEGA SATURN STUDIO SCENE FILE\n{\n";
    file << "  \"count\": " << entities.size() << ",\n";
    file << "  \"entities\": [\n";

    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto &ent = entities[i];
        file << "    {\n";
        file << "      \"id\": " << ent.id << ",\n";
        file << "      \"name\": \"" << ent.name << "\",\n";
        file << "      \"type\": " << static_cast<int>(ent.type) << ",\n";
        file << std::fixed << std::setprecision(4);
        file << "      \"pos_x\": " << ent.position[0] << ", \"pos_y\": " << ent.position[1] << ", \"pos_z\": " << ent.position[2] << ",\n";
        file << "      \"rot_x\": " << ent.rotation[0] << ", \"rot_y\": " << ent.rotation[1] << ", \"rot_z\": " << ent.rotation[2] << ",\n";
        file << "      \"scl_x\": " << ent.scale[0] << ", \"scl_y\": " << ent.scale[1] << ", \"scl_z\": " << ent.scale[2] << ",\n";
        file << "      \"col_r\": " << ent.color[0] << ", \"col_g\": " << ent.color[1] << ", \"col_b\": " << ent.color[2] << ",\n";
        file << "      \"priority\": " << ent.priority << ",\n";
        file << "      \"visible\": " << (ent.visible ? 1 : 0) << ",\n";
        file << "      \"texture\": \"" << ent.texturePath << "\",\n";
        file << "      \"color_mode\": " << ent.colorMode << ",\n";
        file << "      \"script\": \"" << ent.scriptName << "\",\n";
        file << "      \"has_collider\": " << (ent.hasCollider ? 1 : 0) << ",\n";
        file << "      \"is_trigger\": " << (ent.isTrigger ? 1 : 0) << ",\n";
        file << "      \"coll_w\": " << ent.colliderSize[0] << ", \"coll_h\": " << ent.colliderSize[1] << ",\n";
        file << "      \"coll_ox\": " << ent.colliderOffset[0] << ", \"coll_oy\": " << ent.colliderOffset[1] << ",\n";
        file << "      \"cam_follow_x\": " << (ent.camFollowX ? 1 : 0) << ", \"cam_follow_y\": " << (ent.camFollowY ? 1 : 0) << ",\n";
        file << "      \"cam_smooth\": " << ent.camSmoothness << ",\n";
        file << "      \"cam_use_bounds\": " << (ent.camUseBounds ? 1 : 0) << ",\n";
        file << "      \"cam_bmin_x\": " << ent.camBoundsMin[0] << ", \"cam_bmin_y\": " << ent.camBoundsMin[1] << ",\n";
        file << "      \"cam_bmax_x\": " << ent.camBoundsMax[0] << ", \"cam_bmax_y\": " << ent.camBoundsMax[1] << ",\n";
        file << "      \"cam_dz_x\": " << ent.camDeadzone[0] << ", \"cam_dz_y\": " << ent.camDeadzone[1] << ",\n";
        file << "      \"cam_bg_r\": " << ent.camClearColor[0] << ", \"cam_bg_g\": " << ent.camClearColor[1] << ", \"cam_bg_b\": " << ent.camClearColor[2] << ",\n";
        file << "      \"parallax_x\": " << ent.parallax[0] << ", \"parallax_y\": " << ent.parallax[1] << ",\n";
        file << "      \"vdp2_map_path\": \"" << ent.mapPath << "\"\n";
        file << "    }" << (i + 1 < entities.size() ? ",\n" : "\n");
    }

    file << "  ]\n}\n";
    file.close();
    return true;
}

bool SceneSerializer::Load(const std::string &filepath, std::vector<Entity> &outEntities, int &outNextId)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    outEntities.clear();
    std::string line;
    Entity current;
    bool insideEntity = false;
    int maxId = 0;

    while (std::getline(file, line))
    {
        if (line.find("{") != std::string::npos && line.find("\"entities\"") == std::string::npos)
        {
            insideEntity = true;
            current = Entity();
            // Reseta valores padrão do colisor
            current.hasCollider = false;
            current.isTrigger = false;
            current.colliderSize[0] = 1.0f;
            current.colliderSize[1] = 1.0f;
            current.colliderOffset[0] = 0.0f;
            current.colliderOffset[1] = 0.0f;
            current.camFollowX = true;
            current.camFollowY = true;
            current.camSmoothness = 0.125f;
            current.camUseBounds = false;
            current.camBoundsMin[0] = -50.0f;
            current.camBoundsMin[1] = -50.0f;
            current.camBoundsMax[0] = 50.0f;
            current.camBoundsMax[1] = 50.0f;
            current.camDeadzone[0] = 0.25f;
            current.camDeadzone[1] = 0.25f;
            current.camClearColor[0] = 0.1f;
            current.camClearColor[1] = 0.1f;
            current.camClearColor[2] = 0.2f;
            current.parallax[0] = 1.0f;
            current.parallax[1] = 1.0f;
            current.mapPath = "";
            continue;
        }

        if (line.find("}") != std::string::npos && insideEntity)
        {
            insideEntity = false;
            if (current.id > maxId)
                maxId = current.id;
            outEntities.push_back(current);
            continue;
        }

        if (!insideEntity)
            continue;

        std::stringstream ss(line);
        std::string key, val;
        while (ss >> key)
        {
            std::string cleanKey = CleanToken(key);
            if (cleanKey == "id" && (ss >> val))
                current.id = std::stoi(CleanToken(val));
            else if (cleanKey == "name" && (ss >> val))
                current.name = CleanToken(val);
            else if (cleanKey == "type" && (ss >> val))
                current.type = static_cast<EntityType>(std::stoi(CleanToken(val)));
            else if (cleanKey == "pos_x" && (ss >> val))
                current.position[0] = std::stof(CleanToken(val));
            else if (cleanKey == "pos_y" && (ss >> val))
                current.position[1] = std::stof(CleanToken(val));
            else if (cleanKey == "pos_z" && (ss >> val))
                current.position[2] = std::stof(CleanToken(val));
            else if (cleanKey == "rot_x" && (ss >> val))
                current.rotation[0] = std::stof(CleanToken(val));
            else if (cleanKey == "rot_y" && (ss >> val))
                current.rotation[1] = std::stof(CleanToken(val));
            else if (cleanKey == "rot_z" && (ss >> val))
                current.rotation[2] = std::stof(CleanToken(val));
            else if (cleanKey == "scl_x" && (ss >> val))
                current.scale[0] = std::stof(CleanToken(val));
            else if (cleanKey == "scl_y" && (ss >> val))
                current.scale[1] = std::stof(CleanToken(val));
            else if (cleanKey == "scl_z" && (ss >> val))
                current.scale[2] = std::stof(CleanToken(val));
            else if (cleanKey == "col_r" && (ss >> val))
                current.color[0] = std::stof(CleanToken(val));
            else if (cleanKey == "col_g" && (ss >> val))
                current.color[1] = std::stof(CleanToken(val));
            else if (cleanKey == "col_b" && (ss >> val))
                current.color[2] = std::stof(CleanToken(val));
            else if (cleanKey == "priority" && (ss >> val))
                current.priority = std::stoi(CleanToken(val));
            else if (cleanKey == "visible" && (ss >> val))
                current.visible = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "texture" && (ss >> val))
                current.texturePath = CleanToken(val);
            else if (cleanKey == "color_mode" && (ss >> val))
                current.colorMode = std::stoi(CleanToken(val));
            else if (cleanKey == "script" && (ss >> val))
                current.scriptName = CleanToken(val);
            else if (cleanKey == "has_collider" && (ss >> val))
                current.hasCollider = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "is_trigger" && (ss >> val))
                current.isTrigger = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "coll_w" && (ss >> val))
                current.colliderSize[0] = std::stof(CleanToken(val));
            else if (cleanKey == "coll_h" && (ss >> val))
                current.colliderSize[1] = std::stof(CleanToken(val));
            else if (cleanKey == "coll_ox" && (ss >> val))
                current.colliderOffset[0] = std::stof(CleanToken(val));
            else if (cleanKey == "coll_oy" && (ss >> val))
                current.colliderOffset[1] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_follow_x" && (ss >> val))
                current.camFollowX = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "cam_follow_y" && (ss >> val))
                current.camFollowY = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "cam_smooth" && (ss >> val))
                current.camSmoothness = std::stof(CleanToken(val));
            else if (cleanKey == "cam_use_bounds" && (ss >> val))
                current.camUseBounds = (std::stoi(CleanToken(val)) != 0);
            else if (cleanKey == "cam_bmin_x" && (ss >> val))
                current.camBoundsMin[0] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bmin_y" && (ss >> val))
                current.camBoundsMin[1] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bmax_x" && (ss >> val))
                current.camBoundsMax[0] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bmax_y" && (ss >> val))
                current.camBoundsMax[1] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_dz_x" && (ss >> val))
                current.camDeadzone[0] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_dz_y" && (ss >> val))
                current.camDeadzone[1] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bg_r" && (ss >> val))
                current.camClearColor[0] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bg_g" && (ss >> val))
                current.camClearColor[1] = std::stof(CleanToken(val));
            else if (cleanKey == "cam_bg_b" && (ss >> val))
                current.camClearColor[2] = std::stof(CleanToken(val));
            else if (cleanKey == "parallax_x" && (ss >> val))
                current.parallax[0] = std::stof(CleanToken(val));
            else if (cleanKey == "parallax_y" && (ss >> val))
                current.parallax[1] = std::stof(CleanToken(val));
            else if (cleanKey == "vdp2_map_path" && (ss >> val))
                current.mapPath = CleanToken(val);
        }
    }

    file.close();
    outNextId = maxId + 1;
    return true;
}