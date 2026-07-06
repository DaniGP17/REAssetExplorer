using System.Runtime.InteropServices;
using System.Text;

namespace REAssetExplorer.Desktop.Native;

public readonly record struct PakFileEntry(string Path, long Size);

public sealed class NativeGame : IDisposable
{
    private NativeGame(IntPtr handle, string id)
    {
        Handle = handle;
        Id = id;
    }

    public IntPtr Handle { get; private set; }
    public string Id { get; }

    // Blocking: reads every pak header.
    public static NativeGame Open(string gameId, string gameDir, string assetsDir)
    {
        IntPtr handle = NativeMethods.rae_game_open(gameId, gameDir, assetsDir);
        if (handle == IntPtr.Zero) throw new InvalidOperationException(NativeMethods.LastError());
        return new NativeGame(handle, gameId);
    }

    public unsafe List<PakFileEntry> ReadFileList()
    {
        byte* text = NativeMethods.rae_game_file_list(Handle, out long length);
        if (text == null) throw new InvalidOperationException(NativeMethods.LastError());

        var entries = new List<PakFileEntry>(capacity: (int)(length / 80));
        var span = new ReadOnlySpan<byte>(text, checked((int)length));
        while (!span.IsEmpty)
        {
            int end = span.IndexOf((byte)'\n');
            ReadOnlySpan<byte> line = end < 0 ? span : span[..end];
            span = end < 0 ? ReadOnlySpan<byte>.Empty : span[(end + 1)..];
            int tab = line.IndexOf((byte)'\t');
            if (tab <= 0) continue;
            long size = long.TryParse(Encoding.ASCII.GetString(line[(tab + 1)..]), out long parsed) ? parsed : 0;
            entries.Add(new PakFileEntry(Encoding.UTF8.GetString(line[..tab]), size));
        }
        return entries;
    }

    // Blocking: parses the asset (and linked scenes for .scn).
    public unsafe string Outline(string pakPath)
    {
        byte* text = NativeMethods.rae_game_outline(Handle, pakPath, out long length);
        if (text == null) throw new InvalidOperationException(NativeMethods.LastError());
        return Encoding.UTF8.GetString(text, checked((int)length));
    }

    public unsafe string Messages(string pakPath)
    {
        byte* text = NativeMethods.rae_game_messages(Handle, pakPath, out long length);
        if (text == null) throw new InvalidOperationException(NativeMethods.LastError());
        return Encoding.UTF8.GetString(text, checked((int)length));
    }

    public unsafe byte[]? TextureRgba(string texPath, int maxSize, out int width, out int height)
    {
        byte* pixels = NativeMethods.rae_texture_rgba(Handle, texPath, maxSize, out width, out height);
        if (pixels == null || width <= 0 || height <= 0) return null;
        return new ReadOnlySpan<byte>(pixels, checked(width * height * 4)).ToArray();
    }

    public unsafe byte[]? FontData(string fontPath)
    {
        byte* data = NativeMethods.rae_font_data(Handle, fontPath, out long length);
        if (data == null || length <= 0) return null;
        return new ReadOnlySpan<byte>(data, checked((int)length)).ToArray();
    }

    public unsafe string? GuiClips(string guiPath)
    {
        byte* text = NativeMethods.rae_gui_clips(Handle, guiPath, out long length);
        return text == null ? null : Encoding.UTF8.GetString(text, checked((int)length));
    }

    // The first call per game may take seconds: it indexes every scene and prefab.
    public unsafe string? CharacterAssemblies(string meshPath)
    {
        byte* text = NativeMethods.rae_character_assemblies(Handle, meshPath, out long length);
        return text == null ? null : Encoding.UTF8.GetString(text, checked((int)length));
    }

    public IReadOnlyList<string> MotbankMotlists(string motbankPath)
    {
        string text = Marshal.PtrToStringUTF8(NativeMethods.rae_motbank_motlists(Handle, motbankPath)) ?? string.Empty;
        return text.Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line[(line.IndexOf('\t') + 1)..].TrimEnd('\r'))
            .ToList();
    }

    public unsafe string? FsmGraph(string fsmPath)
    {
        byte* text = NativeMethods.rae_fsm_graph(Handle, fsmPath, out long length);
        return text == null ? null : Encoding.UTF8.GetString(text, checked((int)length));
    }

    public string? Uvs(string uvsPath) => Marshal.PtrToStringUTF8(NativeMethods.rae_game_uvs(Handle, uvsPath));

    // Blocking. rgba must hold size * size * 4 bytes.
    public unsafe bool Thumbnail(string pakPath, int size, byte[] rgba)
    {
        fixed (byte* pixels = rgba) return NativeMethods.rae_thumbnail(Handle, pakPath, size, pixels) != 0;
    }

    public void Extract(string pakPath, string outputFile)
    {
        if (NativeMethods.rae_game_extract(Handle, pakPath, outputFile) == 0)
            throw new InvalidOperationException(NativeMethods.LastError());
    }

    public void Dispose()
    {
        if (Handle == IntPtr.Zero) return;
        NativeMethods.rae_game_close(Handle);
        Handle = IntPtr.Zero;
    }
}
