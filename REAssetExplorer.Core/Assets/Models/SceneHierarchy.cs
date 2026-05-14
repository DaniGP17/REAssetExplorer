namespace REAssetExplorer.Core.Assets.Models;

public abstract class SceneNode
{
    public int Id { get; set; }
    public int ParentId { get; set; }
    public List<SceneNode> Children { get; set; } = new();
}

public class SceneFolderNode : SceneNode
{
    public ViaFolder? Folder { get; set; }
}

public class SceneGameObjectNode : SceneNode
{
    public Guid InstanceId { get; set; }
    public ViaGameObject? GameObject { get; set; }
    public ViaTransform?  Transform  { get; set; }
    public List<RszClass> Components { get; set; } = new();
}
