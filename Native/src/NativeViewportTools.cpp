#include "NativeViewport.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>

namespace {

// Unreal's LOD coloration: LOD0 white, then red, green, blue, yellow, magenta, cyan, purple.
constexpr uint32_t LOD_COLORS[8] = { 0xFFFFFFFFu, 0xFF3030E0u, 0xFF30D030u, 0xFFE05030u,
                                     0xFF20E0E0u, 0xFFE040E0u, 0xFFE0E030u, 0xFFA03080u };
// A near-orthographic lens: the camera backs off so the view barely converges.
constexpr float ORTHO_FOV = 0.01f;
constexpr float ORTHO_DEPTH_RANGE = 20000.0f;
constexpr float UNDO_COALESCE_SECONDS = 1.0f;
constexpr float FLOOR_PROBE_LIFT = 0.05f;

void Normalized(float v[3]) {
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-12f) return;
    for (int c = 0; c < 3; c++) v[c] /= length;
}

void ViewForward(ViewportView view, float out[3]) {
    const float directions[7][3] = { { 0, 0, 1 }, { 0, -1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
    std::copy(directions[static_cast<int>(view)], directions[static_cast<int>(view)] + 3, out);
}

}

void NativeViewport::ClassifyDraws() {
    drawKind.assign(pick.draws.size(), DRAW_STATIC);
    std::vector<uint32_t> colors(pick.draws.size(), LOD_COLORS[0]);
    for (std::size_t id = 0; id < pick.draws.size(); id++) {
        uint32_t instance = pick.draws[id].instanceIndex;
        if (instance < instanceSkinned.size() && instanceSkinned[instance]) drawKind[id] = DRAW_SKINNED;
        if (instance < pick.instanceMaterials.size()) {
            const std::vector<uint32_t>& slots = pick.instanceMaterials[instance];
            uint32_t slot = pick.draws[id].materialSlot;
            if (slot < slots.size() && slots[slot] < pick.materialNames.size()) {
                std::string name = pick.materialNames[slots[slot]];
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name.find("decal") != std::string::npos) drawKind[id] = DRAW_DECAL;
            }
        }
        if (id < pick.tags.size()) colors[id] = LOD_COLORS[std::min<uint32_t>(pick.tags[id].lod, 7)];
    }
    viewer->SetDrawColors(colors);
}

bool NativeViewport::DrawAllowed(std::size_t id) const {
    uint32_t mask = appliedShowMask;
    uint8_t kind = id < drawKind.size() ? drawKind[id] : DRAW_STATIC;
    if (kind == DRAW_DECAL) return (mask & SHOW_DECALS) != 0;
    if (kind == DRAW_SKINNED) return (mask & SHOW_SKINNED_MESHES) != 0;
    return (mask & SHOW_STATIC_MESHES) != 0;
}

int32_t NativeViewport::ForcedInstanceLod(uint32_t instance) const {
    if (appliedForcedLod < 0 || instance >= meshLodByInstance.size() || meshLodByInstance[instance] < 0) return -1;
    const SceneMeshLod& lod = meshLods[static_cast<std::size_t>(meshLodByInstance[instance])];
    return std::min(appliedForcedLod, static_cast<int32_t>(lod.lodCount) - 1);
}

void NativeViewport::ApplyViewerSettings() {
    uint32_t mask = showMask.load();
    ViewerShowFlags flags;
    flags.fog = (mask & SHOW_FOG) != 0;
    flags.volumetricFog = (mask & SHOW_VOLUMETRIC_FOG) != 0;
    flags.shadows = (mask & SHOW_SHADOWS) != 0;
    flags.postProcess = (mask & SHOW_POST_PROCESS) != 0;
    flags.localCubemaps = (mask & SHOW_LOCAL_CUBEMAPS) != 0;
    viewer->SetShowFlags(flags);
    viewer->SetExposureOverride(exposureFixed.load(), exposureEv.load());
    viewer->SetTemporalAAAllowed(temporalAAOn.load());

    int32_t forced = forcedLod.load();
    bool streamAll = streamAllZones.load();
    bool maskChanged = mask != appliedShowMask || forced != appliedForcedLod || streamAll != appliedStreamAll;
    if (streamAll != appliedStreamAll) lodNear.clear();
    appliedShowMask = mask;
    appliedForcedLod = forced;
    appliedStreamAll = streamAll;
    if (maskChanged && hasScene) UpdateDrawMask();
}

