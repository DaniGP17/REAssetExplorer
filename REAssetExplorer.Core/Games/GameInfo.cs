namespace REAssetExplorer.Core.Games;

/// <summary>
/// Represents information about a game installation.
/// </summary>
/// <param name="InstallDir">The directory where the game is installed.</param>
/// <param name="FromSteam">Whether the game was found through Steam.</param>
public record GameInfo(string InstallDir, bool FromSteam);