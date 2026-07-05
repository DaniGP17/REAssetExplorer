using Avalonia.Media;
using Avalonia.Media.Immutable;

namespace REAssetExplorer.Desktop.Models;

public enum AssetKind
{
    Folder,
    Mesh,
    Texture,
    RenderTarget,
    Material,
    Scene,
    Prefab,
    Effect,
    UvSequence,
    MotionList,
    Motion,
    MotionBank,
    StateMachine,
    Terrain,
    Collision,
    AiMap,
    Foliage,
    Shader,
    UserData,
    Gui,
    Sound,
    Movie,
    Text,
    Other
}

public static class AssetKinds
{
    private sealed record Info(string Label, string Icon, Color Color);

    private static readonly Dictionary<AssetKind, Info> Table = new()
    {
        [AssetKind.Folder] = new("Folder", "IconFolder", Color.Parse("#D6B25E")),
        [AssetKind.Mesh] = new("Mesh", "IconCube", Color.Parse("#4F9BE0")),
        [AssetKind.Texture] = new("Texture", "IconImage", Color.Parse("#B07CE0")),
        [AssetKind.RenderTarget] = new("Render Target", "IconImage", Color.Parse("#8C7CB8")),
        [AssetKind.Material] = new("Material", "IconMaterial", Color.Parse("#E09A3A")),
        [AssetKind.Scene] = new("Scene", "IconScene", Color.Parse("#39B8C8")),
        [AssetKind.Prefab] = new("Prefab", "IconCube", Color.Parse("#4CB86A")),
        [AssetKind.Effect] = new("Effect", "IconEffect", Color.Parse("#E0584F")),
        [AssetKind.UvSequence] = new("UV Sequence", "IconImage", Color.Parse("#D98A6A")),
        [AssetKind.MotionList] = new("Motion List", "IconMotion", Color.Parse("#E06FA8")),
        [AssetKind.Motion] = new("Motion", "IconMotion", Color.Parse("#C070E0")),
        [AssetKind.MotionBank] = new("Motion Bank", "IconBank", Color.Parse("#D07AB0")),
        [AssetKind.StateMachine] = new("State Machine", "IconStateMachine", Color.Parse("#E0A040")),
        [AssetKind.Terrain] = new("Terrain", "IconTerrain", Color.Parse("#8DBF45")),
        [AssetKind.Collision] = new("Collision Mesh", "IconCollision", Color.Parse("#45C8B0")),
        [AssetKind.AiMap] = new("AI Map", "IconNavigation", Color.Parse("#6CC050")),
        [AssetKind.Foliage] = new("Foliage", "IconLeaf", Color.Parse("#5FAF5F")),
        [AssetKind.Shader] = new("Shader", "IconShader", Color.Parse("#6C7BE0")),
        [AssetKind.UserData] = new("User Data", "IconUserData", Color.Parse("#9AA5B5")),
        [AssetKind.Gui] = new("GUI", "IconImage", Color.Parse("#D0C05A")),
        [AssetKind.Sound] = new("Sound", "IconSound", Color.Parse("#5AB8D0")),
        [AssetKind.Movie] = new("Movie", "IconMovie", Color.Parse("#D0905A")),
        [AssetKind.Text] = new("Message", "IconText", Color.Parse("#A0B06A")),
        [AssetKind.Other] = new("File", "IconFile", Color.Parse("#8A8A8A")),
    };

    private static readonly Dictionary<AssetKind, IBrush> Brushes =
        Table.ToDictionary(p => p.Key, p => (IBrush)new ImmutableSolidColorBrush(p.Value.Color));

    public static AssetKind FromExtension(string extension) => extension.ToLowerInvariant() switch
    {
        "mesh" => AssetKind.Mesh,
        "tex" => AssetKind.Texture,
        "rtex" => AssetKind.RenderTarget,
        "mov" => AssetKind.Movie,
        "mdf2" => AssetKind.Material,
        "scn" => AssetKind.Scene,
        "pfb" => AssetKind.Prefab,
        "efx" => AssetKind.Effect,
        "uvs" => AssetKind.UvSequence,
        "motlist" => AssetKind.MotionList,
        "mot" => AssetKind.Motion,
        "motbank" => AssetKind.MotionBank,
        "motfsm2" or "fsmv2" => AssetKind.StateMachine,
        "terr" => AssetKind.Terrain,
        "mcol" => AssetKind.Collision,
        "aimap" or "ainvm" or "aiwayp" or "aivspc" => AssetKind.AiMap,
        "fol" => AssetKind.Foliage,
        "sdf" or "mmtr" or "mmtrs" or "fx" => AssetKind.Shader,
        "user" => AssetKind.UserData,
        "gui" => AssetKind.Gui,
        "bnk" or "pck" or "sbnk" or "spck" => AssetKind.Sound,
        "msg" => AssetKind.Text,
        _ => AssetKind.Other
    };

    public static IReadOnlyList<AssetKind> FileKinds { get; } =
        Enum.GetValues<AssetKind>().Where(k => k != AssetKind.Folder).ToArray();

    public static string Label(AssetKind kind) => Table[kind].Label;
    public static string IconKey(AssetKind kind) => Table[kind].Icon;
    public static IBrush Brush(AssetKind kind) => Brushes[kind];

    public static bool CanPreview(AssetKind kind) => kind is AssetKind.Mesh or AssetKind.Scene;
    public static bool CanOutline(AssetKind kind) =>
        kind is AssetKind.Mesh or AssetKind.Scene or AssetKind.Material or AssetKind.MotionList or AssetKind.Sound or
            AssetKind.Effect or AssetKind.Texture or AssetKind.UvSequence or AssetKind.RenderTarget or AssetKind.Movie or
            AssetKind.UserData or AssetKind.Prefab or AssetKind.Collision or AssetKind.AiMap or AssetKind.Text or AssetKind.Gui or
            AssetKind.StateMachine or AssetKind.MotionBank;
}
