using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Rendering.Shaders;
using REAssetExplorer.Rendering.Pipeline;
using SharpDX.Direct3D11;

namespace REAssetExplorer.Rendering;

public class MaterialInstance
{
    private static bool _firstBindLogged = false;
    private static SharpDX.Direct3D11.VertexShader? _cachedDebugVS = null;
    private static byte[]? _cachedDebugVSBytecode = null;
    private static SharpDX.Direct3D11.PixelShader? _cachedDebugPS = null;
    private static InputLayout? _cachedDebugInputLayout = null;
    
    // TEMPORAL: Para debugging
    public bool UseDebugDraw { get; set; } = false; // Usar DrawIndexed normal
    public bool UseDebugShaders { get; set; } = true; // ACTIVADO: Usar shaders debug
    
    public string MaterialName { get; set; } = string.Empty;
    
    public MaterialData? MaterialData { get; set; }
    public SdfData? SdfData { get; set; }
    
    /// <summary>
    /// [DEPRECATED] Usar RenderPipeline en su lugar
    /// </summary>
    [Obsolete("Usar RenderPipeline en su lugar")]
    public ShaderProgramData? SelectedShaderProgram { get; set; }
    
    public List<Texture?> Textures { get; set; } = new List<Texture?>();
    
    /// <summary>
    /// Pipeline de renderizado completo para este material
    /// </summary>
    public RenderPipeline? RenderPipeline { get; set; }
    
    // Material properties for UserMaterial constant buffer
    public Vector4 BaseColor { get; set; } = new Vector4(1, 1, 1, 1);
    public float Metallic { get; set; } = 0.0f;
    public float Roughness { get; set; } = 0.5f;
    public float Translucency { get; set; } = 0.0f;
    public float AlphaTestRef { get; set; } = 0.0f;  // TEMPORAL: Cambiado de 0.5 a 0.0 para debugging
    public float OccUVSelect { get; set; } = 0.0f;
    
    // GBuffer type flag
    public float GBufferTypeFlag { get; set; } = 0.0f;
    
    // Custom properties for shader-specific parameters not in standard buffer
    public Dictionary<string, object> CustomProperties { get; set; } = new Dictionary<string, object>();

    public MaterialInstance() { }
    
    /// <summary>
    /// Creates a UserMaterialBuffer from this material's properties
    /// </summary>
    public UserMaterialBuffer GetUserMaterialBuffer()
    {
        return new UserMaterialBuffer
        {
            BaseColor = new SharpDX.Vector4(BaseColor.X, BaseColor.Y, BaseColor.Z, BaseColor.W),
            Metallic = Metallic,
            Roughness = Roughness,
            Translucency = Translucency,
            AlphaTestRef = AlphaTestRef,
            OccUVSelect = OccUVSelect,
            Padding = SharpDX.Vector3.Zero
        };
    }
    
    /// <summary>
    /// Creates a GBufferTypeBuffer from this material's properties
    /// </summary>
    public GBufferTypeBuffer GetGBufferTypeBuffer()
    {
        return new GBufferTypeBuffer
        {
            GBufferTypeFlag = GBufferTypeFlag,
            Padding = SharpDX.Vector3.Zero
        };
    }
    
