using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed partial class FsmGraphViewModel : ObservableObject
{
    private FsmGraph? graph;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(PathText))]
    [NotifyCanExecuteChangedFor(nameof(UpCommand))]
    private FsmGraphNode? group;

    [ObservableProperty]
    private FsmGraphNode? selected;

    [ObservableProperty]
    private string statusText = string.Empty;

    public FsmGraphLayout? Layout { get; private set; }

    public string PathText
    {
        get
        {
            var names = new List<string>();
            for (FsmGraphNode? at = Group; at != null; at = at.Parent) names.Insert(0, at.Name);
            return string.Join("  >  ", names);
        }
    }

    public event Action<FsmGraphNode>? Picked;
    public event Action? FitRequested;
    public event Action? RevealRequested;

    public void Load(FsmGraph value)
    {
        graph = value;
        int states = value.Nodes.Count(n => !n.IsGroup);
        int transitions = value.Nodes.Sum(n => n.Transitions.Count);
        StatusText = $"{states} state{(states == 1 ? "" : "s")}, {transitions} transition{(transitions == 1 ? "" : "s")}";
        Group = value.Root;
    }

    public void Show(string key)
    {
        if (graph == null || !key.StartsWith("fsm:", StringComparison.Ordinal) ||
            !int.TryParse(key.AsSpan(4), out int index) || graph.Find(index) is not { } node) return;
        if (node.IsGroup && node.Children.Count > 0)
        {
            Group = node;
            Selected = null;
        }
        else
        {
            Group = node.Parent ?? Group;
            Selected = node;
            RevealRequested?.Invoke();
        }
    }

    public void Pick(FsmGraphNode node)
    {
        Selected = node;
        Picked?.Invoke(node);
    }

    public void Open(FsmGraphNode node)
    {
        if (!node.IsGroup || node.Children.Count == 0) return;
        Group = node;
        Selected = null;
        Picked?.Invoke(node);
    }

    [RelayCommand(CanExecute = nameof(CanGoUp))]
    private void Up()
    {
        if (Group?.Parent is not { } parent) return;
        FsmGraphNode child = Group;
        Group = parent;
        Selected = child;
        Picked?.Invoke(child);
    }

    private bool CanGoUp() => Group?.Parent != null;

    [RelayCommand]
    private void Fit() => FitRequested?.Invoke();

    partial void OnGroupChanged(FsmGraphNode? value)
    {
        Layout = value == null ? null : FsmGraphLayout.Build(value);
        FitRequested?.Invoke();
    }
}
