using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace REAssetExplorer.UI.Local;

/// <summary>
/// Manages local storage of game file lists.
/// </summary>
public class FileListStorage
{
    private const string AppFolderName = "REAssetExplorer";
    private const string FileListsFolderName = "filelists";
    private const string FileListExtension = ".txt";
    private const string HashExtension = ".hash";
    
    private readonly string _baseDir;

    public FileListStorage()
    {
        _baseDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            AppFolderName,
            FileListsFolderName
        );

        EnsureDirectoryExists();
    }

    /// <summary>
    /// Gets the full path to a game's file list.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>The full file path.</returns>
    public string GetPath(string gameId)
    {
        ValidateGameId(gameId);
        return Path.Combine(_baseDir, $"{gameId}{FileListExtension}");
    }

    /// <summary>
    /// Checks if a file list exists for the specified game.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>True if the file list exists, false otherwise.</returns>
    public bool Exists(string gameId)
    {
        ValidateGameId(gameId);
        return File.Exists(GetPath(gameId));
    }

    /// <summary>
    /// Loads the file list content for the specified game.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>The file list content or null if not found.</returns>
    public string? Load(string gameId)
    {
        ValidateGameId(gameId);
        
        var path = GetPath(gameId);
        return File.Exists(path) ? File.ReadAllText(path) : null;
    }

    /// <summary>
    /// Saves the file list content and its hash for the specified game.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <param name="content">The file list content to save.</param>
    public void Save(string gameId, string content)
    {
        ValidateGameId(gameId);
        ArgumentNullException.ThrowIfNull(content);

        File.WriteAllText(GetPath(gameId), content);

        var hash = ComputeHash(content);
        File.WriteAllText(GetHashPath(gameId), hash);
    }

    /// <summary>
    /// Loads the stored hash for a game's file list.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>The hash string or null if not found.</returns>
    public string? LoadHash(string gameId)
    {
        ValidateGameId(gameId);
        
        var hashPath = GetHashPath(gameId);
        return File.Exists(hashPath) ? File.ReadAllText(hashPath) : null;
    }

    /// <summary>
    /// Computes a SHA256 hash of the given content.
    /// </summary>
    /// <param name="content">The content to hash.</param>
    /// <returns>The hexadecimal hash string.</returns>
    public static string ComputeHash(string content)
    {
        ArgumentNullException.ThrowIfNull(content);
        
        using var sha = SHA256.Create();
        var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(content));
        return Convert.ToHexString(bytes);
    }

    private string GetHashPath(string gameId)
    {
        return Path.Combine(_baseDir, $"{gameId}{HashExtension}");
    }

    private void EnsureDirectoryExists()
    {
        if (!Directory.Exists(_baseDir))
        {
            Directory.CreateDirectory(_baseDir);
        }
    }

    private static void ValidateGameId(string gameId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameId);
        
        if (gameId.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("Game ID contains invalid characters.", nameof(gameId));
        }
    }
}
