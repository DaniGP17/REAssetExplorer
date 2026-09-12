#include "TransformGizmo.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};

V3 Make(const float p[3]) { return { p[0], p[1], p[2] }; }
V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Length(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Unit(V3 a) {
    float length = Length(a);
    return length > 1e-12f ? a * (1.0f / length) : V3{ 1, 0, 0 };
}
// Rodrigues: v turned by angle about the unit axis k.
V3 Rotate(V3 v, V3 k, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + Cross(k, v) * s + k * (Dot(k, v) * (1 - c));
}

constexpr float AXIS_PX = 110;
constexpr float SHAFT_PX = 3;
constexpr float HEAD_LENGTH_PX = 22;
constexpr float HEAD_RADIUS_PX = 6.5f;
constexpr float PLANE_PX = 34;
constexpr float CENTER_PX = 7;
constexpr float RING_PX = 100;
constexpr float SCREEN_RING_PX = 118;
constexpr float RING_WIDTH_PX = 3;
constexpr float SCALE_BOX_PX = 5.5f;
constexpr float SCALE_CENTER_PX = 7;
constexpr float SCALE_PLANE_PX = 60;
constexpr float HIT_PX = 8;
constexpr int RING_SEGMENTS = 72;
constexpr int CONE_SEGMENTS = 16;
constexpr float BACK_ALPHA = 0.3f;
constexpr float PI = 3.14159265f;

// Unreal's axis colours; RGBA8 with R in the low byte.
constexpr uint32_t AXIS_COLORS[3] = { 0xFF0025CAu, 0xFF00A967u, 0xFFED7E2Cu };
constexpr uint32_t HOVER_COLOR = 0xFF00FFFFu;
constexpr uint32_t CENTER_COLOR = 0xFFE6E6E6u;
constexpr uint32_t SCREEN_RING_COLOR = 0xFFC8C8C8u;

uint32_t WithAlpha(uint32_t color, float alpha) {
    return (color & 0x00FFFFFFu) | static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24;
}

uint32_t Shade(uint32_t color, float factor) {
    uint32_t out = color & 0xFF000000u;
    for (int c = 0; c < 3; c++) {
        float channel = static_cast<float>((color >> (8 * c)) & 255) * factor;
        out |= static_cast<uint32_t>(std::clamp(channel, 0.0f, 255.0f)) << (8 * c);
    }
    return out;
}

struct Plane {
    GizmoHandle handle;
    int a;
    int b;
    int normal;
};
constexpr Plane HANDLE_PLANES[3] = { { GizmoHandle::XY, 0, 1, 2 }, { GizmoHandle::YZ, 1, 2, 0 }, { GizmoHandle::ZX, 2, 0, 1 } };

int AxisOf(GizmoHandle handle) {
    return handle == GizmoHandle::X ? 0 : handle == GizmoHandle::Y ? 1 : handle == GizmoHandle::Z ? 2 : -1;
}

const Plane* PlaneOf(GizmoHandle handle) {
    for (const Plane& plane : HANDLE_PLANES) {
        if (plane.handle == handle) return &plane;
    }
    return nullptr;
}

float SegmentDistance(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float length2 = dx * dx + dy * dy;
    float t = length2 > 1e-6f ? std::clamp(((px - ax) * dx + (py - ay) * dy) / length2, 0.0f, 1.0f) : 0.0f;
    float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return std::sqrt(cx * cx + cy * cy);
}

// Parameter along the line o + d t (d unit) closest to the ray e + r s (r unit).
float ClosestParam(V3 o, V3 d, V3 e, V3 r) {
    V3 w = o - e;
    float b = Dot(d, r);
    float denom = 1 - b * b;
    if (denom < 1e-6f) return 0;
    return (b * Dot(r, w) - Dot(d, w)) / denom;
}

bool RayPlane(V3 e, V3 r, V3 p, V3 n, V3& hit) {
    float denom = Dot(r, n);
    if (std::fabs(denom) < 1e-6f) return false;
    hit = e + r * (Dot(p - e, n) / denom);
    return true;
}

float Snap(float value, float step) {
    return step > 0 ? std::round(value / step) * step : value;
}

Mat4 AboutPivot(const Mat4& linear, V3 pivot) {
    float to[3] = { -pivot.x, -pivot.y, -pivot.z };
    float back[3] = { pivot.x, pivot.y, pivot.z };
    return Mul(Mul(Translation(to), linear), Translation(back));
}

class Mesh {
public:
    Mesh(std::vector<ViewerDebugVertex>& out, V3 eye) : out(out), eye(eye) {}

    void Triangle(V3 a, V3 b, V3 c, uint32_t color) {
        for (V3 p : { a, b, c }) out.push_back({ { p.x, p.y, p.z }, color });
    }

    void Quad(V3 a, V3 b, V3 c, V3 d, uint32_t color) {
        Triangle(a, b, c, color);
        Triangle(a, c, d, color);
    }

    // A camera-facing strip, width in world units.
    void Line(V3 a, V3 b, float width, uint32_t color) {
        V3 side = Cross(b - a, eye - (a + b) * 0.5f);
        if (Length(side) < 1e-12f) return;
        side = Unit(side) * (width * 0.5f);
        Quad(a - side, a + side, b + side, b - side, color);
    }

    void Cone(V3 base, V3 dir, float length, float radius, uint32_t color) {
        V3 u = Unit(Cross(dir, std::fabs(dir.y) < 0.9f ? V3{ 0, 1, 0 } : V3{ 1, 0, 0 }));
        V3 v = Cross(dir, u);
        V3 tip = base + dir * length;
        for (int k = 0; k < CONE_SEGMENTS; k++) {
            float a0 = 2 * PI * static_cast<float>(k) / CONE_SEGMENTS;
            float a1 = 2 * PI * static_cast<float>(k + 1) / CONE_SEGMENTS;
            V3 p0 = base + (u * std::cos(a0) + v * std::sin(a0)) * radius;
            V3 p1 = base + (u * std::cos(a1) + v * std::sin(a1)) * radius;
            V3 normal = Unit(Cross(p1 - p0, tip - p0));
            Triangle(p0, p1, tip, Shade(color, Lit(normal, (p0 + p1 + tip) * (1.0f / 3))));
            Triangle(p1, p0, base, Shade(color, 0.6f));
        }
    }

    void Box(V3 center, const float axes[3][3], float half, uint32_t color) {
        for (int f = 0; f < 3; f++) {
            V3 n = Make(axes[f]);
            V3 s = Make(axes[(f + 1) % 3]) * half;
            V3 t = Make(axes[(f + 2) % 3]) * half;
            for (float sign : { 1.0f, -1.0f }) {
                V3 c = center + n * (half * sign);
                Quad(c - s - t, c + s - t, c + s + t, c - s + t, Shade(color, Lit(n, c)));
            }
        }
    }

    void Disk(V3 center, V3 right, V3 up, float radius, uint32_t color) {
        for (int k = 0; k < 24; k++) {
            float a0 = 2 * PI * static_cast<float>(k) / 24;
            float a1 = 2 * PI * static_cast<float>(k + 1) / 24;
            Triangle(center, center + (right * std::cos(a0) + up * std::sin(a0)) * radius,
                     center + (right * std::cos(a1) + up * std::sin(a1)) * radius, color);
        }
    }

    // fadeBack dims the half turned away from the eye.
    void Ring(V3 center, V3 u, V3 v, float radius, float width, uint32_t color, bool fadeBack) {
        V3 toEye = Unit(eye - center);
        for (int k = 0; k < RING_SEGMENTS; k++) {
            float a0 = 2 * PI * static_cast<float>(k) / RING_SEGMENTS;
            float a1 = 2 * PI * static_cast<float>(k + 1) / RING_SEGMENTS;
            V3 d0 = u * std::cos(a0) + v * std::sin(a0);
            V3 d1 = u * std::cos(a1) + v * std::sin(a1);
            bool back = fadeBack && Dot(d0 + d1, toEye) < -0.05f;
            Line(center + d0 * radius, center + d1 * radius, width, back ? WithAlpha(color, BACK_ALPHA) : color);
        }
    }

private:
    float Lit(V3 normal, V3 at) const { return 0.55f + 0.45f * std::fabs(Dot(normal, Unit(eye - at))); }

    std::vector<ViewerDebugVertex>& out;
    V3 eye;
};

}

