using System.Diagnostics;
using System.Text;
using System.Text.Json;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Games.RE7;
using REAssetExplorer.Games.RE7.Assets;
using REAssetExplorer.Games.RE8.Assets;
using REAssetExplorer.Testing.Examples;

namespace REAssetExplorer.Testing;

class TeeWriter : TextWriter
{
    private readonly TextWriter _console;
    private readonly TextWriter _file;

    public override Encoding Encoding => Encoding.UTF8;

    public TeeWriter(TextWriter console, TextWriter file)
    {
        _console = console;
        _file = file;
    }

    public override void WriteLine(string value)
    {
        _console.WriteLine(value);
        _file.WriteLine(value);
    }

    public override void Write(char value)
    {
        _console.Write(value);
        _file.Write(value);
    }
}

public class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    public static void Main()
    {
        // Timer total
        var totalSw = Stopwatch.StartNew();

        // ---- Load Pak ----
        var sw = Stopwatch.StartNew();
        var pakFile = AssetExplorerHelper.LoadRE7Pak();
        sw.Stop();
        Console.WriteLine($"LoadRE7Pak: {sw.ElapsedMilliseconds} ms");

        // ---- Create Loader ----
        sw.Restart();
        var assetLoader = new AssetLoader(
            new RE7Provider(),
            new Dictionary<string, PakFile> { [pakFile.Name] = pakFile }
        );
        sw.Stop();
        Console.WriteLine($"AssetLoader init: {sw.ElapsedMilliseconds} ms");

        // ---- Load Asset ----
        sw.Restart();
        var result = assetLoader.LoadAsset<SceneData>(
            "environment\\scene\\chapter1\\c01_2f.scn.20",
            loadDependencies: true
        );
        sw.Stop();
        Console.WriteLine($"LoadAsset: {sw.ElapsedMilliseconds} ms");

        // ---- Resultado ----
        if (result.IsSuccess && result.Value != null)
        {
            var scene = result.Value;
            Console.WriteLine($"\nScene: {scene.InfoCount} GOs, {scene.FolderCount} folders");

            // Build parent map for walking up the chain
            var parentMap = new Dictionary<int, SceneNode>();
            void IndexParents(SceneNode n)
            {
                foreach (var child in n.Children)
                {
                    parentMap[child.Id] = n;
                    IndexParents(child);
                }
            }
            if (scene.Root != null) IndexParents(scene.Root);

            // Walk the hierarchy and find all GOs that match our target
            void WalkAndPrint(SceneNode node, int depth)
            {
                var indent = new string(' ', depth * 2);
                if (node is SceneFolderNode folder)
                    Console.WriteLine($"{indent}<folder> {folder.Folder?.Name ?? ""} path={folder.Folder?.ScenePath ?? ""}");

                if (node is SceneGameObjectNode go)
                {
                    var name = go.GameObject?.Name ?? "(unnamed)";
                    var t = go.Transform;

                    Console.WriteLine($"{indent}[{name}]");
                    if (t != null)
                        Console.WriteLine($"{indent}  pos=({t.Position.X:F3},{t.Position.Y:F3},{t.Position.Z:F3})  scale=({t.Scale.X:F3},{t.Scale.Y:F3},{t.Scale.Z:F3})");
                    else
                        Console.WriteLine($"{indent}  (no via.Transform)");

                    var meshComp = go.Components.FirstOrDefault(c => c.Name == "via.render.Mesh");
                    if (meshComp != null)
                        Console.WriteLine($"{indent}  mesh: {meshComp.Get<string>("v2") ?? "(null)"}");

                    // Dump raw via.Transform fields if transform is null but component exists
                    var xfComp = go.Components.FirstOrDefault(c => c.Name == "via.Transform");
                    if (xfComp != null && t == null)
                    {
                        Console.WriteLine($"{indent}  [via.Transform raw fields:]");
                        foreach (var f in xfComp.Fields)
                        {
                            if (f.Value is byte[] raw && raw.Length >= 4)
                            {
                                var floats = Enumerable.Range(0, raw.Length / 4)
                                    .Select(i => BitConverter.ToSingle(raw, i * 4));
                                Console.WriteLine($"{indent}    {f.Name} ({raw.Length}B): [{string.Join(", ", floats.Select(v => v.ToString("F3")))}]");
                            }
                            else Console.WriteLine($"{indent}    {f.Name}: {f.Value}");
                        }
                    }
                }

                foreach (var child in node.Children)
                    WalkAndPrint(child, depth + 1);
            }

            if (scene.Root != null) WalkAndPrint(scene.Root, 0);
        }
        else
        {
            Console.WriteLine($"\nFailed: {result.Error}");
        }

        // ---- Total ----
        totalSw.Stop();
        Console.WriteLine($"\nTOTAL: {totalSw.ElapsedMilliseconds} ms");

        Console.WriteLine("\n\nPress any key to exit...");
        Console.ReadKey();
    }

    private static object? BuildJsonNode(SceneNode? node)
    {
        if (node == null) return null;

        return node switch
        {
            SceneFolderNode folder => new
            {
                type = "folder",
                id = folder.Id,
                name = folder.Folder?.Name ?? "",
                scenePath = folder.Folder?.ScenePath ?? "",
                children = folder.Children.Select(BuildJsonNode).ToArray()
            },
            SceneGameObjectNode go => new
            {
                type = "gameObject",
                id = go.Id,
                instanceId = go.InstanceId,
                name = go.GameObject?.Name ?? "",
                tag = go.GameObject?.Tag ?? "",
                components = go.Components.Select(c => c.Name).ToArray(),
                children = go.Children.Select(BuildJsonNode).ToArray()
            },
            _ => new
            {
                type = "unknown",
                id = node.Id,
                children = node.Children.Select(BuildJsonNode).ToArray()
            }
        };
    }
}
