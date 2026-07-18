namespace REAssetExplorer.Desktop.Models;

// Line is the CharacterPartsText record (Explorer/CharacterIndex.h), handed back as is to load the part.
public sealed record CharacterPart(string Name, string Mesh, string Material, int Parent, string ParentJoint, string Motbank,
                                   string Line)
{
    private const string IdentityMatrix = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";

    public static CharacterPart? Parse(string line)
    {
        string[] f = line.TrimEnd('\r').Split('\t');
        if (f.Length < 9 || f[0] != "P") return null;
        return new CharacterPart(f[1], f[2], f[3], int.TryParse(f[4], out int parent) ? parent : -1, f[6], f[7], line.TrimEnd('\r'));
    }

    public static CharacterPart Root(string mesh)
    {
        string name = NameOf(mesh);
        return new CharacterPart(name, mesh, string.Empty, -1, string.Empty, string.Empty,
                                 $"P\t{name}\t{mesh}\t\t-1\t0\t\t\t{IdentityMatrix}");
    }

    private static string NameOf(string mesh)
    {
        string name = Path.GetFileName(mesh);
        int dot = name.IndexOf('.');
        return dot > 0 ? name[..dot] : name;
    }

    // Hangs from the first part and follows its joints.
    public static CharacterPart Added(string mesh)
    {
        string name = NameOf(mesh);
        return new CharacterPart(name, mesh, string.Empty, 0, string.Empty, string.Empty,
                                 $"P\t{name}\t{mesh}\t\t0\t1\t\t\t{IdentityMatrix}");
    }
}

public sealed class CharacterAssembly(string name, string source, string motbank, int uses)
{
    public string Name { get; } = name;
    public string Source { get; } = source;
    public string Motbank { get; } = motbank;
    public int Uses { get; } = uses;
    public List<CharacterPart> Parts { get; } = [];

    public string PartsText => string.Join('\n', Parts.Select(p => p.Line));

    public int IndexOf(string mesh) =>
        Parts.FindIndex(p => string.Equals(p.Mesh, mesh, StringComparison.OrdinalIgnoreCase));

    public static List<CharacterAssembly> Parse(string text)
    {
        var list = new List<CharacterAssembly>();
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] f = line.TrimEnd('\r').Split('\t');
            if (f[0] == "C" && f.Length >= 7)
            {
                list.Add(new CharacterAssembly(f[2], f[3], f[4], int.TryParse(f[6], out int uses) ? uses : 1));
            }
            else if (list.Count > 0 && CharacterPart.Parse(line) is { } part)
            {
                list[^1].Parts.Add(part);
            }
        }
        return list;
    }
}

// Assembly is null for the mesh alone.
public sealed record CharacterOption(string Label, CharacterAssembly? Assembly, string Tip)
{
    public override string ToString() => Label;
}
