using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Core.Render;
using REAssetExplorer.Games.RE7.Assets;
using REAssetExplorer.Rendering.Pipeline;

namespace REAssetExplorer.Games.RE7;

/// <summary>
/// Game provider for Resident Evil 7 Biohazard.
/// </summary>
public class RE7Provider : IGameProvider
{
    private static readonly IPakReader _pakReader = new PakReaderV4();
    private static readonly IAssetReaderRegistry _assetReaders = CreateAssetReaders();
    private static readonly IShaderPreferences _shaderPreferences = new RE7ShaderPreferences();
    private static readonly IShaderSystemDeps _shaderSystemDeps = new RE7ShaderSystemDeps();
    
    /// <inheritdoc/>
    public string Id => "re7";

    /// <inheritdoc/>
    public string Name => "Resident Evil 7 Biohazard";

    /// <inheritdoc/>
    public uint SteamAppId => 418370;
    
    /// <inheritdoc/>
    public string[] PaksLocations => new[]
    {
        "re_chunk_000.pak",
        /*"dlc/re_dlc_stm_529930.pak",
        "dlc/re_dlc_stm_530610.pak",
        "dlc/re_dlc_stm_530611.pak",*/
    };

    public string FilesPath => "natives\\stm\\";
    
    /// <inheritdoc/>
    public IPakReader PakReader => _pakReader;

    /// <inheritdoc/>
    public IAssetReaderRegistry AssetReaders => _assetReaders;
    
    /// <inheritdoc/>
    public string GameDirectory { get; set; } = string.Empty;
    public object? ShaderPreferences => _shaderPreferences;
    public object? ShaderSystemDeps => _shaderSystemDeps;
    public static IShaderPreferences ShaderPreferencesTyped => _shaderPreferences;
    public static IShaderSystemDeps ShaderSystemDepsTyped => _shaderSystemDeps;

    private static IAssetReaderRegistry CreateAssetReaders()
    {
        var registry = new AssetReaderRegistry();
        
        // Register RE7-specific asset readers
        registry.RegisterReader(new RE7TextureReader());
        registry.RegisterReader(new RE7MaterialReader());
        registry.RegisterReader(new RE7MeshReader());
        registry.RegisterReader(new RE7AIMapReader());
        registry.RegisterReader(new RE7BankReader());
        registry.RegisterReader(new RE7SdfReader());
        registry.RegisterReader(new RE7SceneReader());
        registry.RegisterReader(new RE7RSZReader());
        
        return registry;
    }
}