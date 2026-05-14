using System;
using System.IO;

namespace REAssetExplorer.TestUI.Audio;

/// <summary>
/// Resolves the packed WEM codebook file used by <see cref="WemConverter"/> to decode
/// Wwise audio. The file is shipped alongside the binaries under the Dependencies/ folder.
/// </summary>
public static class WemSettings
{
    private static string? _codebooksPath;

    public static string CodebooksPath
    {
        get
        {
            if (_codebooksPath == null) TryAutoLocateCodebooks();
            return _codebooksPath ?? string.Empty;
        }
        set => _codebooksPath = value;
    }

    public static bool IsCodebooksAvailable =>
        !string.IsNullOrEmpty(_codebooksPath) && File.Exists(_codebooksPath);

    public static bool TryAutoLocateCodebooks()
    {
        string[] searchLocations =
        {
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Dependencies", "packed_codebooks_aoTuV_603.bin"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "packed_codebooks_aoTuV_603.bin"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Codebooks", "packed_codebooks_aoTuV_603.bin"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Assets", "packed_codebooks_aoTuV_603.bin"),
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
