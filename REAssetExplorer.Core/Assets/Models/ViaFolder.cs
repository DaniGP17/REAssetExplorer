namespace REAssetExplorer.Core.Assets.Models;

/// <summary>Typed representation of the via.Folder RSZ component.</summary>
public class ViaFolder
{
    public string Name { get; set; } = "";
    public string Tag { get; set; } = "";
    public bool UpdateSelf { get; set; }
    public bool DrawSelf { get; set; }
    public bool Modified { get; set; }
    public string ScenePath { get; set; } = "";

    public static ViaFolder Parse(RszClass rszClass)
    {
        return new ViaFolder
        {
            Name       = rszClass.Get<string>("v0") ?? "",
            Tag        = rszClass.Get<string>("v1") ?? "",
            UpdateSelf = rszClass.GetBool("v2"),
            DrawSelf   = rszClass.GetBool("v3"),
            Modified   = rszClass.GetBool("v4"),
            ScenePath  = rszClass.Get<string>("v5") ?? "",
        };
    }
}
