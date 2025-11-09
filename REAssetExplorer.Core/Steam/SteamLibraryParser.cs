using System.Text.RegularExpressions;
using System.Runtime.Versioning;
using Microsoft.Win32;

namespace REAssetExplorer.Core.Steam;

/// <summary>
/// Provides functionality to parse Steam library configuration and locate games.
/// </summary>
[SupportedOSPlatform("windows")]
public static partial class SteamLibraryParser
{
    private const string SteamRegistryPathCurrentUser = @"Software\Valve\Steam";
    private const string SteamRegistryPathLocalMachine = @"Software\Valve\Steam";
    private const string SteamAppsFolder = "steamapps";
    private const string CommonFolder = "common";
    private const string LibraryFoldersFile = "libraryfolders.vdf";
    
    [GeneratedRegex(@"""path""\s*""([^""]+)""", RegexOptions.Compiled)]
    private static partial Regex PathRegex();
    
    [GeneratedRegex(@"""installdir""\s*""([^""]+)""", RegexOptions.Compiled)]
    private static partial Regex InstallDirRegex();

    /// <summary>
    /// Attempts to read the Steam installation directory from Windows Registry.
    /// </summary>
    /// <returns>Steam root path or null if not found.</returns>
    public static string? GetSteamPathFromRegistry()
    {
        return TryGetRegistryPath(Registry.CurrentUser, "SteamPath") 
               ?? TryGetRegistryPath(Registry.LocalMachine, "InstallPath");
    }

    private static string? TryGetRegistryPath(RegistryKey rootKey, string valueName)
    {
        try
        {
            using var key = rootKey.OpenSubKey(SteamRegistryPathCurrentUser);
            var path = key?.GetValue(valueName)?.ToString();
            
            return !string.IsNullOrWhiteSpace(path) 
                ? path.Replace('/', '\\') 
                : null;
        }
        catch
        {
            return null;
        }
    }
    
    /// <summary>
    /// Returns all Steam library paths (ending in steamapps).
    /// </summary>
    /// <returns>List of steamapps folder paths.</returns>
    public static List<string> GetSteamLibraries()
    {
        var steamPath = GetSteamPathFromRegistry();
        if (steamPath is null)
            return new List<string>();

        var folders = new List<string>();
        var defaultSteamApps = Path.Combine(steamPath, SteamAppsFolder);

        // Add default library
        if (Directory.Exists(defaultSteamApps))
        {
            folders.Add(defaultSteamApps);
        }

        // Add additional libraries from VDF file
        var libraryFile = Path.Combine(steamPath, SteamAppsFolder, LibraryFoldersFile);
        if (File.Exists(libraryFile))
        {
            var extraLibraries = ParseLibraryFolders(libraryFile)
                .Select(path => Path.Combine(path, SteamAppsFolder))
                .Where(Directory.Exists);
            
            folders.AddRange(extraLibraries);
        }

        return folders.Distinct().ToList();
    }

    /// <summary>
    /// Parses libraryfolders.vdf and extracts all library folder paths.
    /// Returns paths WITHOUT "\steamapps".
    /// </summary>
    /// <param name="libraryFile">Path to libraryfolders.vdf.</param>
    /// <returns>List of library folder paths.</returns>
    public static List<string> ParseLibraryFolders(string libraryFile)
    {
        if (!File.Exists(libraryFile))
        {
            return new List<string>();
        }

        return File.ReadLines(libraryFile)
            .Select(line => line.Trim())
            .Where(line => line.StartsWith("\"path\"", StringComparison.Ordinal))
            .Select(line => PathRegex().Match(line))
            .Where(match => match.Success)
            .Select(match => match.Groups[1].Value.Replace("\\\\", "\\"))
            .Where(Directory.Exists)
            .ToList();
    }

    /// <summary>
    /// Extracts the "installdir" value from an appmanifest file.
    /// </summary>
    /// <param name="manifestFile">Path to appmanifest file (e.g. appmanifest_418370.acf).</param>
    /// <returns>The install directory name or null if not found.</returns>
    public static string? ParseInstallDir(string manifestFile)
    {
        if (!File.Exists(manifestFile))
        {
            return null;
        }

        return File.ReadLines(manifestFile)
            .Select(line => InstallDirRegex().Match(line))
            .Where(match => match.Success)
            .Select(match => match.Groups[1].Value)
            .FirstOrDefault();
    }

    /// <summary>
    /// Returns the install folder for a given Steam App ID.
    /// </summary>
    /// <param name="appId">Steam App ID.</param>
    /// <param name="steamAppsPath">Path to the steamapps folder.</param>
    /// <returns>Full path to the game installation or null if not found.</returns>
    public static string? GetGameInstallDir(uint appId, string steamAppsPath)
    {
        var manifest = Path.Combine(steamAppsPath, $"appmanifest_{appId}.acf");
        var installDir = ParseInstallDir(manifest);
        
        if (installDir is null)
            return null;

        var fullPath = Path.Combine(steamAppsPath, CommonFolder, installDir);
        return Directory.Exists(fullPath) ? fullPath : null;
    }
}