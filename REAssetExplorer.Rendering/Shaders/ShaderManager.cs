using System;
using System.Collections.Generic;

namespace REAssetExplorer.Rendering.Shaders;

/// <summary>
/// Gestor simple de shaders - carga, cachea y proporciona acceso a shaders
/// </summary>
public class ShaderManager : IDisposable
{
    private readonly D3D11Device _device;
    private readonly Dictionary<string, Shader> _shaderCache = new();

    public ShaderManager(D3D11Device device)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
    }

    /// <summary>
    /// Obtiene o carga un shader por nombre
    /// </summary>
    /// <param name="vertexShaderName">Nombre del vertex shader (sin extensión)</param>
    /// <param name="pixelShaderName">Nombre del pixel shader (sin extensión)</param>
    public Shader GetOrLoadShader(string vertexShaderName, string pixelShaderName)
    {
        string key = $"{vertexShaderName}+{pixelShaderName}";
        
        if (_shaderCache.TryGetValue(key, out var cachedShader))
        {
            return cachedShader;
        }

        var shader = new Shader();
        shader.LoadShaders(_device, vertexShaderName, pixelShaderName);
        _shaderCache[key] = shader;
        
        Console.WriteLine($"[ShaderManager] Shader cargado: {vertexShaderName} + {pixelShaderName}");
        return shader;
    }

    /// <summary>
    /// Carga un shader desde bytes compilados
    /// </summary>
    public Shader LoadFromBytes(string name, byte[] vertexShaderBytes, byte[] pixelShaderBytes)
    {
        if (_shaderCache.TryGetValue(name, out var cachedShader))
        {
            return cachedShader;
        }

        var shader = new Shader();
        shader.LoadFromBytes(_device, vertexShaderBytes, pixelShaderBytes);
        _shaderCache[name] = shader;
        
        Console.WriteLine($"[ShaderManager] Shader cargado desde bytes: {name}");
        return shader;
    }

    /// <summary>
    /// Limpia todos los shaders cargados
    /// </summary>
    public void ClearCache()
    {
        foreach (var shader in _shaderCache.Values)
        {
            shader.Dispose();
        }
        _shaderCache.Clear();
        Console.WriteLine("[ShaderManager] Cache limpiado");
    }

    public void Dispose()
    {
        ClearCache();
    }
}
