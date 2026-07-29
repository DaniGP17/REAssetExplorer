using System.Globalization;
using System.Runtime.InteropServices;
using Avalonia.Media;
using Avalonia.Media.Fonts;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed class GameFonts : FontCollectionBase
{
    private const int EnglishId = 1;
    private static readonly Dictionary<IntPtr, GameFonts> ByGame = [];

    private readonly Uri key;
    private readonly Dictionary<int, FontFamily> slots = [];
    private Task? loading;

    private GameFonts(NativeGame game)
    {
        Game = game;
        key = new Uri($"fonts:REAssetGame{game.Handle:X}");
    }

    public override Uri Key => key;

    public NativeGame Game { get; }

    public bool IsLoaded { get; private set; }

    public event Action? Loaded;

    public static GameFonts For(NativeGame game)
    {
        if (!ByGame.TryGetValue(game.Handle, out GameFonts? fonts))
        {
            fonts = new GameFonts(game);
            ByGame[game.Handle] = fonts;
        }
        return fonts;
    }

    public FontFamily? Slot(string fontSlot)
    {
        if (!fontSlot.StartsWith("Slot", StringComparison.Ordinal) ||
            !int.TryParse(fontSlot.AsSpan(4), NumberStyles.Integer, CultureInfo.InvariantCulture, out int index))
        {
            index = 0;
        }
        return slots.GetValueOrDefault(index);
    }

    public Task LoadAsync()
    {
        loading ??= LoadCoreAsync();
        return loading;
    }

    private async Task LoadCoreAsync()
    {
        // Native decryption is slow for the large CJK fonts.
        (List<(int Slot, string Path)> chains, Dictionary<string, byte[]> data) = await Task.Run(() =>
        {
            var chainList = new List<(int, string)>();
            var bytes = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
            string text = Marshal.PtrToStringUTF8(NativeMethods.rae_gui_fonts(Game.Handle, EnglishId)) ?? string.Empty;
            foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                string[] f = line.Split('\t');
                if (f.Length < 2 || !int.TryParse(f[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out int slot)) continue;
                chainList.Add((slot, f[1]));
                if (!bytes.ContainsKey(f[1]) && Game.FontData(f[1]) is { } font) bytes[f[1]] = font;
            }
            return (chainList, bytes);
        });

        FontManager.Current.AddFontCollection(this);
        var families = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach ((string path, byte[] font) in data)
        {
            using var stream = new MemoryStream(font, writable: false);
            if (TryAddGlyphTypeface(stream, out GlyphTypeface? typeface) && typeface != null) families[path] = typeface.FamilyName;
            else NativeLog.Write(LogLevel.Warning, $"GUI fonts: {path} could not be loaded");
        }
        foreach (IGrouping<int, (int Slot, string Path)> chain in chains.GroupBy(c => c.Slot))
        {
            // The Latin companion fonts (CAP-..., ...Latin) draw the Latin text of a chain.
            List<string> names = chain.Select(c => c.Path)
                .OrderBy(p => IsLatinCompanion(p) ? 0 : 1)
                .Where(families.ContainsKey)
                .Select(p => $"{key}#{families[p]}")
                .Distinct()
                .ToList();
            if (names.Count > 0) slots[chain.Key] = new FontFamily(string.Join(", ", names));
        }
        IsLoaded = true;
        NativeLog.Write(LogLevel.Info, $"GUI fonts: {families.Count} fonts for {slots.Count} slots");
        Loaded?.Invoke();
    }

    private static bool IsLatinCompanion(string path)
    {
        string name = Path.GetFileNameWithoutExtension(path);
        return name.StartsWith("CAP-", StringComparison.OrdinalIgnoreCase) || name.Contains("Latin", StringComparison.OrdinalIgnoreCase);
    }
}
