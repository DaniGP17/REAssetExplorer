using System;
using System.Runtime.InteropServices;
using SharpDX.Direct3D11;
using Buffer = SharpDX.Direct3D11.Buffer;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Tone mapping pass that converts HDR to LDR
/// </summary>
public class ToneMappingPass : IDisposable
{
    private Shader? _toneMappingShader;
    private SamplerState? _pointSampler;
    private Buffer? _constantBuffer;
    private DepthStencilState? _noDepthState;
    
    // Tone mapping parameters
    public float Exposure { get; set; } = 1.0f;
    public float Gamma { get; set; } = 2.2f;
    public ToneMappingMode Mode { get; set; } = ToneMappingMode.ACES;
    
    [StructLayout(LayoutKind.Sequential)]
    private struct ToneMappingConstants
    {
        public float Exposure;
        public float Gamma;
        public int ToneMappingMode;
        public float Padding;
    }
    
    public void Initialize(D3D11Device device)
    {
        _toneMappingShader = new Shader();
        _toneMappingShader.LoadShaders(device, "ToneMapping_VS", "ToneMapping_PS");
        
        // Create point sampler for HDR texture sampling
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
        
        // Create constant buffer for tone mapping parameters
        var bufferDesc = new BufferDescription
        {
            Usage = ResourceUsage.Default,
            SizeInBytes = Marshal.SizeOf<ToneMappingConstants>(),
            BindFlags = BindFlags.ConstantBuffer,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None,
            StructureByteStride = 0
        };
        _constantBuffer = new Buffer(device.Device, bufferDesc);
        
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
    
    public void ExecuteToneMappingPass(D3D11Device device)
    {
        if (_toneMappingShader == null || _pointSampler == null || _constantBuffer == null)
            return;
        
        var context = device.Context;
        
        // Disable depth testing for fullscreen pass
        context.OutputMerger.SetDepthStencilState(_noDepthState);
        
        // Update constant buffer
        var constants = new ToneMappingConstants
        {
            Exposure = Exposure,
            Gamma = Gamma,
            ToneMappingMode = (int)Mode,
            Padding = 0
        };
        
        context.UpdateSubresource(ref constants, _constantBuffer);
        
        // Set shaders
        context.VertexShader.Set(_toneMappingShader.VertexShader);
        context.PixelShader.Set(_toneMappingShader.PixelShader);
        
        // Fullscreen triangle doesn't need InputLayout (vertices generated in VS)
        context.InputAssembler.InputLayout = null;
        
        // Bind HDR texture as shader resource
        context.PixelShader.SetShaderResource(0, device.HDRShaderResourceView);
        context.PixelShader.SetSampler(0, _pointSampler);
        context.PixelShader.SetConstantBuffer(0, _constantBuffer);
        
        // Draw fullscreen triangle (3 vertices)
        context.Draw(3, 0);
        
        // Unbind shader resources to avoid hazards
        context.PixelShader.SetShaderResource(0, null);
    }
    
    public void Dispose()
    {
        _toneMappingShader?.Dispose();
        _pointSampler?.Dispose();
        _constantBuffer?.Dispose();
        _noDepthState?.Dispose();
    }
}

/// <summary>
/// Tone mapping modes
/// </summary>
public enum ToneMappingMode
{
    Reinhard = 0,
    ACES = 1,
    Uncharted2 = 2
}
