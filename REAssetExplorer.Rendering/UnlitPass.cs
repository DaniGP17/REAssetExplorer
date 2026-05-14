using System;
using System.IO;
using SharpDX;
using SharpDX.Direct3D11;
using SharpDX.DXGI;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Unlit pass that displays albedo without lighting calculations
/// </summary>
public class UnlitPass : IDisposable
{
    private VertexShader? _vertexShader;
    private PixelShader? _pixelShader;
    private SamplerState? _samplerState;
    
    public void Initialize(D3D11Device device)
    {
        LoadShaders(device);
        CreateSamplerState(device);
    }
    
    private void LoadShaders(D3D11Device device)
    {
        var shadersFolder = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Shaders");
        
        var vsPath = Path.Combine(shadersFolder, "Unlit_VS.cso");
        var psPath = Path.Combine(shadersFolder, "Unlit_PS.cso");
        
        if (!File.Exists(vsPath) || !File.Exists(psPath))
        {
            Console.WriteLine($"[UnlitPass] Warning: Shaders not found at {vsPath} / {psPath}");
            return;
        }
        
        try
        {
            var vsBytes = File.ReadAllBytes(vsPath);
            var psBytes = File.ReadAllBytes(psPath);
            
            _vertexShader = new VertexShader(device.Device, vsBytes);
            _pixelShader = new PixelShader(device.Device, psBytes);
            
            Console.WriteLine("[UnlitPass] Shaders loaded successfully");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[UnlitPass] Error loading shaders: {ex.Message}");
        }
    }
    
    private void CreateSamplerState(D3D11Device device)
    {
        var samplerDesc = new SamplerStateDescription
        {
            Filter = Filter.MinMagMipLinear,
            AddressU = TextureAddressMode.Clamp,
            AddressV = TextureAddressMode.Clamp,
            AddressW = TextureAddressMode.Clamp,
            MipLodBias = 0,
            MaximumAnisotropy = 1,
            ComparisonFunction = Comparison.Never,
            BorderColor = new Color4(0, 0, 0, 0),
            MinimumLod = 0,
            MaximumLod = float.MaxValue
        };
        
        _samplerState = new SamplerState(device.Device, samplerDesc);
    }
    
    public void ExecuteUnlitPass(D3D11Device device)
    {
        if (_vertexShader == null || _pixelShader == null)
        {
            Console.WriteLine("[UnlitPass] Shaders not loaded, skipping unlit pass");
            return;
        }
        
        var context = device.Context;
        
        // Set shaders
        context.VertexShader.Set(_vertexShader);
        context.PixelShader.Set(_pixelShader);
        context.GeometryShader.Set(null);
        context.HullShader.Set(null);
        context.DomainShader.Set(null);
        
        // Bind GBuffer1 (albedo) as texture
        context.PixelShader.SetShaderResource(0, device.GBuffer1SRV);
        
        // Bind sampler
        context.PixelShader.SetSampler(0, _samplerState);
        
        // Set render state for fullscreen quad
        context.InputAssembler.InputLayout = null;
        context.InputAssembler.PrimitiveTopology = SharpDX.Direct3D.PrimitiveTopology.TriangleList;
        
        // Disable depth testing for fullscreen quad
        var depthStencilDesc = new DepthStencilStateDescription
        {
            IsDepthEnabled = false,
            DepthWriteMask = DepthWriteMask.Zero,
            DepthComparison = Comparison.Always,
            IsStencilEnabled = false
        };
        
        using (var depthState = new DepthStencilState(device.Device, depthStencilDesc))
        {
            context.OutputMerger.SetDepthStencilState(depthState);
            
            // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
            context.Draw(3, 0);
        }
        
        // Unbind resources
        context.PixelShader.SetShaderResource(0, null);
    }
    
    public void Dispose()
    {
        _vertexShader?.Dispose();
        _pixelShader?.Dispose();
        _samplerState?.Dispose();
    }
}