    /// <summary>
    /// Aplica el material completo: texturas, constant buffers y estados del pipeline
    /// </summary>
    public void Bind(D3D11Device device, RenderPass pass)
    {
        if (RenderPipeline == null)
        {
            Console.WriteLine($"[MaterialInstance] RenderPipeline is null for material '{MaterialName}'");
            return;
        }
        
        // Seleccionar shader del pass apropiado
        if (!RenderPipeline.Shaders.TryGetValue(pass, out var shader))
        {
            // Si no hay shader para este pass, intentar con GBuffer como fallback
            if (!RenderPipeline.Shaders.TryGetValue(RenderPass.GBuffer, out shader))
            {
                Console.WriteLine($"[MaterialInstance] No shader found for pass {pass} in material '{MaterialName}'");
                return;
            }
            if (!_firstBindLogged)
                Console.WriteLine($"[MaterialInstance.Bind] Using GBuffer shader as fallback for pass {pass}");
        }
        else
        {
            if (!_firstBindLogged)
                Console.WriteLine($"[MaterialInstance.Bind] Binding material '{MaterialName}' for pass {pass}");
        }
        
        // Activar vertex y pixel shader
        if (UseDebugShaders)
        {
            // TEMPORAL: Usar shaders de debug
            var (debugVS, debugVSBytecode) = CompileDebugVertexShader(device);
            var debugPS = CompileDebugPixelShader(device);
            var debugInputLayout = CreateDebugInputLayout(device, debugVSBytecode);
            
            device.Context.VertexShader.Set(debugVS);
            device.Context.PixelShader.Set(debugPS);
            device.Context.InputAssembler.InputLayout = debugInputLayout;
            
            if (!_firstBindLogged)
                Console.WriteLine($"[MaterialInstance.Bind] Using DEBUG shaders with matrix transforms");
        }
        else
        {
            device.Context.VertexShader.Set(shader.VertexShader);
            device.Context.PixelShader.Set(shader.PixelShader);
            
            // Aplicar input layout
            if (shader.InputLayout != null)
            {
                device.Context.InputAssembler.InputLayout = shader.InputLayout;
                if (!_firstBindLogged)
                    Console.WriteLine($"[MaterialInstance.Bind] InputLayout set successfully");
            }
            else
            {
                Console.WriteLine($"[MaterialInstance.Bind] WARNING: Shader has no InputLayout!");
            }
        }
        
        // Aplicar estados del pipeline
        if (!UseDebugShaders)
        {
            // Solo aplicar estados del pipeline si usamos shaders reales
            if (RenderPipeline.BlendState != null)
                device.Context.OutputMerger.SetBlendState(RenderPipeline.BlendState);
            
            if (RenderPipeline.DepthStencilState != null)
                device.Context.OutputMerger.SetDepthStencilState(RenderPipeline.DepthStencilState);
            
            if (RenderPipeline.RasterizerState != null)
                device.Context.Rasterizer.State = RenderPipeline.RasterizerState;
        }
        else
        {
            if (!_firstBindLogged)
                Console.WriteLine($"[MaterialInstance.Bind] Using debug mode - keeping default render states (no culling, depth enabled)");
        }
        
        BindShaderResources(device, pass);
        
        // Bindear samplers
        BindSamplers(device);
        
        if (!_firstBindLogged)
            Console.WriteLine($"[MaterialInstance.Bind] About to call BindMaterialConstants");
        
        // Bindear constant buffers del material (UserMaterial y GBufferType)
        // Nota: SceneInfo se bindea en el renderer porque contiene info de la cámara
        BindMaterialConstants(device);
    }
    
