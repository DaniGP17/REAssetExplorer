using System.Runtime.Versioning;

namespace REAssetExplorer.Core.Steam;

/// <summary>
/// Provides methods to locate Steam games by their App ID.
/// </summary>
[SupportedOSPlatform("windows")]
public static class SteamLocator
{
    /// <summary>
    /// Finds the installation directory for a Steam game.
    /// </summary>
    /// <param name="appId">The Steam App ID of the game.</param>
    /// <returns>The installation directory path or null if not found.</returns>
    public static string? FindGame(uint appId)
    {
        var libraries = SteamLibraryParser.GetSteamLibraries();
        
        foreach (var library in libraries)
        {
            var installDir = SteamLibraryParser.GetGameInstallDir(appId, library);
            if (installDir != null)
            {
                return installDir;
            }
        }

        return null;
    }
}