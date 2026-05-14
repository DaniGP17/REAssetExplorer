using System;
using System.Collections.Generic;
using System.Linq;
using SharpDX.Direct3D11;
using REAssetExplorer.Core.Assets.Models;
using SharpDX.D3DCompiler;

namespace REAssetExplorer.Rendering.Pipeline;

/// <summary>
/// Construye pipelines de renderizado completos a partir de materiales y SDFs
/// </summary>
public class PipelineBuilder
{
    private class BuilderRequirements
    {
        public MaterialData? MaterialData { get; set; }
        public int MaterialIndex { get; set; }
        public MeshData? MeshData { get; set; }
    }
    
    private readonly D3D11Device _device;
    private readonly Dictionary<string, RenderPipeline> _pipelineCache = new();
    private readonly IShaderPreferences? _shaderPreferences;
    private BuilderRequirements _currentRequirements = new BuilderRequirements();

    private bool IsGlobalResource(string name)
    {
        string[] globalKeywords =
        {
            "BlueNoise16",
            "AutomaticWrap",
            "SceneInfo",
            "GBufferType",
            "CheckerBoardInfo",
            "InstanceWorldInfo",
            "IBLCubemapArrayList2SRV",
            "LightParameterSRV",
            "ShadowParameterSRV",
            "AreaLightParameterSRV",
            "LightCullingVolumeSRV",
            "LightCullingListSRV",
            "AmbientBRDF",
            "OutdoorProbesSRV",
            "IBLCubemap2DArraySRV",
            "CubemapSRV",
            "ShadowMapSRV",
            "IESLightTableSRV",
            "LTC1",
            "LTC2",
            "SSAOResult"
        };
        return globalKeywords.Any(k => name.Contains(k));
    }
    
    public PipelineBuilder(D3D11Device device, IShaderPreferences? shaderPreferences = null)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
        _shaderPreferences = shaderPreferences;
        
