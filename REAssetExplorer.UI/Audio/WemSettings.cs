using System;
using System.IO;

namespace REAssetExplorer.UI.Audio;

/// <summary>
/// Manages WEM codebooks settings and auto-location
/// </summary>
public static class WemSettings
{
    private static string? _codebooksPath;

    /// <summary>
    /// Gets the path to the codebooks file
    /// </summary>
    public static string CodebooksPath
    {
        get
        {
            if (_codebooksPath == null)
            {
                TryAutoLocateCodebooks();
            }
            return _codebooksPath ?? string.Empty;
        }
        set => _codebooksPath = value;
    }

    /// <summary>
    /// Checks if codebooks are available
    /// </summary>
    public static bool IsCodebooksAvailable => !string.IsNullOrEmpty(_codebooksPath) && File.Exists(_codebooksPath);

    /// <summary>
    /// Tries to auto-locate the codebooks file
    /// </summary>
    public static bool TryAutoLocateCodebooks()
    {
        // Common locations to check
        string[] searchLocations = new[]
        {
            // Dependencies directory (preferred location)
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Dependencies", "packed_codebooks_aoTuV_603.bin"),
            
            // Current directory
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "packed_codebooks_aoTuV_603.bin"),
            
            // Codebooks subdirectory
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Codebooks", "packed_codebooks_aoTuV_603.bin"),
            
            // Assets subdirectory
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Assets", "packed_codebooks_aoTuV_603.bin"),
            
            // Parent directory
            Path.Combine(Directory.GetParent(AppDomain.CurrentDomain.BaseDirectory)?.FullName ?? "", "packed_codebooks_aoTuV_603.bin"),
        };

        foreach (var location in searchLocations)
        {
            if (File.Exists(location))
            {
                _codebooksPath = location;
                return true;
            }
        }

        return false;
    }
}