bool GizmoView::Project(const float p[3], float& x, float& y) const {
    V3 rel = Make(p) - Make(eye);
    float depth = Dot(rel, Make(forward));
    if (depth <= 1e-4f) return false;
    float aspect = width / height;
    float nx = Dot(rel, Make(right)) / (depth * tanHalf * aspect);
    float ny = Dot(rel, Make(up)) / (depth * tanHalf);
    x = (nx + 1) * 0.5f * width;
    y = (1 - ny) * 0.5f * height;
    return true;
}

void GizmoView::Ray(float x, float y, float dir[3]) const {
    float aspect = width / height;
    float nx = (2 * x / width - 1) * tanHalf * aspect;
    float ny = (1 - 2 * y / height) * tanHalf;
    V3 d = Unit(Make(forward) + Make(right) * nx + Make(up) * ny);
    dir[0] = d.x;
    dir[1] = d.y;
    dir[2] = d.z;
}

float GizmoView::MetersPerPixel(const float p[3]) const {
    float depth = std::max(Dot(Make(p) - Make(eye), Make(forward)), 1e-3f);
    return 2 * depth * tanHalf / height;
}

void TransformGizmo::SetFrame(const float newPivot[3], const float axes[3][3]) {
    std::copy(newPivot, newPivot + 3, pivot);
    for (int i = 0; i < 3; i++) std::copy(axes[i], axes[i] + 3, objectAxes[i]);
}

