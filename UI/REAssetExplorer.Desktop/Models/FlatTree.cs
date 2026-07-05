using Avalonia.Collections;

namespace REAssetExplorer.Desktop.Models;

public interface ITreeNode
{
    int Depth { get; }
    bool IsExpanded { get; set; }
}

// Visible rows as a flat list, so a virtualized ListBox can show the tree.
public sealed class FlatTree<T>(Func<T, IReadOnlyList<T>> children, Func<T, T?> parent)
    where T : class, ITreeNode
{
    private IReadOnlyList<T> roots = [];

    public AvaloniaList<T> Rows { get; } = [];

    public void Reset(IReadOnlyList<T> value)
    {
        roots = value;
        var rows = new List<T>();
        foreach (T root in roots)
        {
            rows.Add(root);
            if (root.IsExpanded) AppendVisible(root, rows);
        }
        Rows.Clear();
        Rows.AddRange(rows);
    }

    public void Toggle(T node)
    {
        if (node.IsExpanded) Collapse(node);
        else Expand(node);
    }

    public void Expand(T node)
    {
        if (node.IsExpanded || children(node).Count == 0) return;
        node.IsExpanded = true;
        int index = Rows.IndexOf(node);
        if (index < 0) return;
        var visible = new List<T>();
        AppendVisible(node, visible);
        Rows.InsertRange(index + 1, visible);
    }

    public void Collapse(T node)
    {
        if (!node.IsExpanded) return;
        node.IsExpanded = false;
        int index = Rows.IndexOf(node);
        if (index < 0) return;
        int count = 0;
        while (index + 1 + count < Rows.Count && Rows[index + 1 + count].Depth > node.Depth) count++;
        Rows.RemoveRange(index + 1, count);
    }

    // Rows are inserted in place: a full Reset would make list controls reselect by index.
    public void Reveal(T node)
    {
        var chain = new List<T>();
        for (T? p = parent(node); p != null; p = parent(p)) chain.Add(p);
        for (int i = chain.Count - 1; i >= 0; i--) Expand(chain[i]);
    }

    public void SetExpandedAll(bool expanded)
    {
        foreach (T root in roots) SetExpanded(root, expanded);
        Reset(roots);
    }

    private void SetExpanded(T node, bool expanded)
    {
        IReadOnlyList<T> kids = children(node);
        if (kids.Count == 0) return;
        node.IsExpanded = expanded;
        foreach (T child in kids) SetExpanded(child, expanded);
    }

    private void AppendVisible(T node, List<T> output)
    {
        foreach (T child in children(node))
        {
            output.Add(child);
            if (child.IsExpanded) AppendVisible(child, output);
        }
    }
}
