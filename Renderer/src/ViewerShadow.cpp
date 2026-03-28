#include "Renderer/Viewer.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

// {rootConstant, drawArgs} matching an ExecuteIndirect command signature of [CONSTANT, DRAW_INDEXED].
struct ShadowIndirectRecord {
    uint32_t rootConstant;
    D3D12_DRAW_INDEXED_ARGUMENTS args;
};

// Opt-in: GPU-driven ExecuteIndirect batching of shadow draws instead of one CPU-recorded
// DrawIndexedInstanced per object. Off by default — undo with an unset env var, no rebuild needed.
bool ShadowIndirectEnabled() {
    static const bool enabled = std::getenv("RAE_SHADOW_INDIRECT") != nullptr;
    return enabled;
}

using Vec3 = std::array<float, 3>;

constexpr UINT SHADOW_CONSTANTS_STRIDE = 1024;
constexpr UINT SHADOW_CAST_OFFSET = 768;
// DirectionalLight::update snaps each cascade to (512 >> shift) / 2 steps; shift is 0 in the running game
// (the RE8 capture's cascade offsets are whole multiples of size / 256).
constexpr float CASCADE_SNAP_STEPS = 256.0f;
// LightRenderer::updateShadowRotation copies snipet[frame & 1] (re8.exe).
constexpr float SHADOW_SAMPLE_POINTS[2][32] = {
    { -0.7071f, -0.7071f, 0.0f, -0.875f, 0.5303f, 0.5303f, -0.625f, 0.0f, 0.3536f, -0.3536f, 0.0f, 0.375f,
      -0.1768f, -0.1768f, 0.125f, 0.0f, 0.0f, -0.875f, 0.5303f, 0.5303f, -0.625f, 0.0f, 0.3536f, -0.3536f,
      0.0f, 0.375f, -0.1768f, -0.1768f, 0.125f, 0.0f, -0.7071f, -0.7071f },
    { -0.7071f, 0.7071f, 0.0f, 0.875f, 0.5303f, -0.5303f, -0.625f, 0.0f, 0.3536f, 0.3536f, 0.0f, -0.375f,
      -0.1768f, 0.1768f, 0.125f, 0.0f, 0.0f, 0.875f, 0.5303f, -0.5303f, -0.625f, 0.0f, 0.3536f, 0.3536f,
      0.0f, -0.375f, -0.1768f, 0.1768f, 0.125f, 0.0f, -0.7071f, 0.7071f },
};

float Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

Vec3 Normalize(const Vec3& v) {
    float inv = 1.0f / std::sqrt(Dot(v, v));
    return { v[0] * inv, v[1] * inv, v[2] * inv };
}

bool Inverse(const Mat4& m, Mat4& out) {
    const float* a = m.m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0) return false;
    for (int i = 0; i < 16; i++) out.m[i] = inv[i] / det;
    return true;
}

Mat4 FlipZ() {
    Mat4 flip = Identity();
    flip.m[10] = -1;
    flip.m[14] = 1;
    return flip;
}

// A cascade slice of the camera frustum: view space bounds, world space bounds, near and far distance.
struct CascadeSlice {
    float viewMin[3];
    float viewMax[3];
    float worldCenter[3];
    float begin;
    float end;
};

}

void Viewer::SetDirectionalShadow(const ViewerDirectionalShadow& shadow) {
    directionalShadow = shadow;
    shadowCascadesValid = false;
    if (shadow.enabled) CreateShadowTargets();
}

void Viewer::CreateShadowTargets() {
    if (shadowMap) return;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = SHADOW_MAP_SIZE;
    desc.Height = SHADOW_MAP_SIZE;
    desc.DepthOrArraySize = SHADOW_CASCADES;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D16_UNORM;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                          IID_PPV_ARGS(&shadowMap)), "CreateCommittedResource shadow map");

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = SHADOW_CASCADES;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&shadowDsvHeap)), "CreateDescriptorHeap shadow DSV");
    UINT dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (uint32_t i = 0; i < SHADOW_CASCADES; i++) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D16_UNORM;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray = { 0, i, 1 };
        D3D12_CPU_DESCRIPTOR_HANDLE handle = shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(i) * dsvStride;
        device->CreateDepthStencilView(shadowMap.Get(), &dsv, handle);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R16_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.ArraySize = SHADOW_CASCADES;
    device->CreateShaderResourceView(shadowMap.Get(), &srv, SrvCpuHandle(SRV_LIGHT_SHADOW));

    std::vector<uint8_t> zeros(SHADOW_CONSTANTS_STRIDE * SHADOW_CASCADES, 0);
    shadowConstants = CreateUploadBuffer(zeros);
    D3D12_RANGE none{};
    Check(shadowConstants->Map(0, &none, reinterpret_cast<void**>(&shadowConstantsMapped)), "Map shadow constants");
    std::vector<uint8_t> rotationZeros(256, 0);
    shadowRotationBuffer = CreateUploadBuffer(rotationZeros);
    Check(shadowRotationBuffer->Map(0, &none, reinterpret_cast<void**>(&shadowRotationMapped)), "Map shadow rotation");
}

