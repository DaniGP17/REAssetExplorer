using System.Globalization;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed record UvsAtlas(List<string> Textures, List<(int First, int Count)> Sequences, List<(int Texture, Rect Uv)> Patterns)
{
    public static UvsAtlas Parse(string text)
    {
        var atlas = new UvsAtlas([], [], []);
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] f = line.Split('\t');
            switch (f[0])
            {
                case "T" when f.Length >= 2:
                    atlas.Textures.Add(f[1]);
                    break;
                case "S" when f.Length >= 3:
                    atlas.Sequences.Add((Int(f[1]), Int(f[2])));
                    break;
                case "P" when f.Length >= 6:
                    atlas.Patterns.Add((Int(f[1]), new Rect(new Point(Num(f[2]), Num(f[3])), new Point(Num(f[4]), Num(f[5])))));
                    break;
            }
        }
        return atlas;
    }

    private static int Int(string s) => int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : 0;

    private static double Num(string s) => double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : 0;
}

public sealed class GuiImageCache(NativeGame game)
{
    // UI textures are at most 4096 wide; the preview never needs more than this.
    private const int MaxTextureSize = 2048;

    private readonly Dictionary<string, UvsAtlas?> atlases = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, Bitmap?> textures = new(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> loading = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<(Bitmap, PixelRect, Color), Bitmap> tinted = [];

    public event Action? Changed;

    public UvsAtlas? Atlas(string uvsPath)
    {
        if (uvsPath.Length == 0) return null;
        if (!atlases.TryGetValue(uvsPath, out UvsAtlas? atlas))
        {
            string? text = game.Uvs(uvsPath);
            atlas = text != null ? UvsAtlas.Parse(text) : null;
            atlases[uvsPath] = atlas;
        }
        return atlas;
    }

    public Bitmap? Texture(string texPath)
    {
        if (texPath.Length == 0) return null;
        if (textures.TryGetValue(texPath, out Bitmap? bitmap)) return bitmap;
        if (loading.Add(texPath)) _ = LoadAsync(texPath);
        return null;
    }

    public (Bitmap Bitmap, Rect Source)? Pattern(string uvsPath, int sequence, int pattern)
    {
        UvsAtlas? atlas = Atlas(uvsPath);
        if (atlas == null || sequence < 0 || sequence >= atlas.Sequences.Count) return null;
        (int first, int count) = atlas.Sequences[sequence];
        if (pattern < 0 || pattern >= count || first + pattern >= atlas.Patterns.Count) return null;
        (int textureIndex, Rect uv) = atlas.Patterns[first + pattern];
        if (textureIndex < 0 || textureIndex >= atlas.Textures.Count) return null;
        Bitmap? bitmap = Texture(atlas.Textures[textureIndex]);
        if (bitmap == null) return null;
        Size size = bitmap.Size;
        return (bitmap, new Rect(uv.X * size.Width, uv.Y * size.Height, uv.Width * size.Width, uv.Height * size.Height));
    }

    // Tint alpha is ignored: opacity is drawn separately.
    public IImage Tinted(Bitmap bitmap, Rect source, Color tint)
    {
        var rect = new PixelRect((int)Math.Floor(source.X), (int)Math.Floor(source.Y),
                                 Math.Max((int)Math.Ceiling(source.Width), 1), Math.Max((int)Math.Ceiling(source.Height), 1));
        rect = rect.Intersect(new PixelRect(bitmap.PixelSize));
        if (rect.Width <= 0 || rect.Height <= 0) return bitmap;
        var key = (bitmap, rect, Color.FromRgb(tint.R, tint.G, tint.B));
        if (tinted.TryGetValue(key, out Bitmap? result)) return result;

        var pixels = new byte[rect.Width * rect.Height * 4];
        var handle = GCHandle.Alloc(pixels, GCHandleType.Pinned);
        try
        {
            bitmap.CopyPixels(rect, handle.AddrOfPinnedObject(), pixels.Length, rect.Width * 4);
        }
        finally
        {
            handle.Free();
        }
        for (int i = 0; i < pixels.Length; i += 4)
        {
            pixels[i] = (byte)(pixels[i] * tint.R / 255);
            pixels[i + 1] = (byte)(pixels[i + 1] * tint.G / 255);
            pixels[i + 2] = (byte)(pixels[i + 2] * tint.B / 255);
        }
        result = CreateBitmap(pixels, rect.Width, rect.Height);
        tinted[key] = result;
        return result;
    }

    private async Task LoadAsync(string texPath)
    {
        Bitmap? bitmap = await Task.Run(() =>
        {
            byte[]? rgba = game.TextureRgba(texPath, MaxTextureSize, out int width, out int height);
            return rgba != null ? CreateBitmap(rgba, width, height) : null;
        });
        await Dispatcher.UIThread.InvokeAsync(() =>
        {
            textures[texPath] = bitmap;
            loading.Remove(texPath);
            if (bitmap == null) NativeLog.Write(LogLevel.Warning, $"GUI preview: texture not loaded: {texPath}");
            Changed?.Invoke();
        });
    }

    private static Bitmap CreateBitmap(byte[] rgba, int width, int height)
    {
        var bitmap = new WriteableBitmap(new PixelSize(width, height), new Vector(96, 96), PixelFormat.Rgba8888, AlphaFormat.Unpremul);
        using ILockedFramebuffer frame = bitmap.Lock();
        for (int y = 0; y < height; y++) Marshal.Copy(rgba, y * width * 4, frame.Address + y * frame.RowBytes, width * 4);
        return bitmap;
    }
}
