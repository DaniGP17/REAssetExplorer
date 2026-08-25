using System.Globalization;
using System.Text;

namespace REAssetExplorer.Desktop.Models;

// Index is the language's column in MessageItem.Texts; Id the engine's language id.
public sealed record MessageLanguage(int Index, int Id, string Name)
{
    public override string ToString() => Name;
}

public sealed record MessageAttributeInfo(string Name);

public sealed class MessageItem(string key, string guid, string hash, string[] attributes, string[] texts)
{
    public string Key { get; } = key;
    public string Guid { get; } = guid;
    public string Hash { get; } = hash;
    public string[] Attributes { get; } = attributes;
    public string[] Texts { get; } = texts;
}

public sealed class MessageTable
{
    public List<MessageLanguage> Languages { get; } = [];
    public List<MessageAttributeInfo> Attributes { get; } = [];
    public List<MessageItem> Items { get; } = [];

    public static MessageTable Parse(string text)
    {
        var table = new MessageTable();
        foreach (string line in text.Split('\n'))
        {
            if (line.Length == 0) continue;
            string[] fields = line.Split('\t');
            for (int i = 1; i < fields.Length; i++) fields[i] = Unescape(fields[i]);
            switch (fields[0])
            {
                case "L" when fields.Length >= 3:
                    table.Languages.Add(new MessageLanguage(table.Languages.Count, ParseInt(fields[1]), fields[2]));
                    break;
                case "A" when fields.Length >= 3:
                    table.Attributes.Add(new MessageAttributeInfo(fields[1]));
                    break;
                case "E" when fields.Length >= 4:
                    int attributes = table.Attributes.Count;
                    table.Items.Add(new MessageItem(fields[1], fields[2], fields[3], fields.Skip(4).Take(attributes).ToArray(),
                                                    fields.Skip(4 + attributes).ToArray()));
                    break;
            }
        }
        return table;
    }

    private static int ParseInt(string value) => int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) ? n : -1;

    private static string Unescape(string value)
    {
        if (!value.Contains('\\')) return value;
        var text = new StringBuilder(value.Length);
        for (int i = 0; i < value.Length; i++)
        {
            char c = value[i];
            if (c != '\\' || i + 1 == value.Length)
            {
                text.Append(c);
                continue;
            }
            char next = value[++i];
            text.Append(next switch { 't' => '\t', 'n' => '\n', 'r' => '\r', _ => next });
        }
        return text.ToString();
    }
}
