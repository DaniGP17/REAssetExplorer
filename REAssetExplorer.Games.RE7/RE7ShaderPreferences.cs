using System.Collections.Generic;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Rendering.Pipeline;

namespace REAssetExplorer.Games.RE7;

/// <summary>
/// Preferencias de shaders específicas para Resident Evil 7
/// Define qué shader programs del SDF usar preferentemente para cada render pass
/// </summary>
public class RE7ShaderPreferences : IShaderPreferences
{
    public string GameName => "RE7";
    
    public IEnumerable<string> GetPreferredShaderPatterns(RenderPass pass, ShaderType shaderType)
    {
        switch (pass)
        {
            case RenderPass.GBuffer:
                yield return "DeferredStatic";
                yield return "DeferredStaticClip";
                yield return "DeferredSkinning";
                yield return "DeferredSkinningClip";
                break;
                
            case RenderPass.Forward:
                yield return "ForwardStaticLW";
                yield return "ForwardStaticLWClip";
                yield return "ForwardSkinningLW";
                yield return "ForwardSkinningLWClip";
                break;
                
            case RenderPass.ShadowPass:
                yield return "ShadowStatic";
                yield return "ShadowStaticClip";
                yield return "ShadowSkinning";
                yield return "ShadowSkinningClip";
                break;
                
            case RenderPass.ZPrePass:
                yield return "ZPrePassStatic";
                yield return "ZPrePassStaticClip";
                yield return "ZPrePassSkinning";
                yield return "ZPrePassSkinningClip";
                break;
                
            case RenderPass.Transparent:
                if (shaderType == ShaderType.Transparent || shaderType == ShaderType.ExpensiveTransparent)
                {
                    yield return "ForwardStaticLWClip";
                    yield return "ForwardSkinningLWClip";
                }
                break;
                
            default:
                yield break;
        }
    }
}
