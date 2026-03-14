using System;
using System.Collections.Generic;
using REAssetExplorer.RenderTest2.Interop.Structures;

namespace REAssetExplorer.RenderTest2.Assets;

/// <summary>
/// Tipo de recurso
/// </summary>
public enum ResourceType
{
    Unknown,
    Texture,
    Mesh,
    Material,
    Shader
}

/// <summary>
/// Estado de carga del recurso
/// </summary>
public enum ResourceState
{
    Unloaded,      // No cargado
    Loading,       // En proceso de carga
    Loaded,        // Cargado exitosamente
    Failed         // Error al cargar
}

/// <summary>
/// Clase base para todos los recursos
/// </summary>
public abstract class Resource : IDisposable
{
    /// <summary>
    /// ID único del recurso (hash del path)
    /// </summary>
    public ulong Id { get; set; }

    /// <summary>
    /// Path del recurso en el PAK
    /// </summary>
    public string FilePath { get; set; } = string.Empty;

    /// <summary>
    /// Nombre del recurso
    /// </summary>
    public string Name { get; set; } = string.Empty;

    /// <summary>
    /// Tipo de recurso
    /// </summary>
    public abstract ResourceType Type { get; }

    /// <summary>
    /// Estado actual del recurso
    /// </summary>
    public ResourceState State { get; set; } = ResourceState.Unloaded;

    /// <summary>
    /// Referencias a otros recursos de los que depende
    /// </summary>
    public List<ulong> Dependencies { get; set; } = new();

    /// <summary>
    /// Tiempo de carga en ms (para debugging)
    /// </summary>
    public long LoadTimeMs { get; set; }

    /// <summary>
    /// Libera recursos no administrados
    /// </summary>
    public virtual void Dispose()
    {
        State = ResourceState.Unloaded;
        Dependencies.Clear();
    }
}

/// <summary>
/// Recurso de material
/// </summary>
public class MaterialResource : Resource
{
    public override ResourceType Type => ResourceType.Material;

    // Datos del material
    // TODO: Agregar estructuras específicas de material cuando las definas
    public object? MaterialData { get; set; }

    /// <summary>
    /// IDs de texturas que usa este material
    /// </summary>
    public List<ulong> TextureIds { get; set; } = new();

    public override void Dispose()
    {
        MaterialData = null;
        TextureIds.Clear();
        base.Dispose();
    }
}

/// <summary>
/// Recurso de shader
/// </summary>
public class ShaderResource : Resource
{
    public override ResourceType Type => ResourceType.Shader;

    /// <summary>
    /// Bytecode compilado del shader
    /// </summary>
    public byte[]? Bytecode { get; set; }

    /// <summary>
    /// Tipo de shader (VS, PS, etc.)
    /// </summary>
    public string ShaderType { get; set; } = string.Empty;

    public override void Dispose()
    {
        Bytecode = null;
        base.Dispose();
    }
}