void NativeViewport::EnterView(ViewportView next) {
    if (next == viewKind) return;
    if (viewKind == ViewportView::Perspective) {
        perspectivePose = camera.GetPose();
        viewer->GetClipPlanes(clipNear, clipFar);
        float eye[3];
        float pivot[3];
        camera.GetEye(eye);
        camera.GetPivot(pivot);
        float d[3] = { pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2] };
        float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        std::copy(pivot, pivot + 3, orthoCenter);
        orthoHalfHeight = std::max(distance * std::tan(camera.GetFov() * 0.5f), 0.5f);
    }
    viewKind = next;
    viewShown = static_cast<int32_t>(next);
    if (next == ViewportView::Perspective) {
        camera.SetPose(perspectivePose);
        viewer->SetClipPlanes(clipNear, clipFar);
    }
}

// Pans with any mouse button, zooms with the wheel, never turns.
void NativeViewport::UpdateViewCamera(const CameraInput& input, float panX, float panY, float dt) {
    (void)dt;
    float forward[3];
    ViewForward(viewKind, forward);
    const float worldUp[3] = { 0, 1, 0 };
    const float worldForward[3] = { 0, 0, 1 };
    const float* upHint = std::fabs(forward[1]) > 0.9f ? worldForward : worldUp;
    float right[3] = { forward[1] * upHint[2] - forward[2] * upHint[1], forward[2] * upHint[0] - forward[0] * upHint[2],
                       forward[0] * upHint[1] - forward[1] * upHint[0] };
    Normalized(right);
    float up[3] = { right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                    right[0] * forward[1] - right[1] * forward[0] };
    float height = static_cast<float>(std::max(1u, viewer->GetHeight()));
    float metersPerPixel = 2.0f * orthoHalfHeight / height;
    for (int c = 0; c < 3; c++) orthoCenter[c] += (-right[c] * panX + up[c] * panY) * metersPerPixel;
    if (input.wheel != 0) orthoHalfHeight = std::clamp(orthoHalfHeight * std::pow(0.8f, input.wheel), 0.01f, 50000.0f);

    float distance = orthoHalfHeight / std::tan(ORTHO_FOV * 0.5f);
    CameraPose pose{};
    for (int c = 0; c < 3; c++) pose.position[c] = orthoCenter[c] - forward[c] * distance;
    pose.yaw = std::atan2(forward[0], forward[2]);
    pose.pitch = std::asin(std::clamp(forward[1], -1.0f, 1.0f));
    pose.fov = ORTHO_FOV;
    camera.SetPose(pose);
    viewer->SetClipPlanes(std::max(distance - ORTHO_DEPTH_RANGE, distance * 0.02f), distance + ORTHO_DEPTH_RANGE);
}

void NativeViewport::Bookmark(int32_t slot, bool save) {
    if (slot < 0 || slot > 9) return;
    std::lock_guard lock(mutex);
    bookmarkRequests.emplace_back(slot, save);
}

void NativeViewport::ApplyBookmarks() {
    std::vector<std::pair<int32_t, bool>> requests;
    {
        std::lock_guard lock(mutex);
        requests.swap(bookmarkRequests);
    }
    for (const auto& [slot, save] : requests) {
        SavedView& saved = bookmarks[slot];
        if (save) {
            saved.saved = true;
            saved.pose = viewKind == ViewportView::Perspective ? camera.GetPose() : perspectivePose;
        } else if (saved.saved) {
            EnterView(ViewportView::Perspective);
            camera.SetPose(saved.pose);
        }
    }
    uint32_t mask = 0;
    for (int i = 0; i < 10; i++) mask |= bookmarks[i].saved ? 1u << i : 0u;
    bookmarkMask = mask;
}

void NativeViewport::RecordUndo(const std::vector<int32_t>& targets, const std::vector<Mat4>& before, bool coalesce) {
    UndoEntry entry;
    for (std::size_t i = 0; i < targets.size(); i++) {
        entry.keys.push_back(objects[static_cast<std::size_t>(targets[i])].key);
        entry.before.push_back(before[i]);
        entry.after.push_back(objects[static_cast<std::size_t>(targets[i])].world);
    }
    entry.time = std::chrono::steady_clock::now();
    entry.coalescable = coalesce;
    if (coalesce && !undoStack.empty()) {
        UndoEntry& top = undoStack.back();
        float age = std::chrono::duration<float>(entry.time - top.time).count();
        if (top.coalescable && top.keys == entry.keys && age < UNDO_COALESCE_SECONDS) {
            top.after = entry.after;
            top.time = entry.time;
            return;
        }
    }
    undoStack.push_back(std::move(entry));
    redoStack.clear();
    undoState = (undoStack.empty() ? 0u : 1u) | (redoStack.empty() ? 0u : 2u);
}