void TransformGizmo::Axes(float out[3][3]) const {
    bool useObject = mode == GizmoMode::Scale || local;
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) out[i][c] = useObject ? objectAxes[i][c] : (i == c ? 1.0f : 0.0f);
    }
}

GizmoHandle TransformGizmo::HitTest(const GizmoView& view, float x, float y) const {
    float cx, cy;
    if (!view.Project(pivot, cx, cy)) return GizmoHandle::None;
    float mpp = view.MetersPerPixel(pivot);
    float axes[3][3];
    Axes(axes);
    V3 p = Make(pivot);
    auto screen = [&](V3 w, float& sx, float& sy) {
        float point[3] = { w.x, w.y, w.z };
        return view.Project(point, sx, sy);
    };
    auto nearest = [&](float px, float py) { return std::hypot(px - x, py - y); };

    if (mode == GizmoMode::Rotate) {
        GizmoHandle best = GizmoHandle::None;
        float bestDistance = HIT_PX;
        V3 toEye = Unit(Make(view.eye) - p);
        auto ring = [&](GizmoHandle handle, V3 u, V3 v, float radiusPx, bool frontOnly) {
            float previous[2];
            bool valid = false;
            for (int k = 0; k <= RING_SEGMENTS; k++) {
                float a = 2 * PI * static_cast<float>(k) / RING_SEGMENTS;
                V3 d = u * std::cos(a) + v * std::sin(a);
                float sx, sy;
                bool on = screen(p + d * (radiusPx * mpp), sx, sy) && (!frontOnly || Dot(d, toEye) >= -0.05f);
                if (on && valid) {
                    float distance = SegmentDistance(x, y, previous[0], previous[1], sx, sy);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        best = handle;
                    }
                }
                previous[0] = sx;
                previous[1] = sy;
                valid = on;
            }
        };
        const GizmoHandle rings[3] = { GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z };
        for (int i = 0; i < 3; i++) ring(rings[i], Make(axes[(i + 1) % 3]), Make(axes[(i + 2) % 3]), RING_PX, true);
        ring(GizmoHandle::Screen, Make(view.right), Make(view.up), SCREEN_RING_PX, false);
        return best;
    }

    float centerRadius = (mode == GizmoMode::Scale ? SCALE_CENTER_PX : CENTER_PX) + 4;
    if (nearest(cx, cy) <= centerRadius) return GizmoHandle::Center;

    GizmoHandle best = GizmoHandle::None;
    float bestDistance = HIT_PX;
    const GizmoHandle handles[3] = { GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z };
    for (int i = 0; i < 3; i++) {
        float ax, ay, bx, by;
        if (!screen(p + Make(axes[i]) * (CENTER_PX * mpp), ax, ay) || !screen(p + Make(axes[i]) * (AXIS_PX * mpp), bx, by)) continue;
        float distance = SegmentDistance(x, y, ax, ay, bx, by);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = handles[i];
        }
    }
    if (best != GizmoHandle::None) return best;

    for (const Plane& plane : HANDLE_PLANES) {
        V3 a = Make(axes[plane.a]);
        V3 b = Make(axes[plane.b]);
        if (mode == GizmoMode::Scale) {
            float ax, ay, bx, by;
            float s = SCALE_PLANE_PX * mpp;
            if (!screen(p + a * s, ax, ay) || !screen(p + b * s, bx, by)) continue;
            if (SegmentDistance(x, y, ax, ay, bx, by) <= HIT_PX) return plane.handle;
            continue;
        }
        float s = PLANE_PX * mpp;
        V3 corners[4] = { p, p + a * s, p + a * s + b * s, p + b * s };
        float sx[4], sy[4];
        bool visible = true;
        for (int k = 0; k < 4; k++) visible = visible && screen(corners[k], sx[k], sy[k]);
        if (!visible) continue;
        float sign = 0;
        bool inside = true;
        for (int k = 0; k < 4 && inside; k++) {
            int n = (k + 1) % 4;
            float cross = (sx[n] - sx[k]) * (y - sy[k]) - (sy[n] - sy[k]) * (x - sx[k]);
            if (sign == 0) sign = cross;
            else if (cross * sign < 0) inside = false;
        }
        if (inside) return plane.handle;
    }
    return GizmoHandle::None;
}

