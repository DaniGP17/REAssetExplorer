#include "Renderer/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "D3D12Utils.h"

namespace {

float HalfToFloat(uint32_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;
    if (exponent == 0) {
        float value = std::ldexp(static_cast<float>(mantissa), -24);
        return sign ? -value : value;
    }
    uint32_t bits = exponent == 31 ? sign | 0x7F800000u | (mantissa << 13) : sign | ((exponent + 112) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t Channels(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 3;
    case DXGI_FORMAT_D32_FLOAT:
        return 1;
    default:
        return 4;
    }
}

float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

}

void Viewer::RecordTargetCapture(D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!resolveHdrPso) return;
    if (!targetHdr || targetHdr->GetDesc().Width != width || targetHdr->GetDesc().Height != height) {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                              nullptr, IID_PPV_ARGS(targetHdr.ReleaseAndGetAddressOf())),
              "CreateCommittedResource target HDR");
        if (!targetRtvHeap) {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
            heapDesc.NumDescriptors = 1;
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&targetRtvHeap)), "CreateDescriptorHeap target RTV");
        }
        device->CreateRenderTargetView(targetHdr.Get(), nullptr, targetRtvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    D3D12_RESOURCE_BARRIER toRt = Transition(targetHdr.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &toRt);
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = targetRtvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, nullptr);
    commandList->SetPipelineState(resolveHdrPso.Get());
    commandList->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER toCopy = Transition(targetHdr.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &toCopy);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    struct Source {
        const char* name;
        ID3D12Resource* resource;
    };
    std::vector<Source> sources;
    const char* gbufferNames[4] = { "gb0", "gb1", "gb2", "gb3" };
    for (int i = 0; i < 4; i++) sources.push_back({ gbufferNames[i], gbuffer[i].Get() });
    if (gidTarget) {
        sources.push_back({ "gid", gidTarget.Get() });
        sources.push_back({ "gis", gisTarget.Get() });
    }
    if (hdrTarget) sources.push_back({ "lit", hdrTarget.Get() });
    if (hdrAuxTarget) sources.push_back({ "lit_aux", hdrAuxTarget.Get() });
    if (fogTarget) sources.push_back({ "fogpass", fogTarget.Get() });
    sources.push_back({ "depth", depthBuffer.Get() });

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (const Source& source : sources) {
        barriers.push_back(Transition(source.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    sources.push_back({ "fog", targetHdr.Get() });
    if (volumetricRunning) {
        sources.push_back({ "volfog", volumetricFogTexture.Get() });
        sources.push_back({ "shaded", shadedFog[(volumetricFrame - 1) & 1].Get() });
        for (std::size_t i = sources.size() - 2; i < sources.size(); i++) {
            barriers.push_back(Transition(sources[i].resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
        }
    }
    commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    targetCopies.clear();
    UINT64 total = 0;
    for (const Source& source : sources) {
        D3D12_RESOURCE_DESC desc = source.resource->GetDesc();
        TargetCopy copy{ source.name, desc.Format, {} };
        UINT64 bytes = 0;
        total = (total + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
        device->GetCopyableFootprints(&desc, 0, 1, total, &copy.footprint, nullptr, nullptr, &bytes);
        total = copy.footprint.Offset + bytes;
        targetCopies.push_back(copy);
    }
    if (!targetReadback || targetReadback->GetDesc().Width < total) {
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = total;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(targetReadback.ReleaseAndGetAddressOf())),
              "CreateCommittedResource target readback");
    }
    for (std::size_t i = 0; i < sources.size(); i++) {
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = sources[i].resource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = targetReadback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = targetCopies[i].footprint;
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void Viewer::ReadTargetCapture(const std::vector<uint8_t>& finalRgba) {
    ViewerTargetCapture& out = *targetCaptureOut;
    out.images.clear();
    out.exposure = 0;
    if (!targetCopies.empty()) {
        const TargetCopy& last = targetCopies.back();
        UINT64 end = last.footprint.Offset + static_cast<UINT64>(last.footprint.Footprint.RowPitch) * last.footprint.Footprint.Height *
                                             last.footprint.Footprint.Depth;
        uint8_t* mapped = nullptr;
        D3D12_RANGE range{ 0, static_cast<SIZE_T>(end) };
        Check(targetReadback->Map(0, &range, reinterpret_cast<void**>(&mapped)), "Map target readback");
        for (const TargetCopy& copy : targetCopies) {
            // A volume reads as its slices stacked vertically.
            ViewerTargetImage image{ copy.name, copy.footprint.Footprint.Width,
                                     copy.footprint.Footprint.Height * copy.footprint.Footprint.Depth, Channels(copy.format), {} };
            image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * image.channels);
            float* dst = image.pixels.data();
            for (uint32_t y = 0; y < image.height; y++) {
                const uint8_t* row = mapped + copy.footprint.Offset + static_cast<std::size_t>(y) * copy.footprint.Footprint.RowPitch;
                for (uint32_t x = 0; x < image.width; x++) {
                    switch (copy.format) {
                    case DXGI_FORMAT_R16G16B16A16_FLOAT: {
                        uint16_t h[4];
                        std::memcpy(h, row + x * 8, 8);
                        for (int c = 0; c < 4; c++) *dst++ = HalfToFloat(h[c]);
                        break;
                    }
                    case DXGI_FORMAT_R32_UINT:
                    case DXGI_FORMAT_R11G11B10_FLOAT: {
                        // Also the ambient pass's packing: R11G11B10 as the top bits of each half.
                        uint32_t v;
                        std::memcpy(&v, row + x * 4, 4);
                        *dst++ = HalfToFloat((v & 0x7FF) << 4);
                        *dst++ = HalfToFloat(((v >> 11) & 0x7FF) << 4);
                        *dst++ = HalfToFloat(((v >> 22) & 0x3FF) << 5);
                        break;
                    }
                    case DXGI_FORMAT_R32G32B32A32_FLOAT: {
                        float v[4];
                        std::memcpy(v, row + x * 16, 16);
                        for (int c = 0; c < 3; c++) *dst++ = v[c];
                        out.exposure = v[3];
                        break;
                    }
                    case DXGI_FORMAT_D32_FLOAT:
                        std::memcpy(dst++, row + x * 4, 4);
                        break;
                    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                        for (int c = 0; c < 3; c++) *dst++ = SrgbToLinear(row[x * 4 + c] / 255.0f);
                        *dst++ = row[x * 4 + 3] / 255.0f;
                        break;
                    case DXGI_FORMAT_R10G10B10A2_UNORM: {
                        uint32_t v;
                        std::memcpy(&v, row + x * 4, 4);
                        for (int c = 0; c < 3; c++) *dst++ = ((v >> (c * 10)) & 1023) / 1023.0f;
                        *dst++ = (v >> 30) / 3.0f;
                        break;
                    }
                    case DXGI_FORMAT_R16G16B16A16_SNORM: {
                        int16_t v[4];
                        std::memcpy(v, row + x * 8, 8);
                        for (int c = 0; c < 4; c++) *dst++ = std::max(v[c] / 32767.0f, -1.0f);
                        break;
                    }
                    default:
                        for (int c = 0; c < 4; c++) *dst++ = row[x * 4 + c] / 255.0f;
                        break;
                    }
                }
            }
            out.images.push_back(std::move(image));
        }
        D3D12_RANGE none{};
        targetReadback->Unmap(0, &none);
        targetCopies.clear();

        auto find = [&](const char* name) {
            for (std::size_t i = 0; i < out.images.size(); i++) {
                if (out.images[i].name == name) return static_cast<int>(i);
            }
            return -1;
        };
        int lit = find("lit");
        int aux = find("lit_aux");
        if (lit >= 0 && aux >= 0) {
            std::vector<float>& sum = out.images[lit].pixels;
            const std::vector<float>& add = out.images[aux].pixels;
            for (std::size_t i = 0; i < sum.size(); i++) sum[i] += add[i];
            out.images.erase(out.images.begin() + aux);
        }
    }
    if (finalRgba.size() == static_cast<std::size_t>(width) * height * 4) {
        ViewerTargetImage image{ "final", width, height, 3, {} };
        image.pixels.resize(static_cast<std::size_t>(width) * height * 3);
        for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; i++) {
            for (int c = 0; c < 3; c++) image.pixels[i * 3 + c] = finalRgba[i * 4 + c] / 255.0f;
        }
        out.images.push_back(std::move(image));
    }
}