void NativeViewport::ApplyUndo(bool redo) {
    std::vector<UndoEntry>& from = redo ? redoStack : undoStack;
    std::vector<UndoEntry>& to = redo ? undoStack : redoStack;
    if (from.empty()) return;
    UndoEntry entry = std::move(from.back());
    from.pop_back();
    for (std::size_t i = 0; i < entry.keys.size(); i++) {
        auto found = objectIndex.find(entry.keys[i]);
        if (found == objectIndex.end()) continue;
        MoveObject(found->second, redo ? entry.after[i] : entry.before[i]);
        std::lock_guard lock(mutex);
        transformChanges[entry.keys[i]] = LocalTransformText(found->second);
    }
    entry.coalescable = false;
    to.push_back(std::move(entry));
    RefreshSelectionBounds();
    undoState = (undoStack.empty() ? 0u : 1u) | (redoStack.empty() ? 0u : 2u);
}

// Unreal's End: each selected object drops until its lowest point rests on the mesh below it.
void NativeViewport::SnapToFloor() {
    std::vector<int32_t> roots = SelectionRoots();
    if (roots.empty()) return;
    std::vector<Mat4> before;
    for (int32_t root : roots) before.push_back(objects[static_cast<std::size_t>(root)].world);
    const float* positions = reinterpret_cast<const float*>(pick.positions.data());
    const uint16_t* indices = reinterpret_cast<const uint16_t*>(pick.indices.data());
    int64_t vertexCount = static_cast<int64_t>(pick.positions.size() / 12);
    for (int32_t root : roots) {
        std::vector<uint8_t> skip(pick.worlds.size(), 0);
        std::vector<int32_t> stack{ root };
        float lowest = FLT_MAX;
        float center[2] = { 0, 0 };
        float count = 0;
        while (!stack.empty()) {
            int32_t object = stack.back();
            stack.pop_back();
            for (int32_t child : objectChildren[static_cast<std::size_t>(object)]) stack.push_back(child);
            auto owned = pick.instancesByOwner.find(objects[static_cast<std::size_t>(object)].key);
            if (owned == pick.instancesByOwner.end()) continue;
            for (uint32_t instance : owned->second) {
                skip[instance] = 1;
                const float* m = pick.worlds[instance].m;
                for (uint32_t id : drawsByInstance[instance]) {
                    const ViewerMeshDraw& draw = pick.draws[id];
                    if (!drawShown.empty() && !drawShown[id]) continue;
                    std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(draw.startIndex) + draw.indexCount, pick.indices.size() / 2);
                    for (std::size_t i = draw.startIndex; i < end; i++) {
                        int64_t v = draw.baseVertex + indices[i];
                        if (v < 0 || v >= vertexCount) continue;
                        const float* p = positions + v * 3;
                        float y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
                        lowest = std::min(lowest, y);
                        center[0] += m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
                        center[1] += m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
                        count++;
                    }
                }
            }
        }
        const Mat4& world = objects[static_cast<std::size_t>(root)].world;
        if (count == 0) {
            lowest = world.m[13];
            center[0] = world.m[12];
            center[1] = world.m[14];
        } else {
            center[0] /= count;
            center[1] /= count;
        }
        float origin[3] = { center[0], lowest + FLOOR_PROBE_LIFT, center[1] };
        const float down[3] = { 0, -1, 0 };
        float hit = RaycastMeshes(origin, down, &skip);
        if (hit == FLT_MAX) continue;
        float drop[3] = { 0, origin[1] - hit - lowest, 0 };
        MoveObject(root, Mul(world, Translation(drop)));
        std::lock_guard lock(mutex);
        transformChanges[objects[static_cast<std::size_t>(root)].key] = LocalTransformText(root);
    }
    RecordUndo(roots, before, false);
    RefreshSelectionBounds();
}
