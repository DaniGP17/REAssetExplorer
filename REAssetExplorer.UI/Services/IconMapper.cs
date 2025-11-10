namespace REAssetExplorer.UI.Services;

/// <summary>
/// Maps file extensions and folder names to icon identifiers.
/// </summary>
public static class IconMapper
{
    private const string DefaultFileIcon = "Document16";
    private const string DefaultFolderIcon = "Folder32";

    private static readonly Dictionary<string, string> FileIconMap = new(StringComparer.OrdinalIgnoreCase)
    {
        ["tex"] = "Image16",
        ["mdf2"] = "CircleImage16",
        ["mesh"] = "SelectObject24",
        ["motlist"] = "PersonWalking16",
        ["wcbk"] = "HeadphonesSoundWave20",
        ["wcc"] = "HeadphonesSoundWave20"
    };

    private static readonly Dictionary<string, string> FolderIconMap = new(StringComparer.OrdinalIgnoreCase)
    {
        ["*.pak"] = "Box16",
        
        ["Unknown"] = "BookQuestionMark24",
    };

    /// <summary>
    /// Gets the icon identifier for a file based on its extension.
    /// </summary>
    /// <param name="extension">The file extension (without the dot).</param>
    /// <returns>The icon identifier.</returns>
    public static string GetFileIcon(string extension)
    {
        return FileIconMap.TryGetValue(extension, out var icon) 
            ? icon 
            : DefaultFileIcon;
    }

    /// <summary>
    /// Gets the icon identifier for a folder based on its name or extension.
    /// Supports wildcard patterns (e.g., "*.pak") and exact names (e.g., "Unknown").
    /// </summary>
    /// <param name="folderName">The folder name or path.</param>
    /// <returns>The icon identifier.</returns>
    public static string GetFolderIcon(string folderName)
    {
        if (FolderIconMap.TryGetValue(folderName, out var icon))
        {
            return icon;
        }
        
        foreach (var pattern in FolderIconMap.Keys)
        {
            if (pattern.StartsWith("*"))
            {
                // Wildcard pattern like "*.pak"
                var extension = pattern.Substring(1); // Remove the '*'
                if (folderName.EndsWith(extension, StringComparison.OrdinalIgnoreCase))
                {
                    return FolderIconMap[pattern];
                }
            }
        }
        
        return DefaultFolderIcon;
    }

    /// <summary>
    /// Extracts the real file extension, handling numbered extensions (e.g., .tex.123).
    /// </summary>
    /// <param name="fullPath">The full file path.</param>
    /// <returns>The extracted extension without the dot.</returns>
    public static string GetRealExtension(string fullPath)
    {
        var parts = fullPath.Split('.');

        if (parts.Length < 2)
            return string.Empty;

        var lastPart = parts[^1];

        // If the last part is a number, use the second-to-last part as the extension
        return int.TryParse(lastPart, out _) 
            ? parts[^2] 
            : lastPart;
    }
}
