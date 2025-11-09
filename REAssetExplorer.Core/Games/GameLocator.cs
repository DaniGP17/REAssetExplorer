using System.Runtime.Versioning;
using REAssetExplorer.Core.Steam;

namespace REAssetExplorer.Core.Games;

/// <summary>
/// Locates game installations across different platforms.
/// </summary>
public static class GameLocator
{
    /// <summary>
    /// Attempts to locate a game using the provided game provider.
    /// </summary>
    /// <param name="provider">The game provider with location information.</param>
    /// <returns>Game information if found, null otherwise.</returns>
    [SupportedOSPlatform("windows")]
    public static GameInfo? Locate(IGameProvider provider)
    {
        ArgumentNullException.ThrowIfNull(provider);

        // Try Steam first
        var steamInstall = TryLocateOnSteam(provider);
        if (steamInstall != null)
        {
            return new GameInfo(steamInstall, FromSteam: true);
        }

        // Future: Add other platform locators (Epic, GOG, etc.)
        
        return null;
    }

    [SupportedOSPlatform("windows")]
    private static string? TryLocateOnSteam(IGameProvider provider)
    {
        if (provider.SteamAppId == 0)
        {
            return null;
        }

        return SteamLocator.FindGame(provider.SteamAppId);
    }
}