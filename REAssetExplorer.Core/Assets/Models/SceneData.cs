namespace REAssetExplorer.Core.Assets.Models;

public class SceneData : AssetData
{
    public int InfoCount { get; set; }
    public int ResourceCount { get; set; }
    public int FolderCount { get; set; }
    public int PrefabCount { get; set; }
    public int UserDataCount { get; set; }
    public List<GameObjectInfo> GameObjects { get; set; } = new();
    public List<FolderInfo> Folders { get; set; } = new();
    public List<ResourceInfo> Resources { get; set; } = new();
    public List<PrefabInfo> Prefabs { get; set; } = new();
    public List<UserDataInfo> UserData { get; set; } = new();
    public RSZData? RszData { get; set; }
    public SceneNode? Root { get; set; }
    public HashSet<string> ExternalScenePaths { get; set; } = new();

    /// <summary>
    /// Injects each resolved external scene's hierarchy into the folder node that references it.
    /// Must be called after dependencies are loaded into <see cref="AssetData.ResolvedDependencies"/>.
    /// </summary>
    public void MergeExternalHierarchies()
    {
        if (Root == null) return;
        var visited = new HashSet<SceneNode>(ReferenceEqualityComparer.Instance);
        MergeNode(Root, visited);
    }

    private void MergeNode(SceneNode node, HashSet<SceneNode> visited)
    {
        if (!visited.Add(node)) return;

        if (node is SceneFolderNode folderNode && !string.IsNullOrEmpty(folderNode.Folder?.ScenePath))
        {
            var normalizedPath = folderNode.Folder.ScenePath.Replace('\\', '/').ToLowerInvariant();
            if (ResolvedDependencies.TryGetValue(normalizedPath, out var dep) &&
                dep is SceneData externalScene && externalScene.Root != null)
            {
                foreach (var child in externalScene.Root.Children)
                    folderNode.Children.Add(child);
            }
        }

        foreach (var child in node.Children)
            MergeNode(child, visited);
    }
}

public struct FolderInfo
{
    public int Id { get; set; }
    public int ParentId { get; set; }
}

public struct ResourceInfo
{
    public string Name { get; set; }
}

public struct PrefabInfo
{
    public string Name { get; set; }
}

public struct UserDataInfo
{
    public uint TypeId { get; set; }
    public string Name { get; set; }
}

public struct GameObjectInfo
{
    public Guid InstanceId { get; set; }
    public int Id { get; set; }
    public int ParentId { get; set; }
    public int ComponentCount { get; set; }
    public int PrefabId { get; set; }
}