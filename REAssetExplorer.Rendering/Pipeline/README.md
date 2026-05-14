# Sistema de Pipeline de Renderizado

## 🎯 Filosofía

Sistema modular para construir pipelines de renderizado completos basados en materiales SDF.  
**Un material → Un pipeline → Múltiples passes**

## 📁 Estructura

```
REAssetExplorer.Rendering/
└── Pipeline/
    ├── RenderPass.cs        # Enum de tipos de passes (Shadow, GBuffer, etc.)
    ├── RenderPipeline.cs    # Pipeline completo con todos los estados
    └── PipelineBuilder.cs   # Constructor de pipelines desde SDFs
```

## 🔧 Componentes

### RenderPass
Tipos de passes disponibles:
- `ShadowPass` - Renderizado de shadow maps
- `ZPrePass` - Early depth testing
- `GBuffer` - Deferred rendering (múltiples render targets)
- `Forward` - Forward rendering opaco
- `Lighting` - Lighting pass (deferred)
- `Transparent` - Objetos transparentes
- `Distortion` - Distorsión (heat haze, etc.)
- `PostProcess` - Post-procesado
- `Debug` - Wireframe/Debug

### RenderPipeline
Contiene todos los estados para renderizar un material:
- **Shaders** - Dictionary<RenderPass, Shader> con shaders para cada pass
- **BlendState** - Alpha blending config
- **DepthStencilState** - Depth/stencil config
- **RasterizerState** - Culling, wireframe, etc.
- **SamplerStates** - Samplers para texturas
- **MaterialFlags** - Flags del material (two-sided, alpha test, etc.)
- **ShaderType** - Tipo de shader (Transparent, Forward, etc.)

### PipelineBuilder
Construye pipelines automáticamente desde materiales:
- Analiza el SDF y selecciona shader programs apropiados
- Crea blend states según MaterialFlags
- Configura depth/stencil según tipo de material
- Gestiona rasterizer state (two-sided, etc.)
- Cachea pipelines para reutilización

## 🚀 Uso

### 1. Crear el PipelineBuilder

```csharp
var pipelineBuilder = new PipelineBuilder(device);
```

### 2. Construir pipeline desde un material

```csharp
// Desde Mesh.LoadMaterials()
var pipeline = pipelineBuilder.BuildPipeline(
    materialData,      // MaterialData del asset
    materialIndex,     // Índice del material
    sdfData           // SDF cargado
);

// Asignar al MaterialInstance
materialInstance.RenderPipeline = pipeline;
```

### 3. Usar el pipeline al renderizar

```csharp
// En el render loop
var material = mesh.Materials[submesh.MaterialID];
var pipeline = material.RenderPi peline;

// Activar pipeline para un pass específico
pipeline.Bind(device.Context, RenderPass.GBuffer);

// Configurar matrices
var shader = pipeline.Shaders[RenderPass.GBuffer];
shader.SetMatrices(device, world, view, projection);

// Configurar texturas
shader.SetTextures(device, material.Textures.ToArray());

// Dibujar
device.Context.DrawIndexed(...);
```

## 🔍 Cómo funciona

### Selección automática de shaders

El `PipelineBuilder` analiza el nombre de cada `ShaderProgramData` en el SDF:

```
"ShadowPass"           → RenderPass.ShadowPass
"GBuffer_Standard"     → RenderPass.GBuffer
"Forward_Default"      → RenderPass.Forward
"Transparent_Alpha"    → RenderPass.Transparent
"ZPrePass"             → RenderPass.ZPrePass
etc.
```

### Configuración automática de estados

#### Blend State
- **Transparent ShaderTypes** → Alpha blending activado
- **AlphaTestEnable flag** → Alpha testing
- **Otros** → Sin blending

#### Depth State
- **Transparent** → Read-only depth (no escribe)
- **EnableIgnoreDepth flag** → Depth testing deshabilitado
- **Otros** → Read-write depth normal

#### Rasterizer State
- **TwoSideEnable flags** → No culling
- **Otros** → Back-face culling

## 📊 Material Flags soportados

```csharp
MaterialFlags:
- BaseTwoSideEnable      → Two-sided rendering
- BaseAlphaTestEnable    → Alpha testing
- ShadowCastDisable      → No proyecta sombras
- AlphaTestEnable        → Alpha testing
- TwoSideEnable          → Two-sided
- ForcedTwoSideEnable    → Forzar two-sided
- EnableIgnoreDepth      → Ignorar depth test
```

## 🎨 ShaderTypes soportados

```csharp
ShaderType:
- Standard               → Material opaco estándar
- Transparent            → Material transparente
- ExpensiveTransparent   → Transparencia compleja
- Decal                  → Decal
- Forward                → Forward rendering
- PostProcess            → Post-procesado
- etc.
```

## 💾 Cache

El `PipelineBuilder` cachea automáticamente pipelines usando:
```
CacheKey = "{MasterMaterialPath}_{ShaderType}_{MaterialFlags}"
```

Materiales con la misma configuración comparten el mismo pipeline.

## 🧹 Limpieza

```csharp
// Limpiar cache de pipelines
pipelineBuilder.ClearCache();

// Disponer pipeline individual
materialInstance.RenderPipeline?.Dispose();
```

## 📝 Migración del sistema anterior

### Antes:
```csharp
material.Shader = shader;
material.SelectedShaderProgram = program;
ShaderConfigurator.SetupShaderForRendering(...);
```

### Ahora:
```csharp
material.RenderPipeline = pipelineBuilder.BuildPipeline(...);
material.RenderPipeline.Bind(context, RenderPass.GBuffer);
```

## ⚠️ Notas importantes

1. **Multi-pass rendering**: Un material puede tener shaders para múltiples passes
2. **Fallback automático**: Si no se encuentra un shader apropiado, usa el primero disponible
3. **Cache inteligente**: Pipelines idénticos se reutilizan automáticamente
4. **Estados completos**: El pipeline incluye TODOS los estados D3D11 necesarios

## 🔮 Futuras extensiones

- [ ] Compute shader support
- [ ] Geometry/Tessellation shader support
- [ ] Material property animation
- [ ] Shader permutations/variants
- [ ] Hot-reload de shaders
