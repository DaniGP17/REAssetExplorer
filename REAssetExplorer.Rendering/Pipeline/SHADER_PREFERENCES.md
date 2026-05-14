# Sistema de Shader Preferences

## 🎯 Objetivo

Permitir que cada juego (RE7, RE8, etc.) defina sus propias preferencias de selección de shaders desde los archivos SDF, ya que diferentes juegos pueden usar diferentes convenciones de nombres para sus shader programs.

## 📁 Archivos Creados

### Core
- `IShaderPreferences.cs` - Interfaz para definir preferencias de shaders
- `PipelineBuilder.cs` (modificado) - Ahora acepta preferencias opcionales

### RE7
- `RE7ShaderPreferences.cs` - Preferencias específicas para Resident Evil 7
- `RE7Provider.cs` (modificado) - Expone las preferencias de RE7

### RE8
- `RE8ShaderPreferences.cs` - Preferencias específicas para Resident Evil 8
- `RE8Provider.cs` (modificado) - Expone las preferencias de RE8

## 🚀 Uso

### Opción 1: Con preferencias (recomendado)

```csharp
// Usando preferencias de RE7
var pipelineBuilder = new PipelineBuilder(device, RE7Provider.ShaderPreferences);
var pipeline = pipelineBuilder.BuildPipeline(materialData, materialIndex, sdfData);

// Usando preferencias de RE8
var pipelineBuilder = new PipelineBuilder(device, RE8Provider.ShaderPreferences);
var pipeline = pipelineBuilder.BuildPipeline(materialData, materialIndex, sdfData);
```

### Opción 2: Sin preferencias (fallback automático)

```csharp
// Usa detección automática de shaders
var pipelineBuilder = new PipelineBuilder(device);
var pipeline = pipelineBuilder.BuildPipeline(materialData, materialIndex, sdfData);
```

## 🔍 Cómo funciona

1. **Con preferencias**: Para cada `RenderPass`, el sistema intenta encontrar shaders usando los patrones definidos en orden de preferencia
2. **Sin match en preferencias**: Si no encuentra con las preferencias, usa la detección automática (método `DeterminePassType`)
3. **Fallback final**: Si aún así no encuentra, usa el primer shader válido que encuentre

## 📝 Logs de ejemplo

```
[PipelineBuilder] Construyendo pipeline para: lambert77
  ShaderType: Standard
  Flags: ShadowCastDisable, AlphaMaskUsed
[PipelineBuilder] Usando preferencias de shaders para: RE7
[PipelineBuilder] Analizando 692 shader programs...
[PipelineBuilder] Usando preferencias de RE7...
  ✓ [PREF] GBuffer: RE7_GBuffer_Standard (patrón: 'GBuffer')
  ✓ [PREF] Forward: RE7_Forward_Default (patrón: 'Forward')
  ✓ [PREF] ShadowPass: RE7_ShadowMap (patrón: 'Shadow')
  ✓ Lighting: DeferredLighting_Main (VS: 2048 bytes, PS: 1024 bytes)
[PipelineBuilder] Shaders válidos: 4, Saltados: 688
  ✓ Blend State: Sin blending
  ✓ Depth State: Depth read-write
  ✓ Rasterizer State: Back-face culling
  ✓ Sampler States: 1 sampler creado
[PipelineBuilder] ✓ Pipeline construido y cacheado
```

## 🎨 Personalizar preferencias

### Ejemplo: Añadir nuevos patrones para RE7

```csharp
// En RE7ShaderPreferences.cs
public IEnumerable<string> GetPreferredShaderPatterns(RenderPass pass, ShaderType shaderType)
{
    switch (pass)
    {
        case RenderPass.GBuffer:
            yield return "GBuffer";           // Primera opción
            yield return "Deferred";          // Segunda opción
            yield return "MainPass";          // Tercera opción
            yield return "MiNuevoPatron";     // Nueva opción
            break;
    }
}
```

## ⚙️ Integración en Mesh.cs

Para usarlo en Mesh.cs, necesitarías pasar el GameProvider o las preferencias:

```csharp
// En Mesh constructor o LoadMaterials
public Mesh(D3D11Device device, MeshData meshData, IAssetLoader? assetLoader = null, IShaderPreferences? shaderPrefs = null)
{
    // ...
    LoadMaterials(device, meshData, shaderPrefs);
}

private void LoadMaterials(D3D11Device device, MeshData meshData, IShaderPreferences? shaderPrefs)
{
    var pipelineBuilder = new PipelineBuilder(device, shaderPrefs);
    
    // ... resto del código
    
    var pipeline = pipelineBuilder.BuildPipeline(materialData, materialId, sdfData);
    
    // ... resto del código
}
```

O crear el PipelineBuilder una sola vez y reutilizarlo:

```csharp
private readonly PipelineBuilder _pipelineBuilder;

public Mesh(D3D11Device device, MeshData meshData, IAssetLoader? assetLoader = null, IShaderPreferences? shaderPrefs = null)
{
    _pipelineBuilder = new PipelineBuilder(device, shaderPrefs);
    // ...
}
```

## 📊 Ventajas

✅ **Específico por juego**: Cada juego puede tener sus propias convenciones de nombres  
✅ **Prioridad clara**: Los patrones se intentan en orden de preferencia  
✅ **Fallback robusto**: Si las preferencias fallan, usa detección automática  
✅ **Logging detallado**: Puedes ver exactamente qué shaders se seleccionaron y por qué  
✅ **Sin romper código existente**: Sigue funcionando sin preferencias (opcional)

## 🔮 Futuras mejoras

- [ ] Permitir sobrescribir preferencias en runtime
- [ ] Sistema de configuración JSON para preferencias
- [ ] Estadísticas de uso de shaders
- [ ] Auto-aprendizaje basado en éxitos anteriores
