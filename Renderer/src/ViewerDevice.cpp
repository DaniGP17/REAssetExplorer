#include "Renderer/Viewer.h"

#include <cstring>

#include "D3D12Utils.h"

using Microsoft::WRL::ComPtr;

Viewer::Viewer(HWND hwnd, uint32_t width, uint32_t height)
    : width(width), height(height) {
    CreateDevice();
    CreateSwapChain(hwnd);
    CreateRenderTargets();
    CreateDepthBuffer();
    CreateCommandObjects();
    SetClipPlanes(camNear, camFar);
}

Viewer::~Viewer() {
    if (fence) WaitForGpu();
    if (fenceEvent) CloseHandle(fenceEvent);
}

void Viewer::CreateDevice() {
    bool debugLayer = std::getenv("RAE_D3D_DEBUG") != nullptr;
#ifndef NDEBUG
    debugLayer = true;
#endif
    if (debugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            bool gpuValidation = std::getenv("RAE_D3D_GBV") != nullptr;
#ifndef NDEBUG
            gpuValidation = true;
#endif
            ComPtr<ID3D12Debug1> debug1;
            if (gpuValidation && SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
        }
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    // RAE_D3D_WARP runs on the software rasterizer: a bad GPU access fails there instead of hanging the GPU.
    ComPtr<IDXGIAdapter> warp;
    if (std::getenv("RAE_D3D_WARP") != nullptr) Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
    if (debugLayer) device.As(&debugMessages);

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
}

void Viewer::CreateSwapChain(HWND hwnd) {
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.BufferCount = FRAME_COUNT;
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain1),
          "CreateSwapChainForHwnd");
    Check(swapChain1.As(&swapChain), "IDXGISwapChain3");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void Viewer::CreateRenderTargets() {
    if (!rtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = FRAME_COUNT;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap RTV");
        rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        Check(swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i])), "GetBuffer");
        device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, handle);
        handle.ptr += rtvStride;
    }
}

void Viewer::CreateDepthBuffer() {
    if (!dsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap)), "CreateDescriptorHeap DSV");
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                                          IID_PPV_ARGS(&depthBuffer)), "CreateCommittedResource depth");

    device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    // Tests against the scene while the passes after lighting also read it.
    D3D12_DEPTH_STENCIL_VIEW_DESC readOnly{};
    readOnly.Format = DXGI_FORMAT_D32_FLOAT;
    readOnly.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    readOnly.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    device->CreateDepthStencilView(depthBuffer.Get(), &readOnly, handle);
}

void Viewer::CreateCommandObjects() {
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "CreateCommandAllocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commandList)), "CreateCommandList");
    Check(commandList->Close(), "Close");

    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr) {
        throw std::runtime_error("CreateEventW failed");
    }
}

void Viewer::WaitForGpu() {
    fenceValue++;
    Check(queue->Signal(fence.Get(), fenceValue), "Signal");
    if (fence->GetCompletedValue() < fenceValue) {
        Check(fence->SetEventOnCompletion(fenceValue, fenceEvent), "SetEventOnCompletion");
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> Viewer::CreateUploadBuffer(std::span<const uint8_t> data) {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = data.size();
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                          IID_PPV_ARGS(&buffer)), "CreateCommittedResource upload");

    void* mapped = nullptr;
    D3D12_RANGE readRange{};
    Check(buffer->Map(0, &readRange, &mapped), "Map");
    std::memcpy(mapped, data.data(), data.size());
    buffer->Unmap(0, nullptr);
    return buffer;
}