void TransformGizmo::Build(const GizmoView& view, GizmoHandle hover, std::vector<ViewerDebugVertex>& triangles) const {
    float cx, cy;
    if (!view.Project(pivot, cx, cy)) return;
    float mpp = view.MetersPerPixel(pivot);
    float axes[3][3];
    Axes(axes);
    V3 p = Make(pivot);
    Mesh mesh(triangles, Make(view.eye));
    auto lit = [&](GizmoHandle handle) { return drag == handle || (drag == GizmoHandle::None && hover == handle); };
    const GizmoHandle handles[3] = { GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z };

    if (mode == GizmoMode::Rotate) {
        for (int i = 0; i < 3; i++) {
            uint32_t color = lit(handles[i]) ? HOVER_COLOR : AXIS_COLORS[i];
            mesh.Ring(p, Make(axes[(i + 1) % 3]), Make(axes[(i + 2) % 3]), RING_PX * mpp, RING_WIDTH_PX * mpp, color, true);
        }
        mesh.Ring(p, Make(view.right), Make(view.up), SCREEN_RING_PX * mpp, RING_WIDTH_PX * mpp,
                  lit(GizmoHandle::Screen) ? HOVER_COLOR : SCREEN_RING_COLOR, false);
        if (drag != GizmoHandle::None && sweep != 0) {
            int axis = AxisOf(drag);
            uint32_t fill = WithAlpha(axis >= 0 ? AXIS_COLORS[axis] : SCREEN_RING_COLOR, 0.35f);
            float radius = (drag == GizmoHandle::Screen ? SCREEN_RING_PX : RING_PX) * mpp;
            int steps = std::max(2, static_cast<int>(std::fabs(sweep) / (2 * PI) * RING_SEGMENTS) + 1);
            V3 k = Make(rotateAxis);
            V3 from = Make(sweepFrom);
            for (int s = 0; s < steps; s++) {
                V3 a = Rotate(from, k, sweep * static_cast<float>(s) / steps);
                V3 b = Rotate(from, k, sweep * static_cast<float>(s + 1) / steps);
                mesh.Triangle(p, p + a * radius, p + b * radius, fill);
            }
            mesh.Line(p, p + from * radius, RING_WIDTH_PX * mpp * 0.7f, WithAlpha(HOVER_COLOR, 0.8f));
            mesh.Line(p, p + Rotate(from, k, sweep) * radius, RING_WIDTH_PX * mpp * 0.7f, HOVER_COLOR);
        }
        return;
    }

    for (const Plane& plane : HANDLE_PLANES) {
        V3 a = Make(axes[plane.a]);
        V3 b = Make(axes[plane.b]);
        bool on = lit(plane.handle);
        if (mode == GizmoMode::Scale) {
            float s = SCALE_PLANE_PX * mpp;
            V3 middle = p + (a + b) * (s * 0.5f);
            mesh.Line(p + a * s, middle, SHAFT_PX * mpp, on ? HOVER_COLOR : AXIS_COLORS[plane.a]);
            mesh.Line(middle, p + b * s, SHAFT_PX * mpp, on ? HOVER_COLOR : AXIS_COLORS[plane.b]);
            if (on) mesh.Triangle(p, p + a * s, p + b * s, WithAlpha(HOVER_COLOR, 0.3f));
            continue;
        }
        float s = PLANE_PX * mpp;
        V3 corner = p + a * s + b * s;
        if (on) mesh.Quad(p, p + a * s, corner, p + b * s, WithAlpha(HOVER_COLOR, 0.3f));
        mesh.Line(p + a * s, corner, SHAFT_PX * mpp, on ? HOVER_COLOR : AXIS_COLORS[plane.b]);
        mesh.Line(p + b * s, corner, SHAFT_PX * mpp, on ? HOVER_COLOR : AXIS_COLORS[plane.a]);
    }

    for (int i = 0; i < 3; i++) {
        V3 dir = Make(axes[i]);
        uint32_t color = lit(handles[i]) ? HOVER_COLOR : AXIS_COLORS[i];
        if (mode == GizmoMode::Scale) {
            mesh.Line(p + dir * (SCALE_CENTER_PX * mpp), p + dir * ((AXIS_PX - SCALE_BOX_PX) * mpp), SHAFT_PX * mpp, color);
            mesh.Box(p + dir * (AXIS_PX * mpp), axes, SCALE_BOX_PX * mpp, color);
        } else {
            float headStart = AXIS_PX - HEAD_LENGTH_PX;
            mesh.Line(p + dir * (CENTER_PX * mpp), p + dir * (headStart * mpp), SHAFT_PX * mpp, color);
            mesh.Cone(p + dir * (headStart * mpp), dir, HEAD_LENGTH_PX * mpp, HEAD_RADIUS_PX * mpp, color);
        }
    }

    uint32_t center = lit(GizmoHandle::Center) ? HOVER_COLOR : CENTER_COLOR;
    if (mode == GizmoMode::Scale) {
        mesh.Box(p, axes, SCALE_CENTER_PX * mpp, center);
    } else {
        mesh.Disk(p, Make(view.right), Make(view.up), CENTER_PX * mpp, center);
    }
}