    private void BindShaderResources(D3D11Device device, RenderPass pass)
    {
        if (SdfData == null || RenderPipeline?.Shaders == null || MaterialData == null)
        {
            if (!_firstBindLogged)
                Console.WriteLine($"[BindShaderResources] Missing data: SDF={SdfData != null}, Shaders={RenderPipeline?.Shaders != null}, MatData={MaterialData != null}");
            return;
        }
        
        // Encontrar el shader program activo para este pass desde el RenderPipeline
        if (!RenderPipeline.ShaderPrograms.TryGetValue(pass, out var activeProgram))
        {
            if (!_firstBindLogged)
                Console.WriteLine($"[BindShaderResources] No ShaderProgram found for pass {pass}");
            return;
        }
        
        if (!_firstBindLogged)
            Console.WriteLine($"[BindShaderResources] Found ShaderProgram for pass {pass}, SrvBindings count: {activeProgram.SrvBindings.Count}");
        
        // Encontrar el índice del material
        int materialIdx = -1;
        for (int i = 0; i < MaterialData.MaterialHeaders.Length; i++)
        {
            if (MaterialData.MaterialHeaders[i].MaterialName == MaterialName)
            {
                materialIdx = i;
                break;
            }
        }
        
        if (materialIdx < 0 || materialIdx >= MaterialData.TextureHeaders.Length)
        {
            return;
        }
        
        var textureHeaders = MaterialData.TextureHeaders[materialIdx];
        
        // Agrupar SRV bindings por stage
        var vsBindings = new Dictionary<int, SharpDX.Direct3D11.ShaderResourceView?>(); // Vertex Shader
        var psBindings = new Dictionary<int, SharpDX.Direct3D11.ShaderResourceView?>(); // Pixel Shader
        
        foreach (var (srvInfo, srvHandle) in activeProgram.SrvBindings)
        {
            // Buscar textura del material que coincida con este binding
            SharpDX.Direct3D11.ShaderResourceView? srv = device.DummySRV;
            bool textureFound = false;
            
            for (int i = 0; i < textureHeaders.Length; i++)
            {
                if (textureHeaders[i].TextureType == srvInfo.Name)
                {
                    if (i < Textures.Count && Textures[i]?.ShaderResourceView != null)
                    {
                        srv = Textures[i]!.ShaderResourceView;
                        textureFound = true;
                    }
                    break;
                }
            }
            
            // Usar srvHandle.Slot directamente (DirectX 11 no usa register spaces)
            int slot = srvHandle.Slot;
            int stageBit = srvHandle.StageBit;
            
            // Vertex Shader (bit 0 = 0x01)
            if ((stageBit & 0x01) != 0)
            {
                vsBindings[slot] = srv;
            }
            
            // Pixel Shader (bit 4 = 0x10)
            if ((stageBit & 0x10) != 0)
            {
                psBindings[slot] = srv;
            }
            
            // TODO: Hull (0x02), Domain (0x04), Geometry (0x08), Compute (0x20)
        }
        
        // Bindear SRVs al Vertex Shader
        if (vsBindings.Count > 0)
        {
            int maxSlot = vsBindings.Keys.Max();
            var vsArray = new SharpDX.Direct3D11.ShaderResourceView?[maxSlot + 1];
            for (int i = 0; i <= maxSlot; i++)
                vsArray[i] = vsBindings.TryGetValue(i, out var srv) ? srv : device.DummySRV;
            
            device.Context.VertexShader.SetShaderResources(0, vsArray);
        }
        
        // Bindear SRVs al Pixel Shader
        if (psBindings.Count > 0)
        {
            int maxSlot = psBindings.Keys.Max();
            var psArray = new SharpDX.Direct3D11.ShaderResourceView?[maxSlot + 1];
            for (int i = 0; i <= maxSlot; i++)
                psArray[i] = psBindings.TryGetValue(i, out var srv) ? srv : device.DummySRV;
            
            device.Context.PixelShader.SetShaderResources(0, psArray);
            
            if (!_firstBindLogged)
                Console.WriteLine($"[BindShaderResources] Bound {psBindings.Count} textures to PS slots 0-{maxSlot}");
        }
        else
        {
            if (!_firstBindLogged)
                Console.WriteLine($"[BindShaderResources] No PS texture bindings");
        }
        
        _firstBindLogged = true;
    }
    
    private void BindSamplers(D3D11Device device)
    {
        if (RenderPipeline?.SamplerStates == null || RenderPipeline.SamplerStates.Count == 0)
            return;
        
        if (SdfData == null)
            return;
        
        // Por ahora, bindear samplers solo al Pixel Shader
        // TODO: Leer SamplerBindings del ShaderProgramData para bindear por stage
        var samplers = RenderPipeline.SamplerStates.ToArray();
        device.Context.PixelShader.SetSamplers(0, samplers);
        
        // Si hay samplers que necesiten ir al Vertex Shader, agregarlos aquí
        // basándose en el StageBit de cada SamplerBinding
    }
    
