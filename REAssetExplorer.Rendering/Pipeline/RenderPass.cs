namespace REAssetExplorer.Rendering.Pipeline;

/// <summary>
/// Tipos de passes de renderizado disponibles
/// </summary>
public enum RenderPass
{
    /// <summary>
    /// Shadow map rendering pass
    /// </summary>
    ShadowPass,
    
    /// <summary>
    /// Z-PrePass (early depth testing)
    /// </summary>
    ZPrePass,
    
    /// <summary>
    /// GBuffer pass (deferred rendering)
    /// </summary>
    GBuffer,
    
    /// <summary>
    /// Forward rendering pass (opaco)
    /// </summary>
    Forward,
    
    /// <summary>
    /// Lighting pass (deferred lighting)
    /// </summary>
    Lighting,
    
    /// <summary>
    /// Transparent objects pass
    /// </summary>
    Transparent,
    
    /// <summary>
    /// Distortion pass (heat haze, etc.)
    /// </summary>
    Distortion,
    
    /// <summary>
    /// Post-processing pass
    /// </summary>
    PostProcess,
    
    /// <summary>
    /// Debug/Wireframe pass
    /// </summary>
    Debug
}