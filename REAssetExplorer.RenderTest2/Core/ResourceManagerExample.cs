using System;
using System.Linq;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.Core;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Core.Games;
using REAssetExplorer.RenderTest2.Interop.Structures;

namespace REAssetExplorer.RenderTest2.Examples;

/// <summary>
/// Ejemplos de uso del ResourceManager
/// </summary>
public static class ResourceManagerExample
{
    /// <summary>
    /// Ejemplo básico: Cargar una textura
    /// </summary>
    public static void Example1_LoadTexture(ResourceManager resourceManager)
    {
        // Encolar carga de textura
        var textureId = resourceManager.EnqueueLoad(
            "natives/stm/renderware/texture/example.tex", 
            ResourceType.Texture,
            loadDependencies: false
        );

        // En el main loop, procesar cola de carga
        resourceManager.ProcessLoadQueue(maxItemsPerFrame: 5);

        // Verificar si está cargada
        if (resourceManager.TryGetResource(textureId, out var resource))
        {
            if (resource is RenderTexture textureRes)
            {
                Console.WriteLine($"Texture loaded: {textureRes.Name}");
                Console.WriteLine($"Size: {textureRes.Width}x{textureRes.Height}");
                Console.WriteLine($"Load time: {textureRes.LoadTimeMs}ms");
            }
        }
    }

    /// <summary>
    /// Ejemplo 2: Cargar un mesh con sus dependencias (materiales y texturas)
    /// </summary>
    /*public static void Example2_LoadMeshWithDependencies(ResourceManager resourceManager)
    {
        // Encolar carga de mesh con resolución automática de dependencias
        var meshId = resourceManager.EnqueueLoad(
            "natives/stm/model/character/example.mesh",
            ResourceType.Mesh,
            loadDependencies: true  // Cargará automáticamente materiales y texturas
        );

        // Procesar cola (llamar cada frame)
        resourceManager.ProcessLoadQueue(maxItemsPerFrame: 10);

        // Obtener el mesh cargado
        var meshResource = resourceManager.GetResource<MeshResource>(meshId);
        if (meshResource != null)
        {
            Console.WriteLine($"Mesh loaded: {meshResource.Name}");
            Console.WriteLine($"Materials: {meshResource.MaterialIds.Count}");
            
            // Iterar sobre materiales
            foreach (var matId in meshResource.MaterialIds)
            {
                var material = resourceManager.GetResource<MaterialResource>(matId);
                if (material != null)
                {
                    Console.WriteLine($"  Material: {material.Name}");
                    Console.WriteLine($"    Textures: {material.TextureIds.Count}");
                    
                    // Iterar sobre texturas
                    foreach (var texId in material.TextureIds)
                    {
                        var texture = resourceManager.GetResource<RenderTexture>(texId);
                        if (texture != null)
                        {
                            Console.WriteLine($"      Texture: {texture.Name} ({texture.Width}x{texture.Height})");
                        }
                    }
                }
            }
        }
    }*/

    /// <summary>
    /// Ejemplo 3: Gestión del ciclo de vida en un game loop
    /// </summary>
    public static void Example3_GameLoop(ResourceManager resourceManager)
    {
        bool running = true;
        
        // Encolar varios recursos
        resourceManager.EnqueueLoad("path/to/mesh1.mesh", ResourceType.Mesh, true);
        resourceManager.EnqueueLoad("path/to/mesh2.mesh", ResourceType.Mesh, true);
        resourceManager.EnqueueLoad("path/to/texture1.tex", ResourceType.Texture, false);

        while (running)
        {
            // 1. Procesar carga de recursos (limitar cantidad por frame para no bloquear)
            resourceManager.ProcessLoadQueue(maxItemsPerFrame: 5);

            // 2. Procesar eliminaciones pendientes
            resourceManager.ProcessDeletes();

            // 3. Tu lógica de render aquí
            // RenderScene(resourceManager);

            // 4. Mostrar estadísticas
            if (FrameCount() % 60 == 0) // Cada 60 frames
            {
                Console.WriteLine(resourceManager.GetStats());
            }

            // Condición de salida
            // running = CheckExitCondition();
        }

        // Al finalizar, limpiar todos los recursos
        resourceManager.ClearAll();
    }

    /// <summary>
    /// Ejemplo 4: Cargar múltiples texturas en batch
    /// </summary>
    public static void Example4_BatchLoad(ResourceManager resourceManager)
    {
        var texturePaths = new[]
        {
            "textures/diffuse1.tex",
            "textures/normal1.tex",
            "textures/roughness1.tex",
            "textures/diffuse2.tex",
            "textures/normal2.tex",
        };

        Console.WriteLine($"Enqueueing {texturePaths.Length} textures...");
        
        // Encolar todas
        foreach (var path in texturePaths)
        {
            resourceManager.EnqueueLoad(path, ResourceType.Texture, false);
        }

        // Procesar hasta que todas estén cargadas
        while (resourceManager.LoadQueueCount > 0)
        {
            resourceManager.ProcessLoadQueue(maxItemsPerFrame: 10);
        }

        // Obtener todas las texturas cargadas
        var loadedTextures = resourceManager.GetResourcesOfType<RenderTexture>();
        Console.WriteLine($"Total textures loaded: {loadedTextures.Count()}");
        
        foreach (var tex in loadedTextures)
        {
            Console.WriteLine($"  - {tex.Name}: {tex.Width}x{tex.Height} ({tex.LoadTimeMs}ms)");
        }
    }

    /// <summary>
    /// Ejemplo 5: Verificar y obtener recursos
    /// </summary>
    public static void Example5_CheckAndGet(ResourceManager resourceManager)
    {
        var texturePath = "example.tex";
        
        // Verificar si ya está cargado
        if (resourceManager.IsResourceLoaded(texturePath))
        {
            Console.WriteLine("Texture already loaded!");
            var texture = resourceManager.GetResource<RenderTexture>(texturePath);
            // Usar texture...
        }
        else
        {
            Console.WriteLine("Texture not loaded, enqueueing...");
            resourceManager.EnqueueLoad(texturePath, ResourceType.Texture);
        }
    }

    /// <summary>
    /// Ejemplo 6: Eliminación de recursos no utilizados
    /// </summary>
    public static void Example6_UnloadUnused(ResourceManager resourceManager)
    {
        // Obtener todos los recursos
        var allResources = resourceManager.GetResourcesOfType<Resource>();
        
        foreach (var resource in allResources.ToList())
        {
            // Tu lógica para determinar si un recurso está en uso
            // Por ejemplo, verificar si está en la escena actual
            if (IsResourceUnused(resource))
            {
                Console.WriteLine($"Unloading unused resource: {resource.Name}");
                resourceManager.EnqueueDelete(resource.Id);
            }
        }

        // Procesar eliminaciones
        resourceManager.ProcessDeletes();
    }

    // Métodos helper de ejemplo
    private static int FrameCount() => 0;
    private static bool IsResourceUnused(Resource resource) => false;
}
