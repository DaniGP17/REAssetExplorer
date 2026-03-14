using System;
using Vortice.Direct3D;
using Vortice.Direct3D12;
using Vortice.Direct3D12.Debug;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.DX12;

public class DX12Device : IDisposable
{
    private ID3D12Device? _device;
    private IDXGIFactory4? _factory;
    private bool _disposed;

    public ID3D12Device Device => _device ?? throw new InvalidOperationException("Device not initialized");
    public IDXGIFactory4 Factory => _factory ?? throw new InvalidOperationException("Factory not initialized");

    public bool Initialize()
    {
        try
        {
            // Enable debug layer in debug builds
#if DEBUG
            if (D3D12.D3D12GetDebugInterface<ID3D12Debug>(out var debug).Success)
            {
                debug.EnableDebugLayer();
                debug.Dispose();
            }
#endif

            // Create DXGI factory
            if (DXGI.CreateDXGIFactory2(false, out _factory).Failure)
            {
                Console.WriteLine("Failed to create DXGI factory");
                return false;
            }

            // Find hardware adapter
            IDXGIAdapter1? adapter = GetHardwareAdapter();
            if (adapter == null)
            {
                Console.WriteLine("Failed to find hardware adapter");
                return false;
            }

            // Create D3D12 device
            if (D3D12.D3D12CreateDevice(adapter, FeatureLevel.Level_11_0, out _device).Failure)
            {
                Console.WriteLine("Failed to create D3D12 device");
                adapter.Dispose();
                return false;
            }

            adapter.Dispose();
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during device initialization: {ex.Message}");
            return false;
        }
    }

    private IDXGIAdapter1? GetHardwareAdapter()
    {
        if (_factory == null)
            return null;

        for (int adapterIndex = 0; _factory.EnumAdapters1(adapterIndex, out IDXGIAdapter1? adapter).Success; adapterIndex++)
        {
            AdapterDescription1 desc = adapter.Description1;

            // Skip software adapter
            if ((desc.Flags & AdapterFlags.Software) != AdapterFlags.None)
            {
                adapter.Dispose();
                continue;
            }

            // Check if adapter supports D3D12
            if (D3D12.D3D12CreateDevice(adapter, FeatureLevel.Level_11_0, out ID3D12Device? testDevice).Success)
            {
                testDevice?.Dispose();
                Console.WriteLine($"Using adapter: {desc.Description}");
                return adapter;
            }

            adapter.Dispose();
        }

        return null;
    }

    public void Shutdown()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _device?.Dispose();
        _factory?.Dispose();

        _device = null;
        _factory = null;
        _disposed = true;
    }
}
