#pragma once

#include "raylib.h"
#include "raymath.h"
#include "scene.h"

enum class EditorToolMode
{
    Hand,
    Translate,
    Rotate,
    Scale
};

enum class GizmoAxis
{
    None = 0,
    X,
    Y,
    Z,
    PlaneXY,
    PlaneXZ,
    PlaneYZ,
    Uniform,
    ScreenSpace
};

GizmoAxis DetectGizmoHover(EditorToolMode tool, Vector3 center, float size, Ray ray, bool shiftDown);
void DrawGizmo(EditorToolMode tool, const Entity &ent, Camera3D camera, GizmoAxis activeAxis, GizmoAxis lastAxis, float size);