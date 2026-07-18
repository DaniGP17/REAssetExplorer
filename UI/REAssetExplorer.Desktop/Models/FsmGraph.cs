using Avalonia;

namespace REAssetExplorer.Desktop.Models;

public enum FsmEdgeKind
{
    Transition,
    Start,
    Any
}

public sealed record FsmGraphEdge(int To, string Label, FsmEdgeKind Kind);

// A group holds states (its children); a state's transitions go to its siblings.
public sealed class FsmGraphNode(int index, int parent, string name, bool isGroup, string summary, bool isEnd)
{
    public int Index { get; } = index;
    public int ParentIndex { get; } = parent;
    public string Name { get; } = name;
    public bool IsGroup { get; } = isGroup;
    public string Summary { get; } = summary;
    public bool IsEnd { get; } = isEnd;
    public FsmGraphNode? Parent { get; set; }
    public List<FsmGraphNode> Children { get; } = [];
    public List<FsmGraphEdge> Transitions { get; } = [];
    public List<FsmGraphEdge> Starts { get; } = [];
    public List<FsmGraphEdge> AnyStates { get; } = [];

    // Same key AssetOutline gives the node.
    public string Key => $"fsm:{Index}";
}

public sealed class FsmGraph
{
    public List<FsmGraphNode> Nodes { get; } = [];
    public FsmGraphNode? Root { get; private set; }

    public FsmGraphNode? Find(int index) => index >= 0 && index < Nodes.Count ? Nodes[index] : null;

    public static FsmGraph Parse(string text)
    {
        var graph = new FsmGraph();
        var edges = new List<string[]>();
        foreach (string line in text.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            string[] f = line.TrimEnd('\r').Split('\t');
            if (f[0] == "N" && f.Length >= 7)
            {
                graph.Nodes.Add(new FsmGraphNode(int.Parse(f[1]), int.Parse(f[2]), f[3], f[4] == "1", f[5], f[6] == "1"));
            }
            else if (f.Length >= 4)
            {
                edges.Add(f);
            }
        }
        foreach (FsmGraphNode node in graph.Nodes)
        {
            node.Parent = graph.Find(node.ParentIndex);
            if (node.Parent == null) graph.Root ??= node;
        }
        foreach (FsmGraphNode node in graph.Nodes) node.Parent?.Children.Add(node);
        foreach (string[] f in edges)
        {
            if (!int.TryParse(f[1], out int from) || !int.TryParse(f[2], out int to) || graph.Find(from) is not { } source ||
                graph.Find(to) == null) continue;
            switch (f[0])
            {
                case "S": source.Transitions.Add(new FsmGraphEdge(to, f[3], FsmEdgeKind.Transition)); break;
                case "T": source.Starts.Add(new FsmGraphEdge(to, f[3], FsmEdgeKind.Start)); break;
                case "A": source.AnyStates.Add(new FsmGraphEdge(to, f[3], FsmEdgeKind.Any)); break;
            }
        }
        return graph;
    }
}

public enum FsmBoxKind
{
    State,
    Group,
    End,
    Entry,
    AnyState
}

public sealed class FsmLayoutBox(FsmGraphNode? node, string title, string subtitle, FsmBoxKind kind)
{
    public FsmGraphNode? Node { get; } = node;
    public string Title { get; } = title;
    public string Subtitle { get; } = subtitle;
    public FsmBoxKind Kind { get; } = kind;
    public Rect Rect { get; set; }
}

public sealed record FsmLayoutEdge(FsmLayoutBox From, FsmLayoutBox To, string Label, FsmEdgeKind Kind)
{
    // Fractions of the box height.
    public double FromPort { get; set; } = 0.5;
    public double ToPort { get; set; } = 0.5;
}

public sealed class FsmGraphLayout
{
    public const double BoxWidth = 210;
    public const double BoxHeight = 44;
    private const double ColumnGap = 110;
    private const double RowGap = 24;
    private const int MaxRows = 12;

    public List<FsmLayoutBox> Boxes { get; } = [];
    public List<FsmLayoutEdge> Edges { get; } = [];
    public Rect Bounds { get; private set; }

    public FsmLayoutBox? BoxOf(FsmGraphNode node) => Boxes.FirstOrDefault(b => b.Node == node);

