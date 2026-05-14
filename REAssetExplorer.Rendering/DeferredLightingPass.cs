using SharpDX.Direct3D11;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Helper for deferred rendering lighting pass
/// </summary>
public class DeferredLightingPass : System.IDisposable
{
    private Shader? _lightingShader;
    private SamplerState? _pointSampler;
    private DepthStencilState? _noDepthState;
    
    public void Initialize(D3D11Device device)
    {
        _lightingShader = new Shader();
        _lightingShader.LoadShaders(device, "DeferredLighting_VS", "DeferredLighting_PS");
        
        // Create point sampler for G-Buffer sampling
        var samplerDesc = new SamplerStateDescription
        {
            Filter = Filter.MinMagMipPoint,
            AddressU = TextureAddressMode.Clamp,
            AddressV = TextureAddressMode.Clamp,
            AddressW = TextureAddressMode.Clamp,
            MipLodBias = 0,
            MaximumAnisotropy = 1,
            ComparisonFunction = Comparison.Never,
            BorderColor = new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0),
            MinimumLod = 0,
            MaximumLod = float.MaxValue
        };
        _pointSampler = new SamplerState(device.Device, samplerDesc);
        
        // Create depth stencil state with depth testing disabled
        var depthStencilDesc = new DepthStencilStateDescription
        {
            IsDepthEnabled = false,
            DepthWriteMask = DepthWriteMask.Zero,
            DepthComparison = Comparison.Always,
            IsStencilEnabled = false
        };
        _noDepthState = new DepthStencilState(device.Device, depthStencilDesc);
    }
    
    public void ExecuteLightingPass(D3D11Device device)
    {
        if (_lightingShader == null || _pointSampler == null)
            return;
        
        var context = device.Context;
        
        // Disable depth testing for fullscreen pass
        context.OutputMerger.SetDepthStencilState(_noDepthState);
        
        // Set shaders
        context.VertexShader.Set(_lightingShader.VertexShader);
        context.PixelShader.Set(_lightingShader.PixelShader);
        
        // Fullscreen triangle doesn't need InputLayout (vertices generated in VS)
        context.InputAssembler.InputLayout = null;
        
        // Bind G-Buffers as shader resources
        var srvs = new ShaderResourceView[] 
        { 
            device.GBuffer0SRV!,
            device.GBuffer1SRV!,
            device.GBuffer2SRV!,
            device.GBuffer3SRV!
        };
        context.PixelShader.SetShaderResources(0, srvs);
        context.PixelShader.SetSampler(0, _pointSampler);
        
        // No vertex/index buffers needed - fullscreen triangle generated in VS
        // Don't call SetVertexBuffers with null - just leave previous bindings
        
        // Draw fullscreen triangle (3 vertices)
        context.Draw(3, 0);
        
        // Unbind shader resources to avoid hazards
        context.PixelShader.SetShaderResources(0, new ShaderResourceView?[4]);
    }
    
    public void Dispose()
    {
        _lightingShader?.Dispose();
        _pointSampler?.Dispose();
        _noDepthState?.Dispose();
    }
}