void TransformGizmo::BeginDrag(const GizmoView& view, GizmoHandle handle, float x, float y) {
    drag = handle;
    startMouse[0] = x;
    startMouse[1] = y;
    std::copy(pivot, pivot + 3, startPivot);
    Axes(startAxes);
    sweep = 0;
    float mpp = view.MetersPerPixel(pivot);
    V3 p = Make(pivot);
    V3 eye = Make(view.eye);
    float ray[3];
    view.Ray(x, y, ray);
    V3 r = Make(ray);

    if (mode == GizmoMode::Translate) {
        if (int axis = AxisOf(handle); axis >= 0) {
            startParam = ClosestParam(p, Make(startAxes[axis]), eye, r);
            return;
        }
        const Plane* plane = PlaneOf(handle);
        V3 normal = plane ? Make(startAxes[plane->normal]) : Make(view.forward);
        V3 hit = p;
        RayPlane(eye, r, p, normal, hit);
        startHit[0] = hit.x;
        startHit[1] = hit.y;
        startHit[2] = hit.z;
        return;
    }

    if (mode == GizmoMode::Rotate) {
        int axis = AxisOf(handle);
        V3 u = axis >= 0 ? Make(startAxes[(axis + 1) % 3]) : Make(view.right);
        V3 v = axis >= 0 ? Make(startAxes[(axis + 2) % 3]) : Make(view.up);
        V3 k = Unit(Cross(u, v));
        float radiusPx = axis >= 0 ? RING_PX : SCREEN_RING_PX;
        float best = FLT_MAX;
        float theta = 0;
        for (int s = 0; s < RING_SEGMENTS; s++) {
            float a = 2 * PI * static_cast<float>(s) / RING_SEGMENTS;
            V3 point = p + (u * std::cos(a) + v * std::sin(a)) * (radiusPx * mpp);
            float world[3] = { point.x, point.y, point.z };
            float sx, sy;
            if (!view.Project(world, sx, sy)) continue;
            float distance = std::hypot(sx - x, sy - y);
            if (distance < best) {
                best = distance;
                theta = a;
            }
        }
        V3 from = u * std::cos(theta) + v * std::sin(theta);
        V3 tangent = v * std::cos(theta) - u * std::sin(theta);
        float a[3] = { p.x + from.x * radiusPx * mpp, p.y + from.y * radiusPx * mpp, p.z + from.z * radiusPx * mpp };
        float b[3] = { a[0] + tangent.x * radiusPx * mpp * 0.1f, a[1] + tangent.y * radiusPx * mpp * 0.1f,
                       a[2] + tangent.z * radiusPx * mpp * 0.1f };
        float ax = 0, ay = 0, bx = 1, by = 0;
        view.Project(a, ax, ay);
        view.Project(b, bx, by);
        float length = std::hypot(bx - ax, by - ay);
        screenDir[0] = length > 1e-4f ? (bx - ax) / length : 1.0f;
        screenDir[1] = length > 1e-4f ? (by - ay) / length : 0.0f;
        screenLength = radiusPx;
        rotateAxis[0] = k.x;
        rotateAxis[1] = k.y;
        rotateAxis[2] = k.z;
        sweepFrom[0] = from.x;
        sweepFrom[1] = from.y;
        sweepFrom[2] = from.z;
        return;
    }

    V3 direction{ 0, 0, 0 };
    if (int axis = AxisOf(handle); axis >= 0) direction = Make(startAxes[axis]);
    else if (const Plane* plane = PlaneOf(handle)) direction = Make(startAxes[plane->a]) + Make(startAxes[plane->b]);
    screenLength = AXIS_PX;
    screenDir[0] = std::sqrt(0.5f);
    screenDir[1] = -std::sqrt(0.5f);
    if (Length(direction) > 0) {
        V3 end = p + Unit(direction) * (AXIS_PX * mpp);
        float world[3] = { end.x, end.y, end.z };
        float cx, cy, ex, ey;
        if (view.Project(pivot, cx, cy) && view.Project(world, ex, ey) && std::hypot(ex - cx, ey - cy) > 1) {
            float length = std::hypot(ex - cx, ey - cy);
            screenDir[0] = (ex - cx) / length;
            screenDir[1] = (ey - cy) / length;
        }
    }
}