// DirectionalLight::update without SDSM or boundary fit: the camera frustum (at least MinimumFOV wide) is
// cut at ShadowMinimumAreaSize^2 / 16 * Partition of its depth; cascade 0 gets a light view from ShadowDistance / 2
// up the light direction and an orthographic projection over 2 * ShadowDistance, the others scale and offset it.
void Viewer::UpdateShadowCascades() {
    shadowCascadesValid = false;
    const ViewerDirectionalShadow& s = directionalShadow;
    if (!s.enabled || !shadowMap || !lightInfoMapped) return;
    uint32_t enable;
    std::memcpy(&enable, lightInfoMapped + 60, 4);
    if (!enable) return;

    Vec3 d;
    std::memcpy(d.data(), lightInfoMapped + 48, 12);
    d = Normalize(d);
    Vec3 axes[3] = { { s.boundary[0], s.boundary[1], s.boundary[2] },
                     { s.boundary[4], s.boundary[5], s.boundary[6] },
                     { s.boundary[8], s.boundary[9], s.boundary[10] } };
    Vec3 reference{};
    float best = 1.0f;
    float dots[3];
    for (int k = 0; k < 3; k++) dots[k] = std::fabs(Dot(axes[k], d));
    if (dots[0] < 1.0f) {
        reference = axes[0];
        best = dots[0];
    }
    if (!(best <= dots[1])) {
        reference = axes[1];
        best = dots[1];
    }
    if (!(best <= dots[2])) reference = axes[2];
    Vec3 u = Normalize(Cross(reference, d));
    Vec3 v = Normalize(Cross(d, u));

    Mat4 view;
    std::memcpy(view.m, camView, sizeof(view.m));
    Mat4 cameraWorld;
    if (!Inverse(view, cameraWorld)) return;
    constexpr float pi = 3.14159265358979f;
    float fov = std::max(camFov * 180.0f / pi, s.minimumFov) / 180.0f * pi;
    float tanY = std::tan(fov * 0.5f);
    float tanX = tanY * static_cast<float>(width) / static_cast<float>(height);
    float nearPlane = s.cameraNear > 0 ? s.cameraNear : camNear;
    float farPlane = s.cameraFar > 0 ? s.cameraFar : camFar;
    const float corners[4][2] = { { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
    float area = s.minimumAreaSize * s.minimumAreaSize * 0.0625f;
    float t[SHADOW_CASCADES + 1] = { 0, area * s.partition[0], area * s.partition[1], area * s.partition[2], area * s.partition[3] };

    CascadeSlice slices[SHADOW_CASCADES];
    for (uint32_t k = 0; k < SHADOW_CASCADES; k++) {
        CascadeSlice& slice = slices[k];
        float worldMin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
        float worldMax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (int c = 0; c < 3; c++) {
            slice.viewMin[c] = FLT_MAX;
            slice.viewMax[c] = -FLT_MAX;
        }
        for (const auto& corner : corners) {
            float nearCorner[3] = { corner[0] * tanX * nearPlane, corner[1] * tanY * nearPlane, -nearPlane };
            float farCorner[3] = { corner[0] * tanX * farPlane, corner[1] * tanY * farPlane, -farPlane };
            for (float f : { t[k], t[k + 1] }) {
                float p[3];
                for (int c = 0; c < 3; c++) {
                    p[c] = nearCorner[c] + (farCorner[c] - nearCorner[c]) * f;
                    slice.viewMin[c] = std::min(slice.viewMin[c], p[c]);
                    slice.viewMax[c] = std::max(slice.viewMax[c], p[c]);
                }
                float w[3];
                TransformPoint(cameraWorld, p, w);
                for (int c = 0; c < 3; c++) {
                    worldMin[c] = std::min(worldMin[c], w[c]);
                    worldMax[c] = std::max(worldMax[c], w[c]);
                }
            }
        }
        for (int c = 0; c < 3; c++) slice.worldCenter[c] = (worldMin[c] + worldMax[c]) * 0.5f;
        slice.begin = -slice.viewMax[2];
        slice.end = -slice.viewMin[2];
    }
    auto sliceSize = [](const CascadeSlice& slice) {
        float x = slice.viewMax[0] - slice.viewMin[0];
        float y = slice.viewMax[1] - slice.viewMin[1];
        float z = slice.viewMax[2] - slice.viewMin[2];
        return std::ceil(std::max(z, std::max(y, x)) * std::sqrt(2.0f) * 16.0f) * 0.0625f;
    };
    auto snappedCenter = [&](const CascadeSlice& slice, float size) {
        float steps = CASCADE_SNAP_STEPS / size;
        Vec3 c{ slice.worldCenter[0], slice.worldCenter[1], slice.worldCenter[2] };
        float su = std::ceil(Dot(u, c) * steps) * (1.0f / steps);
        float sv = std::ceil(Dot(v, c) * steps) * (1.0f / steps);
        return Vec3{ u[0] * su + v[0] * sv, u[1] * su + v[1] * sv, u[2] * su + v[2] * sv };
    };

    float size0 = sliceSize(slices[0]);
    Vec3 center0 = snappedCenter(slices[0], size0);
    float eye[3], target[3];
    for (int c = 0; c < 3; c++) {
        eye[c] = center0[c] + d[c] * s.distance * 0.5f;
        target[c] = center0[c] - d[c] * s.distance * 0.5f;
    }
    Mat4 view0 = LookAtRH(eye, target, v.data());
    Mat4 proj0{};
    proj0.m[0] = 2.0f / size0;
    proj0.m[5] = 2.0f / size0;
    proj0.m[10] = 1.0f / (0.0f - s.distance * 2.0f);
    proj0.m[14] = 0.0f;
    proj0.m[15] = 1.0f;

    Mat4 texture = Identity();
    texture.m[0] = 0.5f;
    texture.m[5] = -0.5f;
    texture.m[12] = 0.5f;
    texture.m[13] = 0.5f;
    Mat4 dlViewProj = Mul(Mul(Mul(view0, proj0), FlipZ()), texture);

    float translate[3][4] = {};
    float scale[3][2] = {};
    for (uint32_t k = 0; k < SHADOW_CASCADES; k++) {
        float r = 1.0f;
        Vec3 offset{};
        if (k > 0) {
            float size = sliceSize(slices[k]);
            r = size0 / size;
            Vec3 center = snappedCenter(slices[k], size);
            offset = { center0[0] - center[0], center0[1] - center[1], center0[2] - center[2] };
            float lightX = offset[0] * view0.m[0] + offset[1] * view0.m[4] + offset[2] * view0.m[8];
            float lightY = offset[0] * view0.m[1] + offset[1] * view0.m[5] + offset[2] * view0.m[9];
            float clipX = lightX * proj0.m[0] * r;
            float clipY = lightY * proj0.m[5] * r;
            translate[k - 1][0] = clipX * 0.5f + 0.5f - r * 0.5f;
            translate[k - 1][1] = -clipY * 0.5f + 0.5f - r * 0.5f;
            translate[k - 1][2] = r;
            translate[k - 1][3] = 1.0f;
            scale[k - 1][0] = r * 0.5f;
            scale[k - 1][1] = r;
        }
        Mat4 shift = Identity();
        shift.m[12] = offset[0];
        shift.m[13] = offset[1];
        shift.m[14] = offset[2];
        Mat4 viewK = Mul(shift, view0);
        Mat4 projK = proj0;
        projK.m[0] *= r;
        projK.m[5] *= r;
        std::memcpy(cascadeView[k], viewK.m, sizeof(viewK.m));
        cascadeHalfSize[k] = 1.0f / projK.m[0];

        // createSceneInfo_0 + convertGPUSceneInfo for the cascade.
        Mat4 proj = Mul(projK, FlipZ());
        Mat4 viewProj = Mul(viewK, proj);
        Mat4 viewProjInv, viewInv, projInv;
        Inverse(viewProj, viewProjInv);
        Inverse(viewK, viewInv);
        Inverse(proj, projInv);
        float farZ = proj.m[14] / proj.m[10];
        float nearZ = proj.m[10] * farZ / (proj.m[10] + 1.0f);
        uint8_t* info = shadowConstantsMapped + k * SHADOW_CONSTANTS_STRIDE;
        std::memset(info, 0, SHADOW_CONSTANTS_STRIDE);
        auto write = [&](std::size_t offsetBytes, const void* src, std::size_t bytes) { std::memcpy(info + offsetBytes, src, bytes); };
        auto transpose34 = [](const Mat4& m, float* out) {
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 4; col++) out[row * 4 + col] = m.m[col * 4 + row];
        };
        float t34[12];
        write(0, viewProj.m, 64);
        transpose34(viewK, t34);
        write(64, t34, 48);
        transpose34(viewInv, t34);
        write(112, t34, 48);
        const float* p = proj.m;
        const float* pi = projInv.m;
        float projElement[8] = { p[0], p[5], p[8], p[9], p[10], p[11], p[14], p[15] };
        write(160, projElement, 32);
        float projInvElements[8] = { pi[0], pi[5], pi[10], pi[11], pi[12], pi[13], pi[14], pi[15] };
        write(192, projInvElements, 32);
        write(224, viewProjInv.m, 64);
        write(288, viewProj.m, 64);
        float zToLinear[3] = { nearZ / farZ, nearZ / farZ - 1.0f, nearZ };
        write(352, zToLinear, 12);
        float screen[4] = { static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE),
                            1.0f / static_cast<float>(SHADOW_MAP_SIZE), 1.0f / static_cast<float>(SHADOW_MAP_SIZE) };
        write(368, screen, 16);
        float cullingHelper[2] = { 1.0f / nearZ, 1.0f / std::log2(farZ / nearZ) };
        write(384, cullingHelper, 8);
        write(392, &nearZ, 4);
        write(396, &farZ, 4);
        float bias = static_cast<float>((1u << k) * (1u << k));
        float shadowCast[4] = { bias * s.depthBias, bias * s.slopeBias, nearZ, 0.0f };
        write(SHADOW_CAST_OFFSET, shadowCast, 16);
    }

    // LightRenderer::updateShadow's directional fields of LightInfo.
    std::memcpy(lightInfoMapped + 92, &s.aoEfficiency, 4);
    std::memcpy(lightInfoMapped + 96, dlViewProj.m, 64);
    uint32_t zero = 0;
    std::memcpy(lightInfoMapped + 160, &s.variance, 4);
    std::memcpy(lightInfoMapped + 164, &zero, 4);
    std::memcpy(lightInfoMapped + 168, &zero, 4);
    std::memcpy(lightInfoMapped + 172, &s.bias, 4);
    for (int i = 0; i < 3; i++) {
        std::memcpy(lightInfoMapped + 176 + i * 16, translate[i], 16);
        std::memcpy(lightInfoMapped + 224 + i * 8, scale[i], 8);
    }
    std::memcpy(lightInfoMapped + 248, &zero, 4);
    std::memcpy(lightInfoMapped + 252, &s.aoEfficiency, 4);
    float cascadeDistance[4] = { (slices[0].end + slices[1].begin) * 0.5f, (slices[1].end + slices[2].begin) * 0.5f,
                                 (slices[2].end + slices[3].begin) * 0.5f, slices[3].end };
    std::memcpy(lightInfoMapped + 256, cascadeDistance, 16);

    uint64_t frame = environment.frame >= 0 ? static_cast<uint64_t>(environment.frame) : frameCounter;
    std::memcpy(shadowRotationMapped, SHADOW_SAMPLE_POINTS[frame & 1], sizeof(SHADOW_SAMPLE_POINTS[0]));
    shadowCascadesValid = true;
}

