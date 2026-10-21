#include "gizmo.h"
#include "rlgl.h"
#include <cmath>

static float DistancePointToRay(Vector3 point, Ray ray)
{
    Vector3 v = Vector3Subtract(point, ray.position);
    float d = Vector3DotProduct(v, ray.direction);
    if (d < 0.0f)
        return Vector3Length(v);
    Vector3 proj = Vector3Add(ray.position, Vector3Scale(ray.direction, d));
    return Vector3Distance(point, proj);
}

static float DistanceSegmentToRay(Vector3 p1, Vector3 p2, Ray ray)
{
    const int samples = 8;
    float minDist = 999999.0f;
    for (int i = 0; i <= samples; ++i)
    {
        float t = (float)i / (float)samples;
        Vector3 pt = Vector3Lerp(p1, p2, t);
        float d = DistancePointToRay(pt, ray);
        if (d < minDist)
            minDist = d;
    }
    return minDist;
}

static float DistanceCircleToRay(Vector3 center, float radius, int normalAxis, Ray ray)
{
    const int segments = 24;
    float minDist = 999999.0f;
    for (int i = 0; i < segments; ++i)
    {
        float a1 = (float)i * (2.0f * PI / segments);
        Vector3 pt = center;
        if (normalAxis == 0)
        {
            pt.y += cosf(a1) * radius;
            pt.z += sinf(a1) * radius;
        }
        else if (normalAxis == 1)
        {
            pt.x += cosf(a1) * radius;
            pt.z += sinf(a1) * radius;
        }
        else
        {
            pt.x += cosf(a1) * radius;
            pt.y += sinf(a1) * radius;
        }
        float d = DistancePointToRay(pt, ray);
        if (d < minDist)
            minDist = d;
    }
    return minDist;
}

GizmoAxis DetectGizmoHover(EditorToolMode tool, Vector3 center, float size, Ray ray, bool shiftDown)
{
    const float threshold = 0.22f * size;

    if (tool == EditorToolMode::Translate)
    {
        if (shiftDown && DistancePointToRay(center, ray) < threshold * 0.9f)
        {
            return GizmoAxis::ScreenSpace;
        }

        float pSize = size * 0.35f;
        Vector3 xyCenter = Vector3Add(center, Vector3{pSize * 0.5f, pSize * 0.5f, 0.0f});
        Vector3 xzCenter = Vector3Add(center, Vector3{pSize * 0.5f, 0.0f, pSize * 0.5f});
        Vector3 yzCenter = Vector3Add(center, Vector3{0.0f, pSize * 0.5f, pSize * 0.5f});

        if (DistancePointToRay(xyCenter, ray) < threshold * 0.8f)
            return GizmoAxis::PlaneXY;
        if (DistancePointToRay(xzCenter, ray) < threshold * 0.8f)
            return GizmoAxis::PlaneXZ;
        if (DistancePointToRay(yzCenter, ray) < threshold * 0.8f)
            return GizmoAxis::PlaneYZ;

        float dX = DistanceSegmentToRay(center, Vector3Add(center, Vector3{size, 0.0f, 0.0f}), ray);
        float dY = DistanceSegmentToRay(center, Vector3Add(center, Vector3{0.0f, size, 0.0f}), ray);
        float dZ = DistanceSegmentToRay(center, Vector3Add(center, Vector3{0.0f, 0.0f, size}), ray);

        float minD = dX;
        GizmoAxis axis = GizmoAxis::X;
        if (dY < minD) { minD = dY; axis = GizmoAxis::Y; }
        if (dZ < minD) { minD = dZ; axis = GizmoAxis::Z; }

        if (minD < threshold)
            return axis;
    }
    else if (tool == EditorToolMode::Rotate)
    {
        float dScreen = DistancePointToRay(center, ray);
        if (fabsf(dScreen - (size * 1.15f)) < threshold * 0.5f)
            return GizmoAxis::ScreenSpace;

        float dX = DistanceCircleToRay(center, size, 0, ray);
        float dY = DistanceCircleToRay(center, size, 1, ray);
        float dZ = DistanceCircleToRay(center, size, 2, ray);

        float minD = dX;
        GizmoAxis axis = GizmoAxis::X;
        if (dY < minD) { minD = dY; axis = GizmoAxis::Y; }
        if (dZ < minD) { minD = dZ; axis = GizmoAxis::Z; }

        if (minD < threshold)
            return axis;
    }
    else if (tool == EditorToolMode::Scale)
    {
        if (DistancePointToRay(center, ray) < threshold * 0.9f)
            return GizmoAxis::Uniform;

        float dX = DistanceSegmentToRay(center, Vector3Add(center, Vector3{size, 0.0f, 0.0f}), ray);
        float dY = DistanceSegmentToRay(center, Vector3Add(center, Vector3{0.0f, size, 0.0f}), ray);
        float dZ = DistanceSegmentToRay(center, Vector3Add(center, Vector3{0.0f, 0.0f, size}), ray);

        float minD = dX;
        GizmoAxis axis = GizmoAxis::X;
        if (dY < minD) { minD = dY; axis = GizmoAxis::Y; }
        if (dZ < minD) { minD = dZ; axis = GizmoAxis::Z; }

        if (minD < threshold)
            return axis;
    }

    return GizmoAxis::None;
}

