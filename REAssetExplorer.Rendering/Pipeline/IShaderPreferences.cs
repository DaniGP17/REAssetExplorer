using System.Collections.Generic;
using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Rendering.Pipeline;

/// <summary>
/// Define las preferencias de selección de shaders para un juego específico
/// </summary>
public interface IShaderPreferences
{
    /// <summary>
    /// Obtiene una lista ordenada de patrones de nombres de shaders preferidos para un tipo de pass
    /// Los patrones se intentan en orden de preferencia
    /// </summary>
    /// <param name="pass">El tipo de render pass</param>
    /// <param name="shaderType">El tipo de shader del material</param>
    /// <returns>Lista de patrones a buscar en orden de preferencia</returns>
    IEnumerable<string> GetPreferredShaderPatterns(RenderPass pass, ShaderType shaderType);
    
    /// <summary>
    /// Nombre del juego (para logging)
    /// </summary>
    string GameName { get; }
}