    private void BindMaterialConstants(D3D11Device device)
    {
        Console.WriteLine($"[BindMaterialConstants] START - _firstBindLogged={_firstBindLogged}, UserMaterialBuffer={device.UserMaterialBuffer != null}");
        
        if (device.UserMaterialBuffer == null)
        {
            Console.WriteLine($"[MaterialInstance] UserMaterialBuffer not created in D3D11Device");
            return;
        }
        
        try
        {
            Console.WriteLine($"[BindMaterialConstants] Binding PS constant buffers...");
            
            // PS Slot 0: RootConstant (16 bytes) - bind with zeros
            if (device.RootConstantBuffer != null)
            {
                var rootConstant = new Shaders.RootConstantBuffer();
                var dataBox = device.Context.MapSubresource(device.RootConstantBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
                SharpDX.Utilities.Write(dataBox.DataPointer, ref rootConstant);
                device.Context.UnmapSubresource(device.RootConstantBuffer, 0);
                device.Context.PixelShader.SetConstantBuffer(0, device.RootConstantBuffer);
            }
            
            Console.WriteLine($"[BindMaterialConstants] PS slot 0 bound (RootConstant)");
            
            // PS Slot 1: CheckerBoardInfo (16 bytes) - bind with zeros
            if (device.CheckerBoardInfoBuffer != null)
            {
                var checkerboard = new Shaders.CheckerBoardInfoBuffer();
                var dataBox = device.Context.MapSubresource(device.CheckerBoardInfoBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
                SharpDX.Utilities.Write(dataBox.DataPointer, ref checkerboard);
                device.Context.UnmapSubresource(device.CheckerBoardInfoBuffer, 0);
                device.Context.PixelShader.SetConstantBuffer(1, device.CheckerBoardInfoBuffer);
            }
            
            Console.WriteLine($"[BindMaterialConstants] PS slots 0-1 bound");
            
            // PS Slot 2: LightInfo (352 bytes) - bind with zeros
            if (device.LightInfoBuffer != null)
            {
                var lightInfo = new Shaders.LightInfoBuffer();
                var dataBox = device.Context.MapSubresource(device.LightInfoBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
                SharpDX.Utilities.Write(dataBox.DataPointer, ref lightInfo);
                device.Context.UnmapSubresource(device.LightInfoBuffer, 0);
                device.Context.PixelShader.SetConstantBuffer(2, device.LightInfoBuffer);
            }
            
            // PS Slot 3: OutdoorLightProbeParam (64 bytes) - bind with zeros
            if (device.OutdoorLightProbeParamBuffer != null)
            {
                var outdoorProbes = new Shaders.OutdoorLightProbeParamBuffer();
                var dataBox = device.Context.MapSubresource(device.OutdoorLightProbeParamBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
                SharpDX.Utilities.Write(dataBox.DataPointer, ref outdoorProbes);
                device.Context.UnmapSubresource(device.OutdoorLightProbeParamBuffer, 0);
                device.Context.PixelShader.SetConstantBuffer(3, device.OutdoorLightProbeParamBuffer);
            }
            
            // PS Slot 4: ShadowSamplingRotation - TODO: structured buffer (ResourceType: 3)
            
            // PS Slot 5: UserMaterial (48 bytes) - bind with material properties
            var userMaterialData = GetUserMaterialBuffer();
            
            Console.WriteLine($"[BindMaterialConstants] UserMaterial: BaseColor=({userMaterialData.BaseColor.X:F2},{userMaterialData.BaseColor.Y:F2},{userMaterialData.BaseColor.Z:F2},{userMaterialData.BaseColor.W:F2}), AlphaTestRef={userMaterialData.AlphaTestRef:F2}");
            
            var dataBox5 = device.Context.MapSubresource(device.UserMaterialBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
            SharpDX.Utilities.Write(dataBox5.DataPointer, ref userMaterialData);
            device.Context.UnmapSubresource(device.UserMaterialBuffer, 0);
            device.Context.PixelShader.SetConstantBuffer(5, device.UserMaterialBuffer);
            
            Console.WriteLine($"[BindMaterialConstants] All 6 PS constant buffers bound successfully");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[MaterialInstance] ERROR binding material constants: {ex.GetType().Name}: {ex.Message}");
            if (ex.StackTrace != null)
                Console.WriteLine($"  Stack: {ex.StackTrace.Split('\n')[0]}");
            throw;
        }
    }
    
    private (SharpDX.Direct3D11.VertexShader, byte[]) CompileDebugVertexShader(D3D11Device device)
    {
        // FORCE RECOMPILE - invalidate cache
        _cachedDebugVS?.Dispose();
        _cachedDebugVS = null;
        _cachedDebugVSBytecode = null;
        
        // Use actual SceneInfo cbuffer with matrices
        string vsCode = @"
cbuffer SceneInfo : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
    float4x4 WorldViewProjection;
    float4 CameraPosition;
    float4 ViewportSize;
};

struct VS_INPUT
{
    float3 Position : POSITION0;
    float4 Normal : NORMAL0;
    float4 Tangent : TANGENT0;
    float2 TexCoord0 : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    
    // Try direct WorldViewProjection multiplication (simpler)
    output.Position = mul(float4(input.Position, 1.0), WorldViewProjection);
    
    // Color = green if working
    output.Color = float3(0, 1, 0);
    
    return output;
}
";
        
        var compilationResult = SharpDX.D3DCompiler.ShaderBytecode.Compile(
            vsCode, "main", "vs_5_0", 
            SharpDX.D3DCompiler.ShaderFlags.None, 
            SharpDX.D3DCompiler.EffectFlags.None);
        
        if (compilationResult.HasErrors)
        {
            Console.WriteLine($"[CompileDebugVS] ERROR: {compilationResult.Message}");
            throw new Exception($"Vertex shader compilation failed: {compilationResult.Message}");
        }
        
        _cachedDebugVSBytecode = compilationResult.Bytecode;
        _cachedDebugVS = new SharpDX.Direct3D11.VertexShader(device.Device, compilationResult);
        
        // USE SHADER REFLECTION to verify cbuffer is present
        using (var reflection = new SharpDX.D3DCompiler.ShaderReflection(compilationResult.Bytecode))
        {
            Console.WriteLine($"[CompileDebugVS] Shader reflection:");
            Console.WriteLine($"  Constant buffers: {reflection.Description.ConstantBuffers}");
            
            for (int i = 0; i < reflection.Description.ConstantBuffers; i++)
            {
                var cbuffer = reflection.GetConstantBuffer(i);
                Console.WriteLine($"  CB[{i}]: Name='{cbuffer.Description.Name}', Size={cbuffer.Description.Size} bytes, Type={cbuffer.Description.Type}");
            }
            
            for (int i = 0; i < reflection.Description.BoundResources; i++)
            {
                var resource = reflection.GetResourceBindingDescription(i);
                if (resource.Type == SharpDX.D3DCompiler.ShaderInputType.ConstantBuffer)
                {
                    Console.WriteLine($"  Resource[{i}]: '{resource.Name}' bound to slot {resource.BindPoint}, BindCount={resource.BindCount}");
                }
            }
        }
        
        Console.WriteLine($"[CompileDebugVS] Debug vertex shader compiled");
        return (_cachedDebugVS, _cachedDebugVSBytecode);
    }
    
    private InputLayout CreateDebugInputLayout(D3D11Device device, byte[] vsBytecode)
    {
        // Return cached if available
        if (_cachedDebugInputLayout != null)
            return _cachedDebugInputLayout;
        
        // INTERLEAVED layout - todo en slot 0 para coincidir con VertexPosition struct
        // Position: Vector3 (12 bytes) offset 0
        // Normal: Vector4 (16 bytes) offset 12
        // Tangent: Vector4 (16 bytes) offset 28
        // TexCoord: Vector2 (8 bytes) offset 44
        // TexCoord2: Vector2 (8 bytes) offset 52
        var elements = new SharpDX.Direct3D11.InputElement[]
        {
            new SharpDX.Direct3D11.InputElement("POSITION", 0, SharpDX.DXGI.Format.R32G32B32_Float, 0, 0),
            new SharpDX.Direct3D11.InputElement("NORMAL", 0, SharpDX.DXGI.Format.R32G32B32A32_Float, 12, 0),
            new SharpDX.Direct3D11.InputElement("TANGENT", 0, SharpDX.DXGI.Format.R32G32B32A32_Float, 28, 0),
            new SharpDX.Direct3D11.InputElement("TEXCOORD", 0, SharpDX.DXGI.Format.R32G32_Float, 44, 0)
        };
        
        _cachedDebugInputLayout = new InputLayout(device.Device, vsBytecode, elements);
        Console.WriteLine($"[CreateDebugInputLayout] Created INTERLEAVED layout - 4 elements all in slot 0, total stride {VertexPosition.SizeInBytes} bytes");
        return _cachedDebugInputLayout;
    }
    
    private SharpDX.Direct3D11.PixelShader CompileDebugPixelShader(D3D11Device device)
    {
        // FORCE RECOMPILE - invalidate cache
        _cachedDebugPS?.Dispose();
        _cachedDebugPS = null;
        
        string psCode = @"
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR0;
};

struct PS_OUTPUT
{
    float4 GBuffer0 : SV_TARGET0;
    float4 GBuffer1 : SV_TARGET1;
    float4 GBuffer2 : SV_TARGET2;
    float4 GBuffer3 : SV_TARGET3;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.GBuffer0 = float4(0, 0, 0, 1);
    output.GBuffer1 = float4(input.Color, 1); // Use color from VS (contains testValue)
    output.GBuffer2 = float4(0, 0, 0, 1);
    output.GBuffer3 = float4(0, 0, 0, 1);
    return output;
}
";
        
        var bytecode = SharpDX.D3DCompiler.ShaderBytecode.Compile(psCode, "main", "ps_5_0", SharpDX.D3DCompiler.ShaderFlags.None);
        _cachedDebugPS = new SharpDX.Direct3D11.PixelShader(device.Device, bytecode);
        Console.WriteLine($"[CompileDebugPS] PS compiled - outputs VS color (testValue/1000 as red channel)");
        return _cachedDebugPS;
    }
}
