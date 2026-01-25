using REAssetExplorer.Core.Steam;
using REAssetExplorer.Games.RE7;
using REAssetExplorer.Games.RE8;
using System.Runtime.Versioning;

namespace REAssetExplorer.Testing.TestHelpers;

[SupportedOSPlatform("windows")]
public static class TestDataPaths
{
    private static readonly RE7Provider _re7Provider = new();
    private static readonly RE8Provider _re8Provider = new();

    // Base paths
    public static string ProjectRoot => Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
    public static string FileListsDir => Path.Combine(ProjectRoot, "FileLists");
    public static string TestDataDir => Path.Combine(ProjectRoot, "REAssetExplorer.Testing", "TestData");
    
    // Game installations
    public static string? RE7InstallPath => SteamLocator.FindGame(_re7Provider.SteamAppId);
    
    public static string? RE8InstallPath => SteamLocator.FindGame(_re8Provider.SteamAppId);
    
    // File lists
    public static string RE7FileList => Path.Combine(FileListsDir, "re7.txt");
    public static string RE8FileList => Path.Combine(FileListsDir, "re8.txt");
    
    // PAK files
    public static string RE7MainPak => Path.Combine(RE7InstallPath ?? "", "re_chunk_000.pak");
    public static string RE8MainPak => Path.Combine(RE8InstallPath ?? "", "re_chunk_000.pak");
    
    // Test assets
    public static string GetTestAssetPath(string filename) => Path.Combine(TestDataDir, filename);
    
    public static bool IsRE7Installed() => !string.IsNullOrEmpty(RE7InstallPath) && File.Exists(RE7MainPak);
    
    public static bool IsRE8Installed() => !string.IsNullOrEmpty(RE8InstallPath) && File.Exists(RE8MainPak);
}