void Viewer::RenderShadowCascades() {
    if (!shadowCascadesValid || shadowPipelines.empty()) return;
    D3D12_RESOURCE_BARRIER toDepth = Transition(shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList->ResourceBarrier(1, &toDepth);
    D3D12_VIEWPORT viewport{ 0, 0, static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE), 0, 1 };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(SHADOW_MAP_SIZE), static_cast<LONG>(SHADOW_MAP_SIZE) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    UINT dsvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_GPU_VIRTUAL_ADDRESS constants = shadowConstants->GetGPUVirtualAddress();
    timings.shadowDraws = 0;
    timings.shadowPipelineSwitches = 0;
    bool useIndirect = ShadowIndirectEnabled();
    // One shared write cursor for the whole frame: every cascade's ExecuteIndirect stays queued in the
    // command list until the GPU actually runs it, so a later cascade must never overwrite an earlier
    // one's slice of the buffer before that happens.
    if (useIndirect) EnsureShadowIndirectBuffer(static_cast<uint32_t>(draws.size()) * (SHADOW_CASCADES - 1));
    uint32_t indirectWriteCursor = 0;
    for (uint32_t k = 0; k < SHADOW_CASCADES; k++) {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(k) * dsvStride;
        commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        // The last cascade only takes meshes with RenderEntity's DrawFarCascadeShadowCast, which scripts set at
        // runtime (none in scene data; the RE8 capture's slice 3 holds only the player).
        if (k == SHADOW_CASCADES - 1 || !showFlags.shadows) continue;
        sceneInfoOverride = constants + k * SHADOW_CONSTANTS_STRIDE;
        shadowCastAddress = sceneInfoOverride + SHADOW_CAST_OFFSET;
        const float* m = cascadeView[k];
        float half = cascadeHalfSize[k];
        float depth = directionalShadow.distance * 2.0f;
        if (useIndirect) {
            RenderShadowCascadeIndirect(m, half, depth, indirectWriteCursor);
            continue;
        }
        uint32_t current = UINT32_MAX;
        for (const ViewerMeshDraw& draw : draws) {
            if (!drawMask.empty() && (draw.id >= drawMask.size() || !drawMask[draw.id])) continue;
            if (draw.pipelineIndex >= shadowPipelines.size() || !shadowPipelines[draw.pipelineIndex].pso) continue;
            if (draw.boundsRadius > 0) {
                const float* c = draw.boundsCenter;
                float x = c[0] * m[0] + c[1] * m[4] + c[2] * m[8] + m[12];
                float y = c[0] * m[1] + c[1] * m[5] + c[2] * m[9] + m[13];
                float z = c[0] * m[2] + c[1] * m[6] + c[2] * m[10] + m[14];
                float r = draw.boundsRadius;
                if (std::fabs(x) > half + r || std::fabs(y) > half + r || z > r || z < -depth - r) continue;
                // A caster smaller than ~1.5 shadow-map texels can't contribute a visible shadow;
                // skip it like a shadow LOD (far cascades cover more world per texel, so this mostly
                // drops small clutter there, where it dominates draw count without being seen).
                constexpr float MIN_SHADOW_TEXELS = 2.0f;
                if (r * static_cast<float>(SHADOW_MAP_SIZE) < MIN_SHADOW_TEXELS * half) continue;
            }
            if (draw.pipelineIndex != current) {
                current = draw.pipelineIndex;
                BindGamePipeline(shadowPipelines[current]);
                timings.shadowPipelineSwitches++;
            }
            for (uint32_t paramIndex : shadowPipelines[current].rootConstParams) {
                commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
            }
            commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
            timings.shadowDraws++;
        }
    }
    sceneInfoOverride = 0;
    shadowCastAddress = 0;
    D3D12_RESOURCE_BARRIER toRead = Transition(shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toRead);
    D3D12_VIEWPORT screen{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    D3D12_RECT screenScissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    commandList->RSSetViewports(1, &screen);
    commandList->RSSetScissorRects(1, &screenScissor);
}

// Upload heaps are always in D3D12_RESOURCE_STATE_GENERIC_READ, which already includes
// INDIRECT_ARGUMENT, so this buffer needs no barrier before ExecuteIndirect reads it.
void Viewer::EnsureShadowIndirectBuffer(uint32_t recordsNeeded) {
    if (shadowIndirectBuffer && shadowIndirectCapacity >= recordsNeeded) return;
    uint32_t capacity = std::max<uint32_t>(recordsNeeded, 1024);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(capacity) * sizeof(ShadowIndirectRecord);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (shadowIndirectBuffer) shadowIndirectBuffer->Unmap(0, nullptr);
    shadowIndirectBuffer.Reset();
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&shadowIndirectBuffer)),
          "CreateCommittedResource shadow indirect");
    D3D12_RANGE none{};
    Check(shadowIndirectBuffer->Map(0, &none, reinterpret_cast<void**>(&shadowIndirectMapped)), "Map shadow indirect");
    shadowIndirectCapacity = capacity;
}

