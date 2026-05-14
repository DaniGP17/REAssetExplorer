using System;
using System.Collections.Generic;
using SharpDX.Direct3D11;
using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Rendering.Pipeline;

public class RenderPipeline : IDisposable
{
    public Dictionary<RenderPass, Shader> Shaders { get; set; } = new();
    
    /// <summary>
    /// Mapea cada RenderPass a su ShaderProgramData del SDF para acceso a bindings y recursos
    /// </summary>
    public Dictionary<RenderPass, ShaderProgramData> ShaderPrograms { get; set; } = new();
    
    public BlendState? BlendState { get; set; }
    
    public DepthStencilState? DepthStencilState { get; set; }
    
    public RasterizerState? RasterizerState { get; set; }
    
    public List<SamplerState> SamplerStates { get; set; } = new();
    public MaterialFlags MaterialFlags { get; set; }
    public ShaderType ShaderType { get; set; }
    public List<ShaderConstantBufferInfo> ConstantBuffers { get; set; } = new();
    
    public bool RequiresBlending => 
        ShaderType == ShaderType.Transparent || 
        ShaderType == ShaderType.ExpensiveTransparent ||
        ShaderType == ShaderType.GuiMeshTransparent ||
        (MaterialFlags & MaterialFlags.AlphaTestEnable) != 0;
    
    public bool IsTwoSided => 
        (MaterialFlags & MaterialFlags.BaseTwoSideEnable) != 0 ||
        (MaterialFlags & MaterialFlags.TwoSideEnable) != 0 ||
        (MaterialFlags & MaterialFlags.ForcedTwoSideEnable) != 0;
    
    public bool CastsShadows => 
        (MaterialFlags & MaterialFlags.ShadowCastDisable) == 0;
    
    public void Bind(DeviceContext context, RenderPass pass)
    {
        if (!Shaders.TryGetValue(pass, out var shader))
        {
            return;
        }
        
        context.VertexShader.Set(shader.VertexShader);
        context.PixelShader.Set(shader.PixelShader);
        context.InputAssembler.InputLayout = shader.InputLayout;
        
        if (BlendState != null)
        {
            context.OutputMerger.SetBlendState(BlendState);
        }
        
        if (DepthStencilState != null)
        {
            context.OutputMerger.SetDepthStencilState(DepthStencilState);
        }
        
        if (RasterizerState != null)
        {
            context.Rasterizer.State = RasterizerState;
        }
        
        for (int i = 0; i < SamplerStates.Count; i++)
        {
            context.PixelShader.SetSampler(i, SamplerStates[i]);
        }
    }
    
    public void Dispose()
    {
        foreach (var shader in Shaders.Values)
        {
            shader?.Dispose();
        }
        Shaders.Clear();
        
        BlendState?.Dispose();
        DepthStencilState?.Dispose();
        RasterizerState?.Dispose();
        
        foreach (var sampler in SamplerStates)
        {
            sampler?.Dispose();
        }
        SamplerStates.Clear();
    }
}
