using System;
using System.IO;
using REAssetExplorer.Core.Assets.Audio;

namespace REAssetExplorer.TestUI.Audio;

/// <summary>
/// Wraps <see cref="WEMFile"/> with a byte[]→byte[] convenience API. The underlying
/// converter only works with on-disk files, so we round-trip through %TEMP%.
/// </summary>
public static class WemConverter
{
    public static byte[] ConvertWemToOgg(byte[] wemData, string codebooksPath)
    {
        if (wemData == null || wemData.Length == 0)
            throw new ArgumentException("WEM data is empty", nameof(wemData));
        if (string.IsNullOrEmpty(codebooksPath))
            throw new ArgumentException("Codebooks path is required", nameof(codebooksPath));
        if (!File.Exists(codebooksPath))
            throw new FileNotFoundException($"Codebooks file not found: {codebooksPath}");

        string tempWemFile = Path.GetTempFileName();
        string tempOggFile = Path.GetTempFileName();

        try
        {
            File.WriteAllBytes(tempWemFile, wemData);
            var wemFile = new WEMFile(tempWemFile);
            wemFile.GenerateOGG(tempOggFile, codebooksPath);
            return File.ReadAllBytes(tempOggFile);
        }
        finally
        {
            try { if (File.Exists(tempWemFile)) File.Delete(tempWemFile); } catch { }
            try { if (File.Exists(tempOggFile)) File.Delete(tempOggFile); } catch { }
        }
    }
}
