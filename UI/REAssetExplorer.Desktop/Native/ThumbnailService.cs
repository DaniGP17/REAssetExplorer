using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.Native;

// Newest request first: the rows on screen asked last.
public sealed class ThumbnailService : IDisposable
{
    public const int Size = 128;
    // Bump when the native renderer changes how thumbnails look.
    private const string CacheVersion = "v2";

    private readonly NativeGame game;
    private readonly string cacheDir;
    private readonly Dictionary<string, TaskCompletionSource<Bitmap?>> cache = new(StringComparer.Ordinal);
    private readonly Stack<(string Path, TaskCompletionSource<Bitmap?> Done)> pending = new();
    private readonly object gate = new();
    private readonly SemaphoreSlim signal = new(0);
    private readonly CancellationTokenSource stop = new();
    private readonly Task worker;

    public ThumbnailService(NativeGame game)
    {
        this.game = game;
        cacheDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                "REAssetExplorer", "Thumbnails", CacheVersion, game.Id);
        worker = Task.Run(RunAsync);
    }

    public static bool CanPreview(AssetKind kind) => kind is AssetKind.Mesh or AssetKind.Texture or AssetKind.Material;

    // element: one material of a material file (rae_thumbnail's "file|material" form).
    public Task<Bitmap?> GetAsync(AssetNode node, string? element = null)
    {
        if (!CanPreview(node.Kind)) return Task.FromResult<Bitmap?>(null);
        string key = element == null ? node.FullPath : node.FullPath + "|" + element;
        TaskCompletionSource<Bitmap?> done;
        lock (gate)
        {
            if (cache.TryGetValue(key, out TaskCompletionSource<Bitmap?>? known)) return known.Task;
            done = new TaskCompletionSource<Bitmap?>(TaskCreationOptions.RunContinuationsAsynchronously);
            cache[key] = done;
            pending.Push((key, done));
        }
        signal.Release();
        return done.Task;
    }

    // Waits for the render in flight: the game must outlive it.
    public void Dispose()
    {
        stop.Cancel();
        try
        {
            worker.Wait();
        }
        catch (AggregateException)
        {
        }
        lock (gate)
        {
            foreach ((string _, TaskCompletionSource<Bitmap?> done) in pending) done.TrySetResult(null);
            pending.Clear();
        }
    }

    private async Task RunAsync()
    {
        while (true)
        {
            try
            {
                await signal.WaitAsync(stop.Token);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            (string Path, TaskCompletionSource<Bitmap?> Done) request;
            lock (gate)
            {
                if (pending.Count == 0) continue;
                request = pending.Pop();
            }
            Bitmap? bitmap = null;
            try
            {
                bitmap = Produce(request.Path);
            }
            catch (Exception e)
            {
                NativeLog.Write(LogLevel.Warning, $"Thumbnail {request.Path}: {e.Message}");
            }
            request.Done.TrySetResult(bitmap);
        }
    }

    private Bitmap? Produce(string pakPath)
    {
        string file = Path.Combine(cacheDir, Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(pakPath))) + ".png");
        if (File.Exists(file))
        {
            try
            {
                return new Bitmap(file);
            }
            catch (Exception)
            {
                File.Delete(file);
            }
        }

        byte[] rgba = new byte[Size * Size * 4];
        if (!game.Thumbnail(pakPath, Size, rgba)) return null;
        var bitmap = new WriteableBitmap(new PixelSize(Size, Size), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Opaque);
        using (ILockedFramebuffer frame = bitmap.Lock())
        {
            for (int y = 0; y < Size; y++) Marshal.Copy(rgba, y * Size * 4, frame.Address + y * frame.RowBytes, Size * 4);
        }
        try
        {
            Directory.CreateDirectory(cacheDir);
            bitmap.Save(file, PngBitmapEncoderOptions.Default);
        }
        catch (IOException)
        {
        }
        return bitmap;
    }
}
