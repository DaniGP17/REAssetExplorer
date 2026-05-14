namespace REAssetExplorer.Core.Assets.Models;

/// <summary>Typed representation of the via.GameObject RSZ component.</summary>
public class ViaGameObject
{
    public string Name { get; set; } = "";
    public string Tag { get; set; } = "";
    public bool UpdateSelf { get; set; }
    public bool DrawSelf { get; set; }
    public float TimeScale { get; set; }

    public static ViaGameObject Parse(RszClass rszClass)
    {
        return new ViaGameObject
        {
            Name       = rszClass.Get<string>("v0") ?? "",
            Tag        = rszClass.Get<string>("v1") ?? "",
            UpdateSelf = rszClass.GetBool("v2"),
            DrawSelf   = rszClass.GetBool("v3"),
            TimeScale  = rszClass.GetFloat("v4"),
        };
    }
}
