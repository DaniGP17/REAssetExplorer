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
                    var dependency = new AssetDependency(
                        AssetType.Texture,
                        filePath: textureHeader.TextureFilePath,
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
        
        // Cargar el archivo .tex base para obtener el header y metadata
        var baseResult = context.AssetLoader.LoadAsset<TextureData>(
            dependency.FilePath, 
            loadDependencies: false,
            onProgress
        );
        
        if (!baseResult.IsSuccess || baseResult.Value == null)
        {
            onProgress?.Invoke($"Failed to load base texture: {dependency.FilePath}");
            return null;
        }
        
        var textureData = baseResult.Value;
        
        // Buscar el archivo de streaming (.tex.35) para mips de alta resolución
        string streamingPath = FindBestTextureVariant(dependency.FilePath, context);
        
        if (!string.IsNullOrEmpty(streamingPath) && 
            streamingPath != dependency.FilePath &&
            context.GameProvider != null)
        {
            onProgress?.Invoke($"Loading high-res mip from: {streamingPath}");
            
            // Cargar y parsear el streaming file completo para obtener las dimensiones correctas
            var streamingResult = context.AssetLoader.LoadAsset<TextureData>(
                streamingPath, 
                loadDependencies: false,
                onProgress
            );
            
            if (streamingResult.IsSuccess && streamingResult.Value != null)
            {
                var streamingTexture = streamingResult.Value;
                
                // Usar las dimensiones y datos del archivo de streaming (más grande)
                textureData.Width = streamingTexture.Width;
                textureData.Height = streamingTexture.Height;
                textureData.RawMipData = streamingTexture.RawMipData;
                textureData.Mips = streamingTexture.Mips;
                textureData.MipInfo = streamingTexture.MipInfo;
                
                onProgress?.Invoke($"Loaded high-res texture: {streamingTexture.Width}x{streamingTexture.Height}, {streamingTexture.RawMipData.Length} bytes");
            }
        }
        
        return textureData;
    }
    
    private string FindBestTextureVariant(string texturePath, DependencyLoadContext context)
    {
        if (context.PakFiles == null || context.PakFiles.Count == 0)
            return texturePath;
        
        string fileName = Path.GetFileName(texturePath);
        string baseName = fileName;
        
        while (Path.HasExtension(baseName))
        {
            baseName = Path.GetFileNameWithoutExtension(baseName);
        }
        
        baseName = baseName.ToLowerInvariant();
        
        var variants = new List<(string path, long size)>();
        
        foreach (var pakFile in context.PakFiles)
        {
            foreach (var entry in pakFile.Entries)
            {
                if (entry.FilePath != null)
                {
                    string entryFileName = Path.GetFileName(entry.FilePath).ToLowerInvariant();
                    
                    string entryBaseName = entryFileName;
                    while (Path.HasExtension(entryBaseName))
                    {
                        entryBaseName = Path.GetFileNameWithoutExtension(entryBaseName);
                    }
                    
                    if (entryBaseName == baseName)
                    {
                        variants.Add((entry.FilePath, entry.UncompressedSize));
                    }
                }
            }
        }
        
        if (variants.Count == 0)
        {
            return texturePath;
        }
        
        var largest = variants.OrderByDescending(v => v.size).First();
        return largest.path;
    }
}