    public static FsmGraphLayout Build(FsmGraphNode group)
    {
        var layout = new FsmGraphLayout();
        List<FsmGraphNode> states = group.Children;
        var inGroup = new HashSet<FsmGraphNode>(states);
        FsmGraphNode? Node(int index) => states.FirstOrDefault(s => s.Index == index);

        var column = new Dictionary<FsmGraphNode, int>();
        var order = new List<FsmGraphNode>();
        void Walk(IEnumerable<FsmGraphNode> seeds)
        {
            var queue = new Queue<FsmGraphNode>();
            foreach (FsmGraphNode seed in seeds)
            {
                if (!column.TryAdd(seed, 0)) continue;
                order.Add(seed);
                queue.Enqueue(seed);
            }
            while (queue.Count > 0)
            {
                FsmGraphNode node = queue.Dequeue();
                foreach (FsmGraphEdge edge in node.Transitions)
                {
                    if (Node(edge.To) is not { } next || column.ContainsKey(next)) continue;
                    column[next] = column[node] + 1;
                    order.Add(next);
                    queue.Enqueue(next);
                }
            }
        }
        Walk(group.Starts.Select(e => Node(e.To)).OfType<FsmGraphNode>());
        Walk(group.AnyStates.Select(e => Node(e.To)).OfType<FsmGraphNode>());
        foreach (FsmGraphNode state in states)
        {
            if (!column.ContainsKey(state)) Walk([state]);
        }

        int columns = column.Count == 0 ? 0 : column.Values.Max() + 1;
        var byColumn = Enumerable.Range(0, columns).Select(c => order.Where(n => column[n] == c).ToList()).ToList();
        var row = new Dictionary<FsmGraphNode, double>();
        foreach (List<FsmGraphNode> list in byColumn)
        {
            for (int i = 0; i < list.Count; i++) row[list[i]] = i;
        }
        var predecessors = states.ToDictionary(s => s, _ => new List<FsmGraphNode>());
        foreach (FsmGraphNode state in states)
        {
            foreach (FsmGraphEdge edge in state.Transitions)
            {
                if (Node(edge.To) is { } next && column[next] > column[state]) predecessors[next].Add(state);
            }
        }
        for (int pass = 0; pass < 2; pass++)
        {
            for (int c = 1; c < columns; c++)
            {
                List<FsmGraphNode> list = byColumn[c];
                list.Sort((a, b) => Barycenter(a).CompareTo(Barycenter(b)));
                for (int i = 0; i < list.Count; i++) row[list[i]] = i;
            }
        }
        double Barycenter(FsmGraphNode node) =>
            predecessors[node].Count == 0 ? row[node] : predecessors[node].Average(p => row[p]);

        var boxes = new Dictionary<FsmGraphNode, FsmLayoutBox>();
        double x = 0;
        double tallest = 0;
        foreach (List<FsmGraphNode> list in byColumn)
        {
            int slots = Math.Max(1, (list.Count + MaxRows - 1) / MaxRows);
            int rows = Math.Min(list.Count, MaxRows);
            tallest = Math.Max(tallest, rows);
            for (int i = 0; i < list.Count; i++)
            {
                FsmGraphNode node = list[i];
                var box = new FsmLayoutBox(node, node.Name, node.Summary,
                                           node.IsGroup ? FsmBoxKind.Group : node.IsEnd ? FsmBoxKind.End : FsmBoxKind.State)
                {
                    Rect = new Rect(x + i / MaxRows * (BoxWidth + ColumnGap * 0.5), i % MaxRows * (BoxHeight + RowGap),
                                    BoxWidth, BoxHeight)
                };
                boxes[node] = box;
                layout.Boxes.Add(box);
            }
            x += slots * BoxWidth + (slots - 1) * ColumnGap * 0.5 + ColumnGap;
        }
        double fullHeight = tallest * (BoxHeight + RowGap) - RowGap;
        foreach (List<FsmGraphNode> list in byColumn)
        {
            int rows = Math.Min(list.Count, MaxRows);
            double offset = (fullHeight - (rows * (BoxHeight + RowGap) - RowGap)) / 2;
            foreach (FsmGraphNode node in list) boxes[node].Rect = boxes[node].Rect.Translate(new Vector(0, offset));
        }

        double left = -(BoxWidth * 0.6 + ColumnGap);
        if (group.Starts.Count > 0)
        {
            var entry = new FsmLayoutBox(null, "Entry", string.Empty, FsmBoxKind.Entry)
            {
                Rect = new Rect(left, fullHeight / 2 - BoxHeight / 2, BoxWidth * 0.6, BoxHeight)
            };
            layout.Boxes.Add(entry);
            foreach (FsmGraphEdge edge in group.Starts)
            {
                if (Node(edge.To) is { } target) layout.Edges.Add(new FsmLayoutEdge(entry, boxes[target], edge.Label, edge.Kind));
            }
        }
        if (group.AnyStates.Count > 0)
        {
            var any = new FsmLayoutBox(null, "Any State", string.Empty, FsmBoxKind.AnyState)
            {
                Rect = new Rect(left, fullHeight / 2 - BoxHeight * 2 - RowGap, BoxWidth * 0.6, BoxHeight)
            };
            layout.Boxes.Add(any);
            foreach (FsmGraphEdge edge in group.AnyStates)
            {
                if (Node(edge.To) is { } target) layout.Edges.Add(new FsmLayoutEdge(any, boxes[target], edge.Label, edge.Kind));
            }
        }
        foreach (FsmGraphNode state in states)
        {
            foreach (FsmGraphEdge edge in state.Transitions)
            {
                if (Node(edge.To) is { } target && inGroup.Contains(target))
                {
                    layout.Edges.Add(new FsmLayoutEdge(boxes[state], boxes[target], edge.Label, edge.Kind));
                }
            }
        }

        foreach (IGrouping<FsmLayoutBox, FsmLayoutEdge> outgoing in layout.Edges.GroupBy(e => e.From))
        {
            List<FsmLayoutEdge> list = outgoing.OrderBy(e => e.To.Rect.Center.Y).ToList();
            for (int i = 0; i < list.Count; i++) list[i].FromPort = (i + 1.0) / (list.Count + 1);
        }
        foreach (IGrouping<FsmLayoutBox, FsmLayoutEdge> incoming in layout.Edges.GroupBy(e => e.To))
        {
            List<FsmLayoutEdge> list = incoming.OrderBy(e => e.From.Rect.Center.Y).ToList();
            for (int i = 0; i < list.Count; i++) list[i].ToPort = (i + 1.0) / (list.Count + 1);
        }

        layout.Bounds = layout.Boxes.Count == 0
            ? new Rect(0, 0, BoxWidth, BoxHeight)
            : layout.Boxes.Select(b => b.Rect).Aggregate((a, b) => a.Union(b));
        return layout;
    }
}
