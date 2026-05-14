using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Threading.Tasks;
using REAssetExplorer.Core.Games;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.TestUI.Models;

namespace REAssetExplorer.TestUI;

public static class GameLoader
{
    private const string FileListBaseUrl =
        "https://raw.githubusercontent.com/DaniGP17/REAssetExplorer/main/FileLists";

    public static IReadOnlyDictionary<string, PakFile> LoadedPakFiles { get; private set; }
        = new Dictionary<string, PakFile>();

    public static async Task<bool> LoadGameAsync(IGameProvider provider, Action<string> onProgress)
    {
        var fileListPath = GetFileListPath(provider.Id);

        if (!File.Exists(fileListPath))
        {
            onProgress("Downloading file list...");
            if (!await DownloadFileListAsync(provider.Id, fileListPath, onProgress))
                return false;
        }
        else
        {
            onProgress("File list found, loading PAK files...");
        }

        onProgress("Loading PAK files...");

        var pakLoader = new PakLoader();

        var rszPath = Path.Combine(AppContext.BaseDirectory, "FileLists", provider.RszFile);

        var result = await pakLoader.LoadPakFilesAsync(
            provider,
            fileListPath,
            rszFilePath: File.Exists(rszPath) ? rszPath : null,
            onProgress: onProgress);

        if (result.IsFailure)
        {
            onProgress($"Error: {result.Error}");
            return false;
        }

        LoadedPakFiles = result.Value!;

        onProgress("Building file tree...");
        var tree = TreeManager.Instance;
        tree.Clear();
        foreach (var pak in result.Value!.Values)
            tree.BuildTree(pak);
        tree.SortTree(tree.Root);

        AssetSession.Initialize(provider, LoadedPakFiles);

        return true;
    }

    private static string GetFileListPath(string gameId)
        => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "REAssetExplorer", "filelists", $"{gameId}.txt");

    private static async Task<bool> DownloadFileListAsync(
        string gameId, string localPath, Action<string> onProgress)
    {
        try
        {
            onProgress("Connecting to server...");
            using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
            var content = await client.GetStringAsync($"{FileListBaseUrl}/{gameId}.txt");

            onProgress("Saving file list...");
            Directory.CreateDirectory(Path.GetDirectoryName(localPath)!);
            await File.WriteAllTextAsync(localPath, content);
            return true;
        }
        catch (Exception ex)
        {
            onProgress($"Download failed: {ex.Message}");
            return false;
        }
    }
}
