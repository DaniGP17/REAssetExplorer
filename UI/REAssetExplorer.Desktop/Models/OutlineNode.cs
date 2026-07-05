using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using CommunityToolkit.Mvvm.ComponentModel;

namespace REAssetExplorer.Desktop.Models;

// OverrideKey is set on replaceable asset references and live parameters ("param:");
// Components names a parameter's values ("Min,Max").
public sealed record OutlineProperty(string Section, string Key, string Value, string OverrideKey, string Components);

public sealed partial class OutlineNode(string kind, string name, string detail, string key, int depth, OutlineNode? parent)
    : ObservableObject, ITreeNode
{
    private static readonly IBrush DefaultIconBrush = new ImmutableSolidColorBrush(Color.Parse("#C8C8C8"));
    private static readonly IBrush ObjectBrush = new ImmutableSolidColorBrush(Color.Parse("#6FA3E8"));
    private static readonly IBrush FolderBrush = new ImmutableSolidColorBrush(Color.Parse("#D6B25E"));
    private static readonly IBrush JointBrush = new ImmutableSolidColorBrush(Color.Parse("#D8D2C0"));
    private static readonly IBrush TextBrush = new ImmutableSolidColorBrush(Color.Parse("#D4D4D4"));
    private static readonly IBrush LinkBrush = new ImmutableSolidColorBrush(Color.Parse("#7EA6E0"));
    private static readonly IBrush MissingBrush = new ImmutableSolidColorBrush(Color.Parse("#E05A5A"));
    private static readonly IBrush EventBrush = new ImmutableSolidColorBrush(Color.Parse("#7CC46A"));

    public string Kind { get; } = kind;
    public string Name { get; } = name;
    public string Detail { get; } = detail;
    // Set on nodes the viewport can select: "lod" nodes carry "L", "submesh"
    // nodes "L:S", scene game objects "<scene path>|<node index>".
    public string Key { get; } = key;
    public int Depth { get; } = depth;
    public OutlineNode? Parent { get; } = parent;
    public List<OutlineNode> Children { get; } = [];
    public List<OutlineProperty> Properties { get; } = [];

    [ObservableProperty]
    private bool isExpanded;

    // Rows of these outlines get visibility toggles.
    public bool InScene { get; } = parent?.InScene ?? kind is "scene" or "prefab" or "effect" or "collision" or "gui" or "aimap" or
                                                       "character";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(EffectiveHidden))]
    [NotifyPropertyChangedFor(nameof(HiddenByParent))]
    private bool isHidden;

    public bool EffectiveHidden => IsHidden || (Parent?.EffectiveHidden ?? false);
    public bool HiddenByParent => !IsHidden && (Parent?.EffectiveHidden ?? false);

    public void RefreshVisibility()
    {
        OnPropertyChanged(nameof(EffectiveHidden));
        OnPropertyChanged(nameof(HiddenByParent));
        foreach (OutlineNode child in Children) child.RefreshVisibility();
    }

    public bool CanExpand => Children.Count > 0;
    public Thickness Indent => new(Depth * 14, 0, 0, 0);
    public bool IsLink => Kind == "sceneref";
    public FontStyle NameStyle => IsLink ? FontStyle.Italic : FontStyle.Normal;
    public IBrush NameBrush => Kind switch { "sceneref" => LinkBrush, "missing" => MissingBrush, _ => TextBrush };

    public string KindLabel => Kind switch
    {
        "scene" => "Scene",
        "sceneref" => "Linked Scene",
        "folder" => "Folder",
        "gameobject" => "GameObject",
        "mesh" => "Mesh",
        "mdf" => "Material File",
        "material" => "Material",
        "lod" => "LOD",
        "submesh" => "Submesh",
        "joint" => "Joint",
        "motlist" => "Motion List",
        "motion" => "Motion",
        "soundbank" => "Sound Bank",
        "soundpackage" => "Sound Package",
        "event" => "Event",
        "action" => "Action",
        "sound" => "Sound",
        "container" => "Container",
        "music" => "Music",
        "wem" => "Audio File",
        "language" => "Language",
        "object" => "Object",
        "group" => "Group",
        "effect" => "Effect",
        "emitter" => "Emitter",
        "texture" => "Texture",
        "rendertarget" => "Render Target",
        "userdata" => "User Data",
        "prefab" => "Prefab",
        "collision" => "Collision Mesh",
        "rendermesh" => "Render Mesh",
        "layer" => "Layer",
        "userobject" => "Object",
        "movie" => "Movie",
        "image" => "Image",
        "mip" => "Mip",
        "uvs" => "UV Sequence",
        "sequence" => "Sequence",
        "pattern" => "Pattern",
        "messages" => "Message File",
        "gui" => "GUI",
        "guielement" => Detail,
        "message" => "Message",
        _ => Kind
    };

    public Geometry? Icon => Icons.Get(Kind switch
    {
        "scene" => "IconScene",
        "sceneref" => "IconLink",
        "folder" or "group" => "IconFolder",
        "gameobject" => "IconGameObject",
        "mesh" => "IconCube",
        "mdf" or "material" => "IconMaterial",
        "lod" => "IconLayers",
        "submesh" => "IconSubmesh",
        "joint" => "IconBone",
        "motlist" or "motion" => "IconMotion",
        "soundbank" or "soundpackage" or "sound" or "wem" => "IconSound",
        "event" => "IconPlay",
        "action" => "IconArrowRight",
        "container" => "IconLayers",
        "music" => "IconMotion",
        "language" => "IconText",
        "missing" => "IconAlert",
        "effect" or "emitter" => "IconEffect",
        "texture" or "image" or "uvs" or "pattern" or "rendertarget" => "IconImage",
        "movie" => "IconMovie",
        "prefab" or "rendermesh" => "IconCube",
        "collision" => "IconCollision",
        "aimap" => "IconNavigation",
        "aigroup" or "ailayer" => "IconLayers",
        "ailinks" => "IconLink",
        "fsm" or "fsmgroup" => "IconStateMachine",
        "character" => "IconGameObject",
        "charpart" => "IconCube",
        "fsmstate" => "IconPlay",
        "motbank" => "IconBank",
        "layer" => "IconLayers",
        "userdata" or "userobject" => "IconUserData",
        "mip" or "sequence" => "IconLayers",
        "messages" or "message" => "IconText",
        "gui" => "IconImage",
        "guielement" => Detail switch
        {
            "Texture" or "TextureSet" or "Scale9Grid" or "Material" => "IconImage",
            "Text" or "MaterialText" => "IconText",
            "Rect" or "Circle" or "HitArea" => "IconSubmesh",
            "View" => "IconFrame",
            _ => "IconFolder"
        },
        _ => "IconFile"
    });

    public IBrush IconBrush => Kind switch
    {
        "gameobject" => ObjectBrush,
        "folder" or "group" => FolderBrush,
        "joint" => JointBrush,
        "missing" => MissingBrush,
        "sceneref" => LinkBrush,
        "material" or "mdf" => AssetKinds.Brush(AssetKind.Material),
        "mesh" or "submesh" => AssetKinds.Brush(AssetKind.Mesh),
        "scene" => AssetKinds.Brush(AssetKind.Scene),
        "motion" or "motlist" => AssetKinds.Brush(AssetKind.MotionList),
        "soundbank" or "soundpackage" or "sound" or "wem" or "music" => AssetKinds.Brush(AssetKind.Sound),
        "event" => EventBrush,
        "effect" or "emitter" => AssetKinds.Brush(AssetKind.Effect),
        "texture" or "image" or "mip" => AssetKinds.Brush(AssetKind.Texture),
        "rendertarget" => AssetKinds.Brush(AssetKind.RenderTarget),
        "movie" => AssetKinds.Brush(AssetKind.Movie),
        "prefab" => AssetKinds.Brush(AssetKind.Prefab),
        "collision" or "layer" => AssetKinds.Brush(AssetKind.Collision),
        "aimap" or "aigroup" or "ailinks" or "ailayer" => AssetKinds.Brush(AssetKind.AiMap),
        "fsm" or "fsmgroup" or "fsmstate" => AssetKinds.Brush(AssetKind.StateMachine),
        "character" => ObjectBrush,
        "charpart" => AssetKinds.Brush(AssetKind.Mesh),
        "motbank" => AssetKinds.Brush(AssetKind.MotionBank),
        "rendermesh" => AssetKinds.Brush(AssetKind.Mesh),
        "userdata" or "userobject" => AssetKinds.Brush(AssetKind.UserData),
        "uvs" or "sequence" or "pattern" => AssetKinds.Brush(AssetKind.UvSequence),
        "messages" or "message" => AssetKinds.Brush(AssetKind.Text),
        "gui" => AssetKinds.Brush(AssetKind.Gui),
        "guielement" => Detail is "Panel" or "View" or "Window" or "SelectItem" or "ScrollList" or "ScrollBar" or "ScrollGrid" or
            "SimpleList" ? FolderBrush : AssetKinds.Brush(AssetKind.Gui),
        _ => DefaultIconBrush
    };

    public IEnumerable<OutlineNode> DescendantsAndSelf()
    {
        yield return this;
        foreach (OutlineNode child in Children)
        {
            foreach (OutlineNode node in child.DescendantsAndSelf()) yield return node;
        }
    }

    public static List<OutlineNode> Parse(string text)
    {
        var roots = new List<OutlineNode>();
        var stack = new List<OutlineNode>();
        OutlineNode? last = null;
        foreach (string line in text.Split('\n'))
        {
            string[] f = line.Split('\t');
            if (f.Length >= 5 && f[0] == "N" && int.TryParse(f[1], out int depth))
            {
                while (stack.Count > 0 && stack[^1].Depth >= depth) stack.RemoveAt(stack.Count - 1);
                OutlineNode? parent = stack.Count > 0 ? stack[^1] : null;
                string key = f.Length >= 6 ? f[5] : string.Empty;
                var node = new OutlineNode(f[2], f[3], f[4], key, parent == null ? 0 : parent.Depth + 1, parent);
                if (f.Length >= 7 && f[6] == "1") node.IsHidden = true;
                (parent?.Children ?? roots).Add(node);
                stack.Add(node);
                last = node;
            }
            else if (f.Length >= 4 && f[0] == "P" && last != null)
            {
                last.Properties.Add(new OutlineProperty(f[1], f[2], f[3], f.Length >= 5 ? f[4] : string.Empty,
                                                        f.Length >= 6 ? f[5] : string.Empty));
            }
        }
        return roots;
    }
}
