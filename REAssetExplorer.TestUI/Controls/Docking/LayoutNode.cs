namespace REAssetExplorer.TestUI.Controls.Docking;

public abstract class LayoutNode
{
    public SplitNode? Parent { get; set; }
}

public class PanelLeaf : LayoutNode
{
    public FloatingPanel? Panel { get; set; }
}

public enum SplitOrientation { Horizontal, Vertical }

public class SplitNode : LayoutNode
{
    public SplitOrientation Orientation { get; set; }
    public double           Ratio       { get; set; } = 0.5;
    public LayoutNode       First       { get; set; } = null!;
    public LayoutNode       Second      { get; set; } = null!;
}

public enum DropZone { Left, Top, Right, Bottom }
