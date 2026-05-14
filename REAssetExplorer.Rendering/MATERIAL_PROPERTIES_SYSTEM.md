# Sistema de Propiedades de Materiales por Shader

Este documento explica el nuevo sistema de mapeo de propiedades de materiales que permite asignar diferentes propiedades según el shader que se esté usando.

## Arquitectura

### Componentes Principales

1. **MaterialPropertyMapper** (`Shaders/MaterialPropertyMapper.cs`)
   - Mapea propiedades desde `MaterialData` a `MaterialInstance`
   - Gestiona configuraciones por shader
   - Soporta variantes de shaders automáticamente

2. **MaterialInstance** (`MaterialInstance.cs`)
   - Contiene propiedades estándar del material
   - Incluye diccionario `CustomProperties` para propiedades específicas

3. **ShaderPropertyConfig**
   - Define qué propiedades usar para cada shader

4. **Shader** (`Shader.cs`)
   - Contiene `ProfileName` que identifica el tipo de shader

## Cómo Funciona

### Flujo de Datos

```
Master Material Path (Env_Default)
    → ShaderResolver.Resolve()
        → Shader Profile (Standard_GBuffer)
            → Shader.ProfileName = "Standard_GBuffer"

MaterialData (PropertyHeaders) + Shader.ProfileName
    → MaterialPropertyMapper.ApplyProperties() 
        → MaterialInstance (BaseColor, Metallic, etc.)
```

### Configuraciones de Shader

El sistema usa el nombre del **shader profile** (no el master material) para determinar qué propiedades aplicar:

1. **Coincidencia exacta**: `Standard_GBuffer` → configuración de `Standard_GBuffer`
2. **Coincidencia parcial**: `Standard_GBuffer_VS` → configuración de `Standard_GBuffer`
3. **Configuración por defecto**: Si no se encuentra ninguna coincidencia

## Shaders Configurados

### Standard_GBuffer
- **Usado por**: `Env_Default`, `Env_Default_v2`, `Env_Default_dirt`, etc.
- **Propiedades**:
  - `BaseColor` → Color base del material (Vector4)
  - `Metallic` → Factor metálico (float)
  - `Roughness` → Rugosidad (float)
  - `Translucency` → Translucidez (float)
  - `AlphaTestRef` → Referencia para alpha test (float)
  - `OCC_UVSelect` → Selector UV para oclusión (float)

### Decal (futuro)
- **Usado por**: `Env_Decal`
- **Propiedades**:
  - `BaseColor` → Color base del material (Vector4)
  - `Metallic` → Factor metálico (float)
  - `Roughness` → Rugosidad (float)
  - `Translucency` → Translucidez (float)
  - `skin_enable` → Habilitación de skin (float, guardado en CustomProperties)

## Cómo Agregar un Nuevo Shader

### Paso 1: Agregar Mapeo en ShaderResolver

Edita `ShaderResolver.cs` para mapear el master material al shader profile:

```csharp
private static readonly Dictionary<string, string> _materialToProfileMap = new()
{
    { "Env_Default", "Standard_GBuffer" },
    { "Env_Decal", "Standard_GBuffer" },
    { "Env_Water", "Water_Shader" },  // Nuevo
};
```

### Paso 2: Configurar Propiedades en MaterialPropertyMapper

Edita `MaterialPropertyMapper.cs` para definir las propiedades del shader:

```csharp
{
    "Water_Shader", new ShaderPropertyConfig
    {
        Properties = new Dictionary<string, string>
        {
            { "BaseColor", "BaseColor" },
            { "WaveAmplitude", "WaveAmplitude" },  // Nueva propiedad
            { "WaveFrequency", "WaveFrequency" },  // Nueva propiedad
            { "Roughness", "Roughness" },
            { "Translucency", "Translucency" }
        }
    }
}
```

### Paso 3: Agregar Handler de Propiedades (si es necesaria)

Si la propiedad no existe en `MaterialInstance`, agrégala al switch en `ApplyProperty`:

```csharp
case "WaveAmplitude":
    if (property.Parameters.Length >= 1)
    {
        instance.CustomProperties["wave_amplitude"] = property.Parameters[0];
    }
    break;
```

## Propiedades Estándar de MaterialInstance

Las siguientes propiedades están disponibles directamente:

- `BaseColor` (Vector4)
- `Metallic` (float)
- `Roughness` (float)
- `Translucency` (float)
- `AlphaTestRef` (float)
- `OccUVSelect` (float)
- `GBufferTypeFlag` (float)

## Acceso a Propiedades Personalizadas

```csharp
// Escribir
materialInstance.CustomProperties["mi_propiedad"] = valor;

// Leer
if (materialInstance.CustomProperties.TryGetValue("mi_propiedad", out var valor))
{
    float miValor = (float)valor;
    // Usar el valor
}
```

## Registro Dinámico de Shaders

También se pueden registrar shaders en tiempo de ejecución:

```csharp
MaterialPropertyMapper.RegisterShaderConfig("MiNuevoShader", new ShaderPropertyConfig
{
    Properties = new Dictionary<string, string>
    {
        { "Propiedad1", "Propiedad1" },
        { "Propiedad2", "Propiedad2" }
    }
});
```

## Debugging

Para ver qué propiedades se están aplicando, puedes agregar logging en `ApplyProperty`:

```csharp
Console.WriteLine($"Aplicando {propertyName} = {property.Parameters[0]}");
```

## Notas Importantes

1. Los nombres de propiedades en `MaterialData` son case-insensitive
2. Las variantes de shaders heredan la configuración del shader base
3. Si una propiedad no existe en `MaterialData`, simplemente se ignora (no se genera error)
4. Los valores por defecto se establecen antes de aplicar las propiedades del material

## Ejemplos de Uso

### En Mesh.cs

```csharp
var materialInstance = new MaterialInstance
{
    MaterialName = materialName,
    Shader = shader,
    MaterialData = materialData,
    Textures = textures
};

// Aplicar propiedades específicas del shader desde MaterialData
// El ProfileName del shader (ej: "Standard_GBuffer") determina qué propiedades se aplican
MaterialPropertyMapper.ApplyProperties(materialInstance, materialData, matIdx, shader.ProfileName);
```

Las propiedades del material (BaseColor, Metallic, Roughness, etc.) son asignadas automáticamente desde el `MaterialData` según el tipo de shader.

### Debugging

Para ver qué shader profile se está usando:

```csharp
Console.WriteLine($"Material: {materialName}, Shader: {shader.ProfileName}");
```

Para ver qué propiedades tiene un material:

```csharp
var props = MaterialPropertyMapper.GetMaterialProperties(materialData, materialIndex);
foreach (var prop in props)
{
    Console.WriteLine($"{prop.Key}: [{string.Join(", ", prop.Value)}]");
}
```
