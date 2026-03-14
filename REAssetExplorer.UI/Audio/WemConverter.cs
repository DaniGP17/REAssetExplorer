using System;
using System.IO;
using REAssetExplorer.Core.Assets.Audio;

namespace REAssetExplorer.UI.Audio;

public static class WemConverter
{
    public static byte[] ConvertWemToOgg(byte[] wemData, string codebooksPath)
    {
        if (wemData == null || wemData.Length == 0)
        {
            throw new ArgumentException("WEM data is empty", nameof(wemData));
        }

        if (string.IsNullOrEmpty(codebooksPath))
        {
            throw new ArgumentException("Codebooks path is required", nameof(codebooksPath));
        }

        if (!File.Exists(codebooksPath))
        {
            throw new FileNotFoundException($"Codebooks file not found: {codebooksPath}");
        }

        string tempWemFile = Path.GetTempFileName();
        string tempOggFile = Path.GetTempFileName();

        try
        {
            File.WriteAllBytes(tempWemFile, wemData);
            
            var wemFile = new WEMFile(tempWemFile);
            wemFile.GenerateOGG(tempOggFile, codebooksPath);

            byte[] oggData = File.ReadAllBytes(tempOggFile);
            return oggData;
        }
        finally
        {
            try
            {
                if (File.Exists(tempWemFile))
                    File.Delete(tempWemFile);
                if (File.Exists(tempOggFile))
                    File.Delete(tempOggFile);
            }
            catch
            {
                // Ignore cleanup errors
            }
        }
    }

    public static MemoryStream ConvertWemToOggStream(byte[] wemData, string codebooksPath)
    {
        byte[] oggData = ConvertWemToOgg(wemData, codebooksPath);
        return new MemoryStream(oggData);
    }
}
