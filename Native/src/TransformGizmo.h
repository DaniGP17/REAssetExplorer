#ifndef REASSETEXPLORER_TRANSFORMGIZMO_H
#define REASSETEXPLORER_TRANSFORMGIZMO_H
#include <cstdint>
#include <vector>

#include "Renderer/RenderMath.h"
#include "Renderer/RenderTypes.h"

// Values match rae_viewport_set_transform_tool.
enum class GizmoMode : int32_t { Translate, Rotate, Scale };

enum class GizmoHandle : int32_t { None, X, Y, Z, XY, YZ, ZX, Center, Screen };

// The camera as the viewport projects it; pixels with y down.
struct GizmoView {
    float eye[3]{};
    float forward[3]{ 0, 0, -1 };
    float right[3]{ 1, 0, 0 };
    float up[3]{ 0, 1, 0 };
    float tanHalf = 1;
    float width = 1;
    float height = 1;

    bool Project(const float p[3], float& x, float& y) const;
    // From the eye through pixel (x, y).
    void Ray(float x, float y, float dir[3]) const;
    // World length of one pixel at p's depth.
    float MetersPerPixel(const float p[3]) const;
};

struct GizmoSnap {
    bool translate = false;
    bool rotate = false;
    bool scale = false;
    float translateStep = 0.1f;
    float rotateStep = 10.0f;  // degrees
    float scaleStep = 0.25f;
};

// Unreal's move / rotate / scale widget: drawn at a constant screen size, picked in screen space.
class TransformGizmo {
public:
    GizmoMode mode = GizmoMode::Translate;
    bool local = false;

    // axes: the active object's unit axes; scale always uses them, move and rotate when local.
    void SetFrame(const float pivot[3], const float axes[3][3]);
    GizmoHandle HitTest(const GizmoView& view, float x, float y) const;
    void Build(const GizmoView& view, GizmoHandle hover, std::vector<ViewerDebugVertex>& triangles) const;
    void BeginDrag(const GizmoView& view, GizmoHandle handle, float x, float y);
    // The world change since BeginDrag: world' = world * result.
    Mat4 Drag(const GizmoView& view, float x, float y, const GizmoSnap& snap);
    GizmoHandle Dragging() const { return drag; }
    void EndDrag() { drag = GizmoHandle::None; }

private:
    void Axes(float out[3][3]) const;

    float pivot[3]{};
    float objectAxes[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    GizmoHandle drag = GizmoHandle::None;
    float startMouse[2]{};
    float startPivot[3]{};
    float startAxes[3][3]{};
    float startParam = 0;
    float startHit[3]{};
    float rotateAxis[3]{};
    float sweepFrom[3]{};  // unit vector from the pivot to where a rotation began
    float screenDir[2]{};
    float screenLength = 1;
    float sweep = 0;  // radians, drawn as a sector while rotating
};

#endif
