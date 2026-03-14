using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Resolvers;

/// <summary>
/// Dependency resolver for material assets.
/// </summary>
public class MaterialDependencyResolver : IDependencyResolver
{
    public AssetType AssetType => AssetType.Material;
    
    public bool ResolveDependencies(AssetData asset, DependencyResolutionContext context)
    {
        if (asset is not MaterialData materialData)
            return false;
        
        try
        {
            materialData.Dependencies.Clear();
            var seenTexturePaths = new HashSet<string>(StringComparer.Ordinal);
            
            // Add texture dependencies from all materials in the file
            for (int matIndex = 0; matIndex < materialData.TextureHeaders.Length; matIndex++)
            {
                var materialHeader = materialData.MaterialHeaders[matIndex];
                var textureHeaders = materialData.TextureHeaders[matIndex];

                /*var sdfDep = new AssetDependency(
                    AssetType.Sdf,
                    filePath: materialHeader.MasterMaterialFilePath,
                    name: Path.GetFileNameWithoutExtension(materialHeader.MasterMaterialFilePath),
                    isRequired: true,
                    purpose: "SDF"
                )
                {
                    Metadata = new Dictionary<string, object>
                    {
                        ["MaterialName"] = materialHeader.MaterialName,
                        ["ShaderType"] = materialHeader.ShaderType,
                        ["MaterialIndex"] = matIndex
                    }
                };
                
                materialData.Dependencies.Add(sdfDep);*/
                
                foreach (var textureHeader in textureHeaders)
                {
                    if (string.IsNullOrWhiteSpace(textureHeader.TextureFilePath))
                        continue;

                    var normalizedTexturePath = textureHeader.TextureFilePath.Replace('\\', '/').ToLowerInvariant();
                    if (!seenTexturePaths.Add(normalizedTexturePath))
                        continue;

                    var dependency = new AssetDependency(
                        AssetType.Texture,
                        filePath: normalizedTexturePath,
                        name: Path.GetFileNameWithoutExtension(textureHeader.TextureFilePath),
                        isRequired: true, // Textures are usually optional
                        purpose: textureHeader.TextureType
                    )
                    {
                        Metadata = new Dictionary<string, object>
                        {
                            ["MaterialName"] = materialHeader.MaterialName,
                            ["TextureType"] = textureHeader.TextureType,
                            ["MaterialIndex"] = matIndex
                        }
                    };
                    
                    materialData.Dependencies.Add(dependency);
                }
            }
            
            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error resolving material dependencies: {ex.Message}");
            return false;
        }
    }
    
    public AssetData? LoadDependency(AssetDependency dependency, DependencyLoadContext context, Action<string>? onProgress = null)
    {
        if (dependency.AssetType != AssetType.Texture)
            return null;
        
        if (string.IsNullOrEmpty(dependency.FilePath))
        {
            onProgress?.Invoke($"Cannot load texture '{dependency.Name}': path is empty");
            return null;
        }

        /*if (dependency.AssetType == AssetType.Sdf)
        {
            var sdfResult = context.AssetLoader.LoadAsset<SdfData>(
                dependency.FilePath, 
                loadDependencies: true,
                onProgress
            );
        
            if (!sdfResult.IsSuccess || sdfResult.Value == null)
            {
                onProgress?.Invoke($"Failed to load SDF: {dependency.FilePath}");
                return null;
            }
            
            return sdfResult.Value;
        }*/
        
        string texturePathToLoad = dependency.FilePath;
        if (context.AssetLoader is AssetLoader assetLoader)
        {
            texturePathToLoad = assetLoader.FindBestVariantPath(dependency.FilePath);
        }

        var textureResult = context.AssetLoader.LoadAsset<TextureData>(
            texturePathToLoad,
            loadDependencies: false,
            onProgress
        );

        if ((!textureResult.IsSuccess || textureResult.Value == null) &&
            !texturePathToLoad.Equals(dependency.FilePath, StringComparison.Ordinal))
        {
            textureResult = context.AssetLoader.LoadAsset<TextureData>(
                dependency.FilePath,
                loadDependencies: false,
                onProgress
            );
        }

        if (!textureResult.IsSuccess || textureResult.Value == null)
        {
            onProgress?.Invoke($"Failed to load texture: {dependency.FilePath}");
            return null;
        }

        return textureResult.Value;
    }
}
