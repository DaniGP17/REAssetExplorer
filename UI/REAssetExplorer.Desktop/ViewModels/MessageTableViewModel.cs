using CommunityToolkit.Mvvm.ComponentModel;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed class MessageRow(MessageItem item, string text)
{
    public MessageItem Item { get; } = item;
    public string Key => Item.Key;
    public string Text { get; } = text;
}

public sealed partial class MessageTableViewModel : ObservableObject
{
    private const int EnglishId = 1;
    private const string LineBreakMark = " \u21B5 ";

    private MessageTable table = new();

    [ObservableProperty]
    private IReadOnlyList<MessageLanguage> languages = [];

    [ObservableProperty]
    private MessageLanguage? selectedLanguage;

    [ObservableProperty]
    private string searchText = string.Empty;

    [ObservableProperty]
    private IReadOnlyList<MessageRow> rows = [];

    [ObservableProperty]
    private MessageRow? selectedRow;

    [ObservableProperty]
    private string statusText = string.Empty;

    public MessageTable Table => table;

    public event Action<MessageItem?>? Selected;

    public async Task LoadAsync(NativeGame game, AssetNode node)
    {
        StatusText = "Loading...";
        try
        {
            string text = await Task.Run(() => game.Messages(node.FullPath));
            table = await Task.Run(() => MessageTable.Parse(text));
        }
        catch (Exception e)
        {
            NativeLog.Write(LogLevel.Warning, $"{node.Name}: {e.Message}");
            StatusText = "Not loaded";
            return;
        }
        Languages = table.Languages;
        SelectedLanguage = table.Languages.FirstOrDefault(l => l.Id == EnglishId) ?? table.Languages.FirstOrDefault();
        Refresh();
    }

    partial void OnSelectedLanguageChanged(MessageLanguage? value) => Refresh();

    partial void OnSearchTextChanged(string value) => Refresh();

    partial void OnSelectedRowChanged(MessageRow? value) => Selected?.Invoke(value?.Item);

    private void Refresh()
    {
        int column = SelectedLanguage?.Index ?? -1;
        string filter = SearchText.Trim();
        MessageItem? kept = SelectedRow?.Item;
        var list = new List<MessageRow>(table.Items.Count);
        foreach (MessageItem item in table.Items)
        {
            string text = column >= 0 && column < item.Texts.Length ? item.Texts[column] : string.Empty;
            if (filter.Length > 0 && !item.Key.Contains(filter, StringComparison.OrdinalIgnoreCase) &&
                !text.Contains(filter, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            list.Add(new MessageRow(item, text.ReplaceLineEndings(LineBreakMark)));
        }
        Rows = list;
        SelectedRow = kept == null ? null : list.FirstOrDefault(r => r.Item == kept);
        StatusText = filter.Length == 0
            ? $"{table.Items.Count:N0} entries"
            : $"{list.Count:N0} of {table.Items.Count:N0} entries";
    }
}
