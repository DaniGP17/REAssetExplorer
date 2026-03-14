using System;
using Vortice.Direct3D12;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.DX12;

public class DX12SwapChain : IDisposable
{
    private const int FrameCount = 2;

    private IDXGISwapChain3? _swapChain;
    private ID3D12DescriptorHeap? _rtvHeap;
    private ID3D12DescriptorHeap? _dsvHeap;
    private ID3D12Resource?[] _renderTargets;
    private ID3D12Resource? _depthStencilBuffer;
    private int _rtvDescriptorSize;
    private int _frameIndex;
    private bool _disposed;

    public int CurrentBackBufferIndex => _frameIndex;
    public int RTVDescriptorSize => _rtvDescriptorSize;
    public IDXGISwapChain3? SwapChain => _swapChain;

    public DX12SwapChain()
    {
        _renderTargets = new ID3D12Resource[FrameCount];
    }

    public bool Initialize(ID3D12Device device, IDXGIFactory4 factory, ID3D12CommandQueue commandQueue, IntPtr hwnd, int width, int height)
    {
        try
        {
            // Create swap chain description
            var swapChainDesc = new SwapChainDescription1
            {
                Width = width,
                Height = height,
                Format = Format.R8G8B8A8_UNorm,
                BufferCount = FrameCount,
                BufferUsage = Usage.RenderTargetOutput,
                SwapEffect = SwapEffect.FlipDiscard,
                SampleDescription = new SampleDescription(1, 0)
            };

            // Create swap chain
            using (var tempSwapChain = factory.CreateSwapChainForHwnd(commandQueue, hwnd, swapChainDesc))
            {
                _swapChain = tempSwapChain.QueryInterface<IDXGISwapChain3>();
            }

            if (_swapChain == null)
            {
                Console.WriteLine("Failed to create swap chain");
                return false;
            }

            // Disable Alt+Enter fullscreen
            factory.MakeWindowAssociation(hwnd, WindowAssociationFlags.IgnoreAltEnter);

            _frameIndex = _swapChain.CurrentBackBufferIndex;

            // Create descriptor heap for render target views
            var rtvHeapDesc = new DescriptorHeapDescription
            {
                Type = DescriptorHeapType.RenderTargetView,
                DescriptorCount = FrameCount,
                Flags = DescriptorHeapFlags.None
            };

            _rtvHeap = device.CreateDescriptorHeap(rtvHeapDesc);
            if (_rtvHeap == null)
            {
                Console.WriteLine("Failed to create RTV descriptor heap");
                return false;
            }

            _rtvDescriptorSize = device.GetDescriptorHandleIncrementSize(DescriptorHeapType.RenderTargetView);

            // Create render target views using sRGB format so the GPU applies gamma correction
            // on write. The swap chain itself stays as R8G8B8A8_UNorm (required for FlipDiscard),
            // but writing via an _SRgb RTV converts linear shader output to sRGB for the display.
            var srgbRtvDesc = new RenderTargetViewDescription
            {
                Format = Format.R8G8B8A8_UNorm_SRgb,
                ViewDimension = RenderTargetViewDimension.Texture2D
            };
            var rtvHandle = _rtvHeap.GetCPUDescriptorHandleForHeapStart();
            for (int i = 0; i < FrameCount; i++)
            {
                _renderTargets[i] = _swapChain.GetBuffer<ID3D12Resource>(i);
                device.CreateRenderTargetView(_renderTargets[i], srgbRtvDesc, rtvHandle);
                rtvHandle.Ptr += (nuint)_rtvDescriptorSize;
            }

            // Create depth stencil descriptor heap
            var dsvHeapDesc = new DescriptorHeapDescription
            {
                Type = DescriptorHeapType.DepthStencilView,
                DescriptorCount = 1,
                Flags = DescriptorHeapFlags.None
            };

            _dsvHeap = device.CreateDescriptorHeap(dsvHeapDesc);
            if (_dsvHeap == null)
            {
                Console.WriteLine("Failed to create DSV descriptor heap");
                return false;
            }

            // Create depth stencil buffer
            if (!CreateDepthStencilBuffer(device, width, height))
            {
                Console.WriteLine("Failed to create depth stencil buffer");
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during swap chain initialization: {ex.Message}");
            return false;
        }
    }

    private bool CreateDepthStencilBuffer(ID3D12Device device, int width, int height)
    {
        try
        {
            // Create depth stencil resource
            var depthHeapProperties = new HeapProperties(HeapType.Default);
            var depthResourceDesc = ResourceDescription.Texture2D(
                Format.D32_Float,
                (uint)width,
                (uint)height,
                1, // array size
                1, // mip levels
                1, // sample count
                0, // sample quality
                ResourceFlags.AllowDepthStencil);

            var optimizedClearValue = new ClearValue(Format.D32_Float, 1.0f, 0);

            _depthStencilBuffer = device.CreateCommittedResource(
                depthHeapProperties,
                HeapFlags.None,
                depthResourceDesc,
                ResourceStates.DepthWrite,
                optimizedClearValue);

            if (_depthStencilBuffer == null)
            {
                Console.WriteLine("Failed to create depth stencil buffer resource");
                return false;
            }

            // Create depth stencil view
            var dsvDesc = new DepthStencilViewDescription
            {
                Format = Format.D32_Float,
                ViewDimension = DepthStencilViewDimension.Texture2D,
                Flags = DepthStencilViewFlags.None
            };

            device.CreateDepthStencilView(_depthStencilBuffer, dsvDesc, _dsvHeap!.GetCPUDescriptorHandleForHeapStart());

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating depth stencil buffer: {ex.Message}");
            return false;
        }
    }

    public bool Resize(ID3D12Device device, int width, int height)
    {
        try
        {
            // Release depth stencil buffer
            _depthStencilBuffer?.Dispose();
            _depthStencilBuffer = null;

            // Release render targets
            for (int i = 0; i < FrameCount; i++)
            {
                _renderTargets[i]?.Dispose();
                _renderTargets[i] = null;
            }

            // Resize swap chain
            if (_swapChain == null)
                return false;

            _swapChain.ResizeBuffers(FrameCount, width, height, Format.R8G8B8A8_UNorm, SwapChainFlags.None);
            _frameIndex = _swapChain.CurrentBackBufferIndex;

            var srgbRtvDesc = new RenderTargetViewDescription
            {
                Format = Format.R8G8B8A8_UNorm_SRgb,
                ViewDimension = RenderTargetViewDimension.Texture2D
            };
            var rtvHandle = _rtvHeap!.GetCPUDescriptorHandleForHeapStart();
            for (int i = 0; i < FrameCount; i++)
            {
                _renderTargets[i] = _swapChain.GetBuffer<ID3D12Resource>(i);
                device.CreateRenderTargetView(_renderTargets[i], srgbRtvDesc, rtvHandle);
                rtvHandle.Ptr += (nuint)_rtvDescriptorSize;
            }

            // Recreate depth stencil buffer
            if (!CreateDepthStencilBuffer(device, width, height))
            {
                Console.WriteLine("Failed to recreate depth stencil buffer");
                return false;
            }

            Console.WriteLine($"Swap chain resized to {width}x{height}");
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during swap chain resize: {ex.Message}");
            return false;
        }
    }

    public void Present(int vsync = 1)
    {
        // vsync: 0 = no vsync, 1 = vsync every frame, 2+ = vsync every N frames
        int syncInterval = Math.Max(0, vsync);
        _swapChain?.Present(syncInterval, PresentFlags.None);
        _frameIndex = _swapChain?.CurrentBackBufferIndex ?? 0;
    }

    public ID3D12Resource? GetRenderTarget(int index)
    {
        if (index < 0 || index >= FrameCount)
            return null;

        return _renderTargets[index];
    }

    public CpuDescriptorHandle GetRTVHandle(int index)
    {
        if (_rtvHeap == null)
            throw new InvalidOperationException("RTV heap not initialized");

        var handle = _rtvHeap.GetCPUDescriptorHandleForHeapStart();
        handle.Ptr += (nuint)(index * _rtvDescriptorSize);
        return handle;
    }

    public CpuDescriptorHandle GetDSVHandle()
    {
        if (_dsvHeap == null)
            throw new InvalidOperationException("DSV heap not initialized");

        return _dsvHeap.GetCPUDescriptorHandleForHeapStart();
    }

    public void Shutdown()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _depthStencilBuffer?.Dispose();
        _depthStencilBuffer = null;

        for (int i = 0; i < FrameCount; i++)
        {
            _renderTargets[i]?.Dispose();
            _renderTargets[i] = null;
        }

        _dsvHeap?.Dispose();
        _rtvHeap?.Dispose();
        _swapChain?.Dispose();

        _dsvHeap = null;
        _rtvHeap = null;
        _swapChain = null;
        _disposed = true;
    }
}
