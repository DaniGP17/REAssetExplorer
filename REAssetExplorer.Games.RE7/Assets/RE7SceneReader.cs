using System.Linq;
using System.Text;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Rsz;

namespace REAssetExplorer.Games.RE7.Assets;

public class RE7SceneReader : IAssetReader<SceneData>
{
    public AssetType AssetType => AssetType.Scene;
    
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".scn.20",
        ".scn"
    };

    /// <inheritdoc/>
    public IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        if (header[0] != 'S' || header[1] != 'C' || header[2] != 'N')
        {
            return false;
        }

        return true;
    }

    public Result<SceneData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var scn = new SceneData();
            
            using var br = new BinaryReader(new MemoryStream(data.ToArray()));
            
            br.ReadBytes(4);
            scn.InfoCount = br.ReadInt32();
            scn.ResourceCount = br.ReadInt32();
            scn.FolderCount = br.ReadInt32();
            scn.PrefabCount = br.ReadInt32();
            scn.UserDataCount = br.ReadInt32();
            
            ulong folderInfoOffset = br.ReadUInt64();
            ulong resourceInfoOffset = br.ReadUInt64();
            ulong prefabInfoOffset = br.ReadUInt64();
            ulong userDataInfoOffset = br.ReadUInt64();
            ulong dataOffet = br.ReadUInt64();
            
            for (int i = 0; i < scn.InfoCount; i++)
            {
                scn.GameObjects.Add(new GameObjectInfo
                {
                    InstanceId = new Guid(br.ReadBytes(16)),
                    Id = br.ReadInt32(),
                    ParentId = br.ReadInt32(),
                    ComponentCount = br.ReadInt32(),
                    PrefabId = br.ReadInt32()
                });
            }
            
            br.BaseStream.Seek((long)folderInfoOffset, SeekOrigin.Begin);
            for (int i = 0; i < scn.FolderCount; i++)
            {
                scn.Folders.Add(new FolderInfo
                {
                    Id = br.ReadInt32(),
                    ParentId = br.ReadInt32()
                });
            }
            
            br.BaseStream.Seek((long)resourceInfoOffset, SeekOrigin.Begin);
            for (int i = 0; i < scn.ResourceCount; i++)
            {
                scn.Resources.Add(new ResourceInfo
                {
                    Name = br.ReadNullTerminatedString(br.ReadInt64(), Encoding.Unicode)
                });
            }
            
            br.BaseStream.Seek((long)prefabInfoOffset, SeekOrigin.Begin);
            for (int i = 0; i < scn.PrefabCount; i++)
            {
                scn.Prefabs.Add(new PrefabInfo
                {
                    Name = br.ReadNullTerminatedString(br.ReadInt64(), Encoding.Unicode)
                });
            }
            
            br.BaseStream.Seek((long)userDataInfoOffset, SeekOrigin.Begin);
            for (int i = 0; i < scn.UserDataCount; i++)
            {
                var userDataInfo = new UserDataInfo();
                userDataInfo.TypeId = br.ReadUInt32();
                br.ReadBytes(4); // padding
                userDataInfo.Name = br.ReadNullTerminatedString(br.ReadInt64(), Encoding.Unicode);
                scn.UserData.Add(userDataInfo);
            }
            
            br.BaseStream.Seek((long)dataOffet, SeekOrigin.Begin);

            var rsz = new RE7RSZReader();
            br.BaseStream.Seek((long)dataOffet, SeekOrigin.Begin);
            var rszResult = rsz.Read(new byte[] {}, fileName, br);
            if (!rszResult.IsSuccess)
                return Result<SceneData>.Failure(rszResult.Error, rszResult.Exception);
            scn.RszData = rszResult.Value;
            BuildHierarchy(scn);

            return Result<SceneData>.Success(scn);
        }
        catch (Exception ex)
        {
            return Result<SceneData>.Failure($"Failed to read scene '{fileName}': {ex.Message}", ex);
        }
    }
    
    public bool ResolveDependencies(SceneData asset) => true;

    private static void BuildHierarchy(SceneData scn)
    {
        var rsz = scn.RszData!;
        var nodes = new Dictionary<int, SceneNode>();

        for (int f = 0; f < scn.Folders.Count; f++)
        {
            var folder = scn.Folders[f];
            var gmIndex = scn.RszData.ObjectTable[folder.Id];
            var node = new SceneFolderNode { Id = folder.Id, ParentId = folder.ParentId };

            if (gmIndex > 0 && gmIndex < rsz.Classes.Count && rsz.Classes[gmIndex] != null)
                node.Folder = ViaFolder.Parse(rsz.Classes[gmIndex]);

            if (node.Folder != null && !string.IsNullOrEmpty(node.Folder.ScenePath) && node.Folder.ScenePath.Contains(".scn"))
                scn.ExternalScenePaths.Add(node.Folder.ScenePath);

            nodes[folder.Id] = node;
        }

        foreach (var go in scn.GameObjects)
        {
            var node = new SceneGameObjectNode
            {
                Id = go.Id,
                ParentId = go.ParentId,
                InstanceId = go.InstanceId,
            };

            if (go.Id < rsz.ObjectTable.Count)
            {
                int goClassIdx = rsz.ObjectTable[go.Id];
                if (goClassIdx > 0 && goClassIdx < rsz.Classes.Count && rsz.Classes[goClassIdx] != null)
                    node.GameObject = ViaGameObject.Parse(rsz.Classes[goClassIdx]);
            }

            for (int c = 0; c < go.ComponentCount; c++)
            {
                int compTableIdx = go.Id + 1 + c;
                if (compTableIdx >= rsz.ObjectTable.Count) break;
                int classIdx = rsz.ObjectTable[compTableIdx];
                if (classIdx > 0 && classIdx < rsz.Classes.Count && rsz.Classes[classIdx] != null)
                    node.Components.Add(rsz.Classes[classIdx]);
            }

            var transformComp = node.Components.FirstOrDefault(c => c.Name == "via.Transform");
            if (transformComp != null)
                node.Transform = ViaTransform.Parse(transformComp);

            nodes[go.Id] = node;
        }

        var root = new SceneFolderNode { Id = -1, ParentId = -1 };
        foreach (var node in nodes.Values)
        {
            if (node.ParentId < 0 || !nodes.TryGetValue(node.ParentId, out var parent))
                root.Children.Add(node);
            else
                parent.Children.Add(node);
        }

        scn.Root = root;
    }
}