// Built once per shadow PSO. nullptr means "don't use indirect for this pipeline" (falls back to a
// direct DrawIndexedInstanced), which only happens if a material's shader binds its instance index
// through something other than exactly one root constant.
ID3D12CommandSignature* Viewer::EnsureIndirectSignature(GamePipeline& pipeline) {
    if (pipeline.indirectSignature) return pipeline.indirectSignature.Get();
    if (pipeline.rootConstParams.size() != 1) return nullptr;
    D3D12_INDIRECT_ARGUMENT_DESC args[2]{};
    args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    args[0].Constant.RootParameterIndex = pipeline.rootConstParams[0];
    args[0].Constant.DestOffsetIn32BitValues = 0;
    args[0].Constant.Num32BitValuesToSet = 1;
    args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = sizeof(ShadowIndirectRecord);
    desc.NumArgumentDescs = 2;
    desc.pArgumentDescs = args;
    if (FAILED(device->CreateCommandSignature(&desc, pipeline.root.Get(), IID_PPV_ARGS(&pipeline.indirectSignature)))) {
        return nullptr;
    }
    return pipeline.indirectSignature.Get();
}

// Same culling and per-pipeline batching as the direct path, but each contiguous same-pipeline run of
// surviving draws is written into shadowIndirectBuffer and issued with one ExecuteIndirect call instead
// of N recorded DrawIndexedInstanced calls. writeCursor is shared across every cascade in the frame
// (see the caller): each call only ever advances it, never rewinds it.
void Viewer::RenderShadowCascadeIndirect(const float* m, float half, float depthRange, uint32_t& writeCursor) {
    auto* records = reinterpret_cast<ShadowIndirectRecord*>(shadowIndirectMapped);
    uint32_t current = UINT32_MAX;
    uint32_t batchStart = 0;
    uint32_t batchCount = 0;
    ID3D12CommandSignature* currentSignature = nullptr;
    auto flush = [&]() {
        if (batchCount > 0 && currentSignature) {
            UINT64 offset = static_cast<UINT64>(batchStart) * sizeof(ShadowIndirectRecord);
            commandList->ExecuteIndirect(currentSignature, batchCount, shadowIndirectBuffer.Get(), offset, nullptr, 0);
        }
        batchCount = 0;
    };
    for (const ViewerMeshDraw& draw : draws) {
        if (!drawMask.empty() && (draw.id >= drawMask.size() || !drawMask[draw.id])) continue;
        if (draw.pipelineIndex >= shadowPipelines.size() || !shadowPipelines[draw.pipelineIndex].pso) continue;
        if (draw.boundsRadius > 0) {
            const float* c = draw.boundsCenter;
            float x = c[0] * m[0] + c[1] * m[4] + c[2] * m[8] + m[12];
            float y = c[0] * m[1] + c[1] * m[5] + c[2] * m[9] + m[13];
            float z = c[0] * m[2] + c[1] * m[6] + c[2] * m[10] + m[14];
            float r = draw.boundsRadius;
            if (std::fabs(x) > half + r || std::fabs(y) > half + r || z > r || z < -depthRange - r) continue;
            constexpr float MIN_SHADOW_TEXELS = 2.0f;
            if (r * static_cast<float>(SHADOW_MAP_SIZE) < MIN_SHADOW_TEXELS * half) continue;
        }
        if (draw.pipelineIndex != current) {
            flush();
            current = draw.pipelineIndex;
            BindGamePipeline(shadowPipelines[current]);
            timings.shadowPipelineSwitches++;
            currentSignature = shadowPipelines[current].rootConstParams.size() == 1
                                    ? EnsureIndirectSignature(shadowPipelines[current])
                                    : nullptr;
            batchStart = writeCursor;
        }
        if (currentSignature && writeCursor < shadowIndirectCapacity) {
            ShadowIndirectRecord& rec = records[writeCursor];
            rec.rootConstant = draw.instanceIndex | (draw.materialSlot << 24);
            rec.args = { draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0 };
            writeCursor++;
            batchCount++;
        } else {
            for (uint32_t paramIndex : shadowPipelines[current].rootConstParams) {
                commandList->SetGraphicsRoot32BitConstant(paramIndex, draw.instanceIndex | (draw.materialSlot << 24), 0);
            }
            commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
        }
        timings.shadowDraws++;
    }
    flush();
}
