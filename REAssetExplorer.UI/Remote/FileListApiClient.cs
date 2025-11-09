using System.Net.Http;
using System.Security.Cryptography;
using System.Text;

namespace REAssetExplorer.UI.Remote;

/// <summary>
/// Client for downloading file lists from GitHub repository.
/// </summary>
public class FileListApiClient
{
    private const string BaseUrl = "https://raw.githubusercontent.com/DaniGP17/REAssetExplorer/main/FileLists";
    private const string FileListFileName = "{0}.txt";
    
    private readonly HttpClient _httpClient;

    public FileListApiClient(HttpClient httpClient)
    {
        _httpClient = httpClient ?? throw new ArgumentNullException(nameof(httpClient));
    }

    /// <summary>
    /// Computes the hash of the file list content.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>The SHA256 hash string or null if the request fails.</returns>
    public async Task<string?> GetHashAsync(string gameId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameId);

        try
        {
            var content = await GetFileListAsync(gameId);
            if (content == null)
                return null;

            return ComputeHash(content);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error getting hash for game '{gameId}': {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Downloads the complete file list for the specified game from GitHub.
    /// </summary>
    /// <param name="gameId">The game identifier.</param>
    /// <returns>The file list content or null if the request fails.</returns>
    public async Task<string?> GetFileListAsync(string gameId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameId);

        try
        {
            var url = BuildUrl(gameId);
            var response = await _httpClient.GetAsync(url);

            if (!response.IsSuccessStatusCode)
            {
                Console.WriteLine($"Failed to download file list for game '{gameId}' from GitHub. Status: {response.StatusCode}");
                return null;
            }

            return await response.Content.ReadAsStringAsync();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error downloading file list for game '{gameId}': {ex.Message}");
            return null;
        }
    }

    private static string BuildUrl(string gameId)
    {
        var fileName = string.Format(FileListFileName, gameId);
        return $"{BaseUrl}/{fileName}";
    }

    private static string ComputeHash(string content)
    {
        using var sha = SHA256.Create();
        var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(content));
        return Convert.ToHexString(bytes);
    }
}