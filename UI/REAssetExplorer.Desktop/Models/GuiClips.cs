using System.Globalization;

namespace REAssetExplorer.Desktop.Models;

// Interpolation (via.timeline): 1 discrete, 2 linear, 5 hermite (a bezier with the handles).
public sealed record GuiClipKey(double Frame, int Interpolation, double X1, double Y1, double X2, double Y2, string Value);

public sealed record GuiClipCurve(string Attribute, string Component, int AttributeType, List<GuiClipKey> Keys);

public sealed record GuiClipTrack(string Name, bool Root, List<GuiClipCurve> Curves);

public sealed record GuiClip(string Name, double Frames, bool Loops, string Next, int Pattern, List<GuiClipTrack> Tracks)
{
    public bool IsStatic => Frames <= 0;
}

public sealed class GuiClipTable
{
    public Dictionary<string, List<GuiClip>> ByElement { get; } = new(StringComparer.Ordinal);

    public static GuiClipTable Parse(string text)
    {
        var table = new GuiClipTable();
        List<GuiClip>? clips = null;
        GuiClip? clip = null;
        GuiClipTrack? track = null;
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] f = line.Split('\t');
            switch (f[0])
            {
                case "E" when f.Length >= 2:
                    clips = [];
                    table.ByElement[f[1]] = clips;
                    break;
                case "C" when f.Length >= 6 && clips != null:
                    clip = new GuiClip(f[1], Num(f[2]), f[3] == "1", f[4], (int)Num(f[5]), []);
                    clips.Add(clip);
                    break;
                case "T" when f.Length >= 3 && clip != null:
                    track = new GuiClipTrack(f[1], f[2] == "1", []);
                    clip.Tracks.Add(track);
                    break;
                case "V" when f.Length >= 5 && track != null:
                    var keys = new List<GuiClipKey>();
                    foreach (string key in f[4].Split('\x1e', StringSplitOptions.RemoveEmptyEntries))
                    {
                        string[] k = key.Split('\x1f');
                        if (k.Length < 7) continue;
                        keys.Add(new GuiClipKey(Num(k[0]), (int)Num(k[1]), Num(k[2]), Num(k[3]), Num(k[4]), Num(k[5]), k[6]));
                    }
                    keys.Sort((a, b) => a.Frame.CompareTo(b.Frame));
                    track.Curves.Add(new GuiClipCurve(f[1], f[2], (int)Num(f[3]), keys));
                    break;
            }
        }
        return table;
    }

    private static double Num(string s) => double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : 0;
}