Mat4 TransformGizmo::Drag(const GizmoView& view, float x, float y, const GizmoSnap& snap) {
    V3 p = Make(startPivot);
    float moved = (x - startMouse[0]) * screenDir[0] + (y - startMouse[1]) * screenDir[1];

    if (mode == GizmoMode::Translate) {
        float ray[3];
        view.Ray(x, y, ray);
        V3 eye = Make(view.eye);
        V3 r = Make(ray);
        V3 offset{};
        if (int axis = AxisOf(drag); axis >= 0) {
            V3 dir = Make(startAxes[axis]);
            float t = ClosestParam(p, dir, eye, r) - startParam;
            offset = dir * (snap.translate ? Snap(t, snap.translateStep) : t);
        } else if (const Plane* plane = PlaneOf(drag)) {
            V3 hit;
            if (!RayPlane(eye, r, p, Make(startAxes[plane->normal]), hit)) return Identity();
            V3 diff = hit - Make(startHit);
            V3 a = Make(startAxes[plane->a]);
            V3 b = Make(startAxes[plane->b]);
            float da = Dot(diff, a), db = Dot(diff, b);
            if (snap.translate) {
                da = Snap(da, snap.translateStep);
                db = Snap(db, snap.translateStep);
            }
            offset = a * da + b * db;
        } else {
            V3 hit;
            if (!RayPlane(eye, r, p, Make(view.forward), hit)) return Identity();
            offset = hit - Make(startHit);
            if (snap.translate) {
                offset = { Snap(offset.x, snap.translateStep), Snap(offset.y, snap.translateStep), Snap(offset.z, snap.translateStep) };
            }
        }
        float t[3] = { offset.x, offset.y, offset.z };
        return Translation(t);
    }

    if (mode == GizmoMode::Rotate) {
        float angle = moved / screenLength;
        if (snap.rotate && snap.rotateStep > 0) {
            float step = snap.rotateStep * PI / 180.0f;
            angle = Snap(angle, step);
        }
        sweep = angle;
        float s = std::sin(angle * 0.5f);
        float q[4] = { rotateAxis[0] * s, rotateAxis[1] * s, rotateAxis[2] * s, std::cos(angle * 0.5f) };
        const float zero[3] = { 0, 0, 0 };
        const float one[3] = { 1, 1, 1 };
        return AboutPivot(ComposeTRS(zero, q, one), p);
    }

    float factor = 1 + moved / screenLength;
    if (snap.scale) factor = 1 + Snap(factor - 1, snap.scaleStep);
    factor = std::max(factor, 0.01f);
    float scale[3] = { 1, 1, 1 };
    if (int axis = AxisOf(drag); axis >= 0) {
        scale[axis] = factor;
    } else if (const Plane* plane = PlaneOf(drag)) {
        scale[plane->a] = factor;
        scale[plane->b] = factor;
    } else {
        scale[0] = scale[1] = scale[2] = factor;
    }
    Mat4 linear = Identity();
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            float sum = 0;
            for (int k = 0; k < 3; k++) sum += startAxes[k][r] * scale[k] * startAxes[k][c];
            linear.m[r * 4 + c] = sum;
        }
    }
    return AboutPivot(linear, p);
}
