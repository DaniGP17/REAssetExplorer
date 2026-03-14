using System.Collections.Generic;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Rendering.Pipeline;

namespace REAssetExplorer.Games.RE8;

/// <summary>
/// Preferencias de shaders específicas para Resident Evil 8 Village
/// Define qué shader programs del SDF usar preferentemente para cada render pass
/// </summary>
public class RE8ShaderPreferences : IShaderPreferences
{
    public string GameName => "RE8";
    
    public IEnumerable<string> GetPreferredShaderPatterns(RenderPass pass, ShaderType shaderType)
    {
        // RE8 puede tener diferentes convenciones de nombres que RE7
        switch (pass)
        {
            case RenderPass.GBuffer:
                yield return "GBuffer";
                yield return "Deferred";
                yield return "MainGeometry";
                yield return "BasePass";
                break;
                
            case RenderPass.Forward:
                yield return "Forward";
                yield return "ForwardRendering";
                yield return "Standard";
                yield return "Default";
                break;
                
            case RenderPass.ShadowPass:
                yield return "Shadow";
                yield return "ShadowCast";
                yield return "ShadowMap";
                yield return "DepthOnly";
                break;
                
            case RenderPass.ZPrePass:
                yield return "ZPrePass";
                yield return "Z_PrePass";
                yield return "EarlyZ";
                yield return "DepthPrepass";
                break;
                
            case RenderPass.Transparent:
                if (shaderType == ShaderType.Transparent || shaderType == ShaderType.ExpensiveTransparent)
                {
                    yield return "Transparent";
                    yield return "TransparentPass";
                    yield return "Alpha";
                    yield return "Translucent";
                }
                break;
                
            case RenderPass.Lighting:
                yield return "Lighting";
                yield return "DeferredLighting";
                yield return "LightPass";
                yield return "Shading";
                break;
                
            case RenderPass.Distortion:
                yield return "Distortion";
                yield return "Refraction";
                yield return "HeatHaze";
                break;
                
            case RenderPass.PostProcess:
                yield return "PostProcess";
                yield return "PostFX";
                yield return "Post";
                break;
                
            default:
                yield break;
        }
    }
}