        if (_shaderPreferences != null)
        {
            Console.WriteLine($"[PipelineBuilder] Usando preferencias de shaders para: {_shaderPreferences.GameName}");
        }
    }
    
    /// <summary>
    /// Construye o recupera del cache un pipeline completo para un material
    /// </summary>
    public RenderPipeline BuildPipeline(MaterialData materialData, int materialIndex, SdfData sdfData, MeshData meshData)
    {
        _currentRequirements = new BuilderRequirements
        {
            MaterialData = materialData,
            MaterialIndex = materialIndex,
            MeshData = meshData
        };
        var materialHeader = materialData.MaterialHeaders[materialIndex];
        string cacheKey = $"{materialHeader.MasterMaterialFilePath}_{materialHeader.ShaderType}_{materialHeader.Flags}";
        
        if (_pipelineCache.TryGetValue(cacheKey, out var cachedPipeline))
        {
            Console.WriteLine($"[PipelineBuilder] ✓ Pipeline recuperado del cache: {cacheKey}");
            return cachedPipeline;
        }
        
        Console.WriteLine($"[PipelineBuilder] Construyendo pipeline para: {materialHeader.MaterialName}");
        Console.WriteLine($"  ShaderType: {materialHeader.ShaderType}");
        Console.WriteLine($"  Flags: {materialHeader.Flags}");
        
        var pipeline = new RenderPipeline
        {
            ShaderType = materialHeader.ShaderType,
            MaterialFlags = materialHeader.Flags
        };
        
        // 1. Seleccionar shaders apropiados del SDF para cada pass
        pipeline.Shaders = SelectShadersForPasses(pipeline, sdfData, materialData, materialIndex);
        
        // 2. Crear blend state
        pipeline.BlendState = CreateBlendState(materialHeader);
        
        // 3. Crear depth/stencil state
        pipeline.DepthStencilState = CreateDepthStencilState(materialHeader);
        
        // 4. Crear rasterizer state
        pipeline.RasterizerState = CreateRasterizerState(materialHeader);
        
        // 5. Crear sampler states
        CreateSamplerStates(pipeline);
        
        // 6. Almacenar información de constant buffers
        if (pipeline.Shaders.Count > 0)
        {
            var firstShaderProgram = pipeline.Shaders.Values.First();
            // Aquí podrías extraer la info de constant buffers del SDF si la necesitas
        }
        
        _pipelineCache[cacheKey] = pipeline;
        Console.WriteLine($"[PipelineBuilder] ✓ Pipeline construido y cacheado");
        
        return pipeline;
    }
    
    private Dictionary<RenderPass, Shader> SelectShadersForPasses(RenderPipeline pipeline, SdfData sdfData, MaterialData materialData, int materialIdx)
    {
        Console.WriteLine($"[PipelineBuilder] Analizando {sdfData.ShaderPrograms.Count} shader programs...");
        
        var shaders = new Dictionary<RenderPass, Shader>();
        var result = new Dictionary<RenderPass, ShaderProgramData>();
        
        if (_shaderPreferences != null)
        {
            Console.WriteLine($"[PipelineBuilder] Usando preferencias de {_shaderPreferences.GameName}...");
            result = SelectShadersWithPreferences(pipeline, sdfData, materialData, materialIdx);
        }
        
        // TODO: search for remaining shaders that match passes but weren't selected by preferences, as a fallback
        /*foreach (var program in sdfData.ShaderPrograms)
        {
            var passType = DeterminePassType(program.name, materialData.MaterialHeaders[materialIdx].ShaderType);
            
            if (passType.HasValue && !pipeline.Shaders.ContainsKey(passType.Value))
            {
                if (program.VertexShaderData == null || program.VertexShaderData.Length == 0)
                {
                    skippedPrograms++;
                    continue;
                }
                
                if (program.PixelShaderData == null || program.PixelShaderData.Length == 0)
                {
                    skippedPrograms++;
                    continue;
                }
                
                try
                {
                    var shader = new Shader();
                    shader.LoadFromBytes(_device, program.VertexShaderData, program.PixelShaderData);
                    
                    pipeline.Shaders[passType.Value] = shader;
                    Console.WriteLine($"  ✓ {passType.Value}: {program.name} (VS: {program.VertexShaderData.Length} bytes, PS: {program.PixelShaderData.Length} bytes)");
                    validPrograms++;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  ✗ Error creando shader '{program.name}': {ex.Message}");
                    skippedPrograms++;
                }
            }
        }*/
        
        foreach (var kvp in result)
        {
            var renderPass = kvp.Key;
            var shaderProgram = kvp.Value;

            try
            {
                var shader = new Shader();
                shader.LoadFromBytes(_device, shaderProgram.VertexShaderData, shaderProgram.PixelShaderData, shaderProgram.InputLayout);
                pipeline.Shaders[renderPass] = shader;
                pipeline.ShaderPrograms[renderPass] = shaderProgram;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error creating shader '{shaderProgram.name}' for pass {renderPass}: {ex.Message}");
            }
        }
        
        return pipeline.Shaders;
    }
    
    /// <summary>
    /// Selecciona shaders usando las preferencias específicas del juego
    /// </summary>
    private Dictionary<RenderPass, ShaderProgramData> SelectShadersWithPreferences(RenderPipeline pipeline, SdfData sdfData, MaterialData materialData, int materialIdx)
    {
        var result = new Dictionary<RenderPass, ShaderProgramData>();
        var passTypes = new[] 
        { 
            RenderPass.ShadowPass, 
            RenderPass.ZPrePass, 
            RenderPass.GBuffer, 
            RenderPass.Forward, 
            RenderPass.Lighting, 
            RenderPass.Transparent, 
            RenderPass.Distortion, 
            RenderPass.PostProcess 
        };
        
        foreach (var pass in passTypes)
        {
            var patterns = _shaderPreferences!.GetPreferredShaderPatterns(pass, materialData.MaterialHeaders[materialIdx].ShaderType);
            
            foreach (var pattern in patterns)
            {
                int programIndex = sdfData.ShaderPrograms.FindIndex(p =>
                    p.name.IndexOf(pattern, StringComparison.OrdinalIgnoreCase) >= 0 &&
                    p.VertexShaderData?.Length > 0 &&
                    p.PixelShaderData?.Length > 0
                );

                if (programIndex != -1)
                {
                    var matchingProgram = sdfData.ShaderPrograms[programIndex];
                    if (IsShaderValid(programIndex, matchingProgram, pass, materialData, materialIdx))
                    {
                        result.Add(pass, matchingProgram);
                    }
                }
            }
        }
        
        return result;
    }

    private bool IsShaderValid(int programIndex, ShaderProgramData program, RenderPass pass, MaterialData materialData, int materialIdx)
    {
        switch (pass)
        {
            case RenderPass.ShadowPass:
            case RenderPass.ZPrePass:
            case RenderPass.GBuffer:
            case RenderPass.Forward:
            case RenderPass.Transparent:
            case RenderPass.Distortion:
                if (program.VertexShaderData == null || program.VertexShaderData.Length == 0
                    || program.PixelShaderData == null || program.PixelShaderData.Length == 0)
                    return false;
                break;
            default:
                Console.WriteLine($"Can't validate requirements for pass {pass}");
                break;
        }
        
        try
        {
            for (int i = 0; i < program.SrvBindings.Count; i++)
            {
                var (srvInfo, srvHandle) = program.SrvBindings[i];
                if (IsGlobalResource(srvInfo.Name))
                {
                    continue;
                }
                
                // Skinning
                if (_currentRequirements.MeshData != null && 
                    _currentRequirements.MeshData.IsSkinning != program.name.Contains("Skinning", StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }
                
                // Alpha
                if (materialData.MaterialHeaders[materialIdx].Flags.HasFlag(MaterialFlags.BaseAlphaTestEnable) != program.name.Contains("Clip", StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }

                // Check if the material has the required texture for this shader resource
                if (!(materialData.TextureHeaders[materialIdx].Any(t => t.TextureType == program.SrvBindings[i].Item1.Name)))
                {
                    Console.WriteLine("Cannot find required texture '" + program.SrvBindings[i].Item1.Name + "' for shader '" + program.name + "' in material '" + materialData.MaterialHeaders[materialIdx].MaterialName + "'");
                    return false;
                }
            }
            
            // Check if the mesh vertex buffer contains the required input attribute for this shader resource
            if (_currentRequirements.MeshData == null)
            {
                Console.WriteLine("MeshData is null in validation");
                return false;
            }
            
            var meshBufferTypes = new HashSet<VertexElementSlots>();
            meshBufferTypes.UnionWith(_currentRequirements.MeshData.MeshBuffer.Elements.Select(vb => vb.InputSlot));
            
            
            for (int i = 0; i < program.InputLayout.Elements.Count; i++)
            {
                var inputElement = program.InputLayout.Elements[i];
                if (inputElement.SemanticType == SemanticType.Position)
                {
                    if (!meshBufferTypes.Contains(VertexElementSlots.Position))
                    {
                        Console.WriteLine("Shader '" + program.name + "' requires POSITION semantic but mesh does not have it.");
                        return false;
                    }
                    continue;
                }
                
                if (inputElement.SemanticType == SemanticType.Normal)
                {
                    if (!meshBufferTypes.Contains(VertexElementSlots.Normal))
                    {
                        Console.WriteLine("Shader '" + program.name + "' requires NORMAL semantic but mesh does not have it.");
                        return false;
                    }
                    continue;
                }
                
                if (inputElement.SemanticType == SemanticType.Tangent) // Normal includes tangent packed in
                {
                    if (!meshBufferTypes.Contains(VertexElementSlots.Normal))
                    {
                        Console.WriteLine("Shader '" + program.name + "' requires TANGENT semantic but mesh does not have it.");
                        return false;
                    }
                    continue;
                }
                
                if (inputElement.SemanticType == SemanticType.Texcoord)
                {
                    if (!meshBufferTypes.Contains(VertexElementSlots.Uv0))
                    {
                        Console.WriteLine("Shader '" + program.name + "' requires TEXCOORD semantic but mesh does not have it.");
                        return false;
                    }
                    continue;
                }
                
                Console.WriteLine("Unhandled shader input semantic in validation: " + inputElement.SemanticType + " for program " + program.name);
                return false;
            }
            
            Console.WriteLine("Valid shader found: " + program.name + " for pass " + pass);
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Shader reflection failed: {ex.Message}");
            Console.WriteLine("-----------------\n");
            return false;
        }
    }
    
    /// <summary>
    /// Determina el tipo de pass basándose en el nombre del shader program
    /// </summary>
    private RenderPass? DeterminePassType(string programName, ShaderType shaderType)
    {
        var nameLower = programName.ToLowerInvariant();
        
        // Shadow passes
        if (nameLower.Contains("shadow"))
            return RenderPass.ShadowPass;
        
        // Z-PrePass
        if (nameLower.Contains("zprepass") || nameLower.Contains("z_prepass") || nameLower.Contains("depthwrite"))
            return RenderPass.ZPrePass;
        
        // GBuffer (deferred rendering)
        if (nameLower.Contains("gbuffer"))
            return RenderPass.GBuffer;
        
        // Forward rendering
        if (nameLower.Contains("forward") || nameLower.Contains("default"))
            return RenderPass.Forward;
        
        // Lighting
        if (nameLower.Contains("lighting") || nameLower.Contains("light"))
            return RenderPass.Lighting;
        
        // Transparent
        if (nameLower.Contains("transparent") || nameLower.Contains("alpha"))
        {
            if (shaderType == ShaderType.Transparent || shaderType == ShaderType.ExpensiveTransparent)
                return RenderPass.Transparent;
        }
        
        // Distortion
        if (nameLower.Contains("distortion"))
            return RenderPass.Distortion;
        
        // Post-process
        if (nameLower.Contains("postprocess") || nameLower.Contains("post_process"))
            return RenderPass.PostProcess;
        
        // Si no coincide con ningún patrón conocido, retornar null
        return null;
    }
    
    /// <summary>
    /// Usa un shader por defecto si no se encuentra ninguno apropiado
    /// </summary>
    private void UseFallbackShader(RenderPipeline pipeline, SdfData sdfData)
    {
        // Tomar el primer shader program con VS y PS válidos
        var fallbackProgram = sdfData.ShaderPrograms
            .FirstOrDefault(p => p.VertexShaderData?.Length > 0 && p.PixelShaderData?.Length > 0);
        
        if (fallbackProgram.VertexShaderData != null && fallbackProgram.PixelShaderData != null)
        {
            try
            {
                var shader = new Shader();
                shader.LoadFromBytes(_device, fallbackProgram.VertexShaderData, fallbackProgram.PixelShaderData);
                pipeline.Shaders[RenderPass.Forward] = shader;
                Console.WriteLine($"  ✓ Usando shader fallback: {fallbackProgram.name} (VS: {fallbackProgram.VertexShaderData.Length} bytes, PS: {fallbackProgram.PixelShaderData.Length} bytes)");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Error creando shader fallback: {ex.Message}");
            }
        }
        else
        {
            Console.WriteLine($"  ✗ No se encontró ningún shader program válido en el SDF");
        }
    }
    
    private BlendState CreateBlendState(MaterialHeaderData materialHeader)
    {
        var blendDesc = new BlendStateDescription();
        blendDesc.RenderTarget[0] = new RenderTargetBlendDescription();
        
        bool needsBlending = 
            materialHeader.ShaderType == ShaderType.Transparent ||
            materialHeader.ShaderType == ShaderType.ExpensiveTransparent ||
            materialHeader.ShaderType == ShaderType.GuiMeshTransparent ||
            (materialHeader.Flags & MaterialFlags.AlphaTestEnable) != 0;
        
        if (needsBlending)
        {
            blendDesc.RenderTarget[0].IsBlendEnabled = true;
            blendDesc.RenderTarget[0].SourceBlend = BlendOption.SourceAlpha;
            blendDesc.RenderTarget[0].DestinationBlend = BlendOption.InverseSourceAlpha;
            blendDesc.RenderTarget[0].BlendOperation = BlendOperation.Add;
            blendDesc.RenderTarget[0].SourceAlphaBlend = BlendOption.One;
            blendDesc.RenderTarget[0].DestinationAlphaBlend = BlendOption.Zero;
            blendDesc.RenderTarget[0].AlphaBlendOperation = BlendOperation.Add;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = ColorWriteMaskFlags.All;
            
            Console.WriteLine($"Blend State: Alpha Blending enabled");
        }
        else
        {
            blendDesc.RenderTarget[0].IsBlendEnabled = false;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = ColorWriteMaskFlags.All;
            
            Console.WriteLine($"Blend State: Without blending");
        }
        
        return new BlendState(_device.Device, blendDesc);
    }
    
    /// <summary>
    /// Crea depth/stencil state según las propiedades del material
    /// </summary>
    private DepthStencilState CreateDepthStencilState(MaterialHeaderData materialHeader)
    {
        var depthDesc = new DepthStencilStateDescription
        {
            IsDepthEnabled = true,
            DepthComparison = Comparison.Less,
            IsStencilEnabled = false
        };
        
        // Materiales transparentes no escriben en depth (pero sí leen)
        if (materialHeader.ShaderType == ShaderType.Transparent ||
            materialHeader.ShaderType == ShaderType.ExpensiveTransparent)
        {
            depthDesc.DepthWriteMask = DepthWriteMask.Zero;
        }
        else
        {
            depthDesc.DepthWriteMask = DepthWriteMask.All;
        }
        
        if ((materialHeader.Flags & MaterialFlags.EnableIgnoreDepth) != 0)
        {
            depthDesc.IsDepthEnabled = false;
        }
        
        return new DepthStencilState(_device.Device, depthDesc);
    }
    
    /// <summary>
    /// Crea rasterizer state según las propiedades del material
    /// </summary>
    private RasterizerState CreateRasterizerState(MaterialHeaderData materialHeader)
    {
        var rasterDesc = new RasterizerStateDescription
        {
            FillMode = FillMode.Solid,
            IsFrontCounterClockwise = true,  // Coincidir con Renderer
            DepthBias = 0,
            DepthBiasClamp = 0.0f,
            SlopeScaledDepthBias = 0.0f,
            IsDepthClipEnabled = true,
            IsScissorEnabled = false,
            IsMultisampleEnabled = false,
            IsAntialiasedLineEnabled = false
        };
        
        // Two-sided rendering (no culling)
        bool isTwoSided = 
            (materialHeader.Flags & MaterialFlags.BaseTwoSideEnable) != 0 ||
            (materialHeader.Flags & MaterialFlags.TwoSideEnable) != 0 ||
            (materialHeader.Flags & MaterialFlags.ForcedTwoSideEnable) != 0;
        
        // TEMPORAL: Desactivar culling para debugging
        rasterDesc.CullMode = CullMode.None;
        Console.WriteLine($"  ✓ Rasterizer State: No culling (DEBUG)");
        
        /*if (isTwoSided)
        {
            rasterDesc.CullMode = CullMode.None;
            Console.WriteLine($"  ✓ Rasterizer State: Two-sided (no culling)");
        }
        else
        {
            rasterDesc.CullMode = CullMode.Back;
            Console.WriteLine($"  ✓ Rasterizer State: Back-face culling");
        }*/
        
        return new RasterizerState(_device.Device, rasterDesc);
    }
    
    /// <summary>
    /// Crea sampler states para texturas
    /// </summary>
    private void CreateSamplerStates(RenderPipeline pipeline)
    {
        // Sampler básico con linear filtering y wrap
        var samplerDesc = new SamplerStateDescription
        {
            Filter = Filter.MinMagMipLinear,
            AddressU = TextureAddressMode.Wrap,
            AddressV = TextureAddressMode.Wrap,
            AddressW = TextureAddressMode.Wrap,
            MipLodBias = 0,
            MaximumAnisotropy = 4,
            ComparisonFunction = Comparison.Always,
            BorderColor = new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0),
            MinimumLod = 0,
            MaximumLod = float.MaxValue
        };
        
        pipeline.SamplerStates.Add(new SamplerState(_device.Device, samplerDesc));
        Console.WriteLine($"  ✓ Sampler States: 1 sampler creado");
    }
    
    /// <summary>
    /// Limpia el cache de pipelines
    /// </summary>
    public void ClearCache()
    {
        foreach (var pipeline in _pipelineCache.Values)
        {
            pipeline.Dispose();
        }
        _pipelineCache.Clear();
        Console.WriteLine("[PipelineBuilder] Cache limpiado");
    }
    
    public void Dispose()
    {
        ClearCache();
    }
}
