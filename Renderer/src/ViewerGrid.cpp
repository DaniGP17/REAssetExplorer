#include "Renderer/Viewer.h"

#include <cstring>
#include <d3dcompiler.h>
#include <string>

#include "D3D12Utils.h"
#include "Renderer/RenderMath.h"

using Microsoft::WRL::ComPtr;

namespace {

struct GridConstants {
    float viewProj[16];
    float viewProjInv[16];
    float eye[3];
    float pad0;
    float viewport[2];
    float pad1[2];
};

const char* GRID_SOURCE = R"(
cbuffer Constants : register(b0) {
    row_major float4x4 viewProj;
    row_major float4x4 viewProjInv;
    float3 eye;
    float2 viewport;
};
Texture2D<float> sceneDepth : register(t0);

float4 VSMain(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float Lines(float2 coord, float spacing) {
    float2 c = coord / spacing;
    float2 width = max(fwidth(c), 1e-5);
    float2 dist = abs(frac(c - 0.5) - 0.5) / width;
    float coverage = 1 - saturate(min(dist.x, dist.y));
    float cellsPerPixel = max(width.x, width.y);
    return coverage * (1 - saturate((cellsPerPixel - 0.125) / 0.125));
}

float4 PSMain(float4 pos : SV_Position) : SV_Target {
    float2 ndc = float2(pos.x / viewport.x * 2 - 1, 1 - pos.y / viewport.y * 2);
    float4 a = mul(float4(ndc, 0, 1), viewProjInv);
    float4 b = mul(float4(ndc, 1, 1), viewProjInv);
    float3 nearP = a.xyz / a.w;
    float3 dir = b.xyz / b.w - nearP;
    float t = abs(dir.y) > 1e-8 ? -nearP.y / dir.y : -1;
    bool onPlane = t > 0 && t < 1;
    float3 hit = nearP + dir * saturate(t);

    // Derivatives before any branch: every pixel of the quad must reach them.
    float2 coord = hit.xz;
    float2 fw = max(fwidth(coord), 1e-5);
    float alpha = max(max(Lines(coord, 0.1) * 0.26, Lines(coord, 1.0) * 0.45),
                      max(Lines(coord, 10.0) * 0.62, Lines(coord, 100.0) * 0.72));
    float3 color = float3(0.62, 0.62, 0.62);
    float xAxis = 1 - saturate(abs(coord.y) / (fw.y * 1.5));
    float zAxis = 1 - saturate(abs(coord.x) / (fw.x * 1.5));
    color = lerp(color, float3(0.86, 0.26, 0.26), xAxis);
    alpha = max(alpha, xAxis * 0.85);
    color = lerp(color, float3(0.28, 0.48, 0.92), zAxis);
    alpha = max(alpha, zAxis * 0.85);

    float4 clip = mul(float4(hit, 1), viewProj);
    bool visible = onPlane && clip.z / clip.w <= sceneDepth.Load(int3(pos.xy, 0));
    float fade = 1 - saturate(distance(hit, eye) / max(40.0, abs(eye.y) * 30.0));
    alpha *= visible ? fade * fade : 0;
    return float4(color * alpha, alpha);
}
)";

}

void Viewer::CreateGridPipeline() {
    if (gridPso) return;

    D3D12_DESCRIPTOR_RANGE depthRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = { 0, 0, sizeof(GridConstants) / 4 };
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = { 1, &depthRange };
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors),
          "D3D12SerializeRootSignature grid");
    Check(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&gridRootSignature)), "CreateRootSignature grid");

    auto compile = [](const char* entry, const char* target) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> compileErrors;
        HRESULT hr = D3DCompile(GRID_SOURCE, std::strlen(GRID_SOURCE), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, &blob, &compileErrors);
        if (FAILED(hr)) {
            std::string msg = std::string("compile grid ") + entry;
            if (compileErrors) msg += std::string(": ") + static_cast<const char*>(compileErrors->GetBufferPointer());
            throw std::runtime_error(msg);
        }
        return blob;
    };
    ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ps = compile("PSMain", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = gridRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = psoDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    Check(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&gridPso)), "CreateGraphicsPipelineState grid");
}

// After the resolve, with the scene depth readable and the back buffer bound.
void Viewer::DrawGrid() {
    CreateGridPipeline();

    Mat4 view;
    Mat4 proj;
    std::memcpy(view.m, camView, sizeof(view.m));
    std::memcpy(proj.m, camProj, sizeof(proj.m));
    Mat4 viewProj = Mul(view, proj);
    GridConstants constants{};
    std::memcpy(constants.viewProj, viewProj.m, sizeof(constants.viewProj));
    std::memcpy(constants.viewProjInv, skyViewProjInv, sizeof(constants.viewProjInv));
    std::memcpy(constants.eye, camEye, sizeof(constants.eye));
    constants.viewport[0] = static_cast<float>(width);
    constants.viewport[1] = static_cast<float>(height);

    commandList->SetGraphicsRootSignature(gridRootSignature.Get());
    commandList->SetPipelineState(gridPso.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(GridConstants) / 4, &constants, 0);
    commandList->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(SRV_RESOLVE_DEPTH));
    commandList->DrawInstanced(3, 1, 0, 0);
}