static void DrawScreenArrow(Vector2 from, Vector2 to, float headSize, Color color)
{
    DrawLineEx(from, to, 2.5f, color);
    Vector2 dir = Vector2Normalize(Vector2Subtract(to, from));
    Vector2 perp = {-dir.y, dir.x};

    Vector2 p1 = to;
    Vector2 p2 = Vector2Add(Vector2Subtract(to, Vector2Scale(dir, headSize)), Vector2Scale(perp, headSize * 0.45f));
    Vector2 p3 = Vector2Subtract(Vector2Subtract(to, Vector2Scale(dir, headSize)), Vector2Scale(perp, headSize * 0.45f));

    DrawTriangle(p1, p3, p2, color);
}

static void DrawScreenBox(Vector2 center, float size, Color color)
{
    ::Rectangle r = {center.x - size * 0.5f, center.y - size * 0.5f, size, size};
    DrawRectangleRec(r, color);
    DrawRectangleLinesEx(r, 1.0f, ColorBrightness(color, -0.3f));
}

void DrawGizmo(EditorToolMode tool, const Entity &ent, Camera3D camera, GizmoAxis activeAxis, GizmoAxis lastAxis, float size)
{
    rlDrawRenderBatchActive();

    Vector3 entCenter = {ent.position[0], ent.position[1], ent.position[2]};
    GizmoAxis displayAxis = (activeAxis != GizmoAxis::None) ? activeAxis : lastAxis;

    Vector2 c  = GetWorldToScreenEx(entCenter, camera, 320, 224);
    Vector2 xA = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{size, 0.0f, 0.0f}), camera, 320, 224);
    Vector2 yA = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{0.0f, size, 0.0f}), camera, 320, 224);
    Vector2 zA = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{0.0f, 0.0f, size}), camera, 320, 224);

    Color colX = (displayAxis == GizmoAxis::X) ? YELLOW : RED;
    Color colY = (displayAxis == GizmoAxis::Y) ? YELLOW : GREEN;
    Color colZ = (displayAxis == GizmoAxis::Z) ? YELLOW : BLUE;
    Color colU = (displayAxis == GizmoAxis::Uniform) ? YELLOW : LIGHTGRAY;

    bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (tool == EditorToolMode::Translate)
    {
        float pDist = size * 0.35f;
        Vector2 xyP = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{pDist, pDist, 0.0f}), camera, 320, 224);
        Vector2 xzP = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{pDist, 0.0f, pDist}), camera, 320, 224);
        Vector2 yzP = GetWorldToScreenEx(Vector3Add(entCenter, Vector3{0.0f, pDist, pDist}), camera, 320, 224);

        Color colXY = (displayAxis == GizmoAxis::PlaneXY) ? YELLOW : Color{0, 0, 255, 160};
        Color colXZ = (displayAxis == GizmoAxis::PlaneXZ) ? YELLOW : Color{0, 255, 0, 160};
        Color colYZ = (displayAxis == GizmoAxis::PlaneYZ) ? YELLOW : Color{255, 0, 0, 160};

        DrawLineEx(c, xyP, 1.5f, colXY);
        DrawLineEx(c, xzP, 1.5f, colXZ);
        DrawLineEx(c, yzP, 1.5f, colYZ);

        DrawScreenBox(xyP, 6.0f, colXY);
        DrawScreenBox(xzP, 6.0f, colXZ);
        DrawScreenBox(yzP, 6.0f, colYZ);

        DrawScreenArrow(c, xA, 10.0f, colX);
        DrawScreenArrow(c, yA, 10.0f, colY);
        DrawScreenArrow(c, zA, 10.0f, colZ);

        if (shiftDown)
        {
            Color colCenter = (displayAxis == GizmoAxis::ScreenSpace) ? YELLOW : LIGHTGRAY;
            DrawScreenBox(c, 10.0f, colCenter);
        }
        else
        {
            DrawCircleV(c, 3.5f, WHITE);
        }
    }
    else if (tool == EditorToolMode::Scale)
    {
        DrawLineEx(c, xA, 2.5f, colX);
        DrawLineEx(c, yA, 2.5f, colY);
        DrawLineEx(c, zA, 2.5f, colZ);

        DrawScreenBox(xA, 8.0f, colX);
        DrawScreenBox(yA, 8.0f, colY);
        DrawScreenBox(zA, 8.0f, colZ);
        DrawScreenBox(c, 9.0f, colU);
    }
    else if (tool == EditorToolMode::Rotate)
    {
        const int segs = 36;
        for (int axis = 0; axis < 3; axis++)
        {
            Color col = (axis == 0) ? colX : (axis == 1) ? colY : colZ;
            Vector2 prev = {0};
            for (int i = 0; i <= segs; i++)
            {
                float a = (float)i * (2.0f * PI / segs);
                Vector3 p = entCenter;
                if (axis == 0) { p.y += cosf(a) * size; p.z += sinf(a) * size; }
                if (axis == 1) { p.x += cosf(a) * size; p.z += sinf(a) * size; }
                if (axis == 2) { p.x += cosf(a) * size; p.y += sinf(a) * size; }

                Vector2 s = GetWorldToScreenEx(p, camera, 320, 224);
                if (i > 0)
                    DrawLineEx(prev, s, 2.0f, col);
                prev = s;
            }
        }
        Color colScreen = (displayAxis == GizmoAxis::ScreenSpace) ? YELLOW : LIGHTGRAY;
        float rScreen = Vector2Distance(c, xA) * 1.15f;
        DrawCircleLines((int)c.x, (int)c.y, rScreen, colScreen);
        DrawCircleV(c, 3.5f, WHITE);
    }
}