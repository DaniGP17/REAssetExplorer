using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Rsz;

namespace REAssetExplorer.TestUI;

public class InspectorViewModel : INotifyPropertyChanged
{
    public string ObjectName { get; init; } = "";
    public string ObjectTag  { get; init; } = "";
    public bool HasContent   => Components.Count > 0;

    public ObservableCollection<ComponentViewModel> Components { get; } = new();

    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Builds an InspectorViewModel from a HierarchyNode.
    /// Returns null if the node carries no inspectable payload.
    /// </summary>
    public static InspectorViewModel? FromNode(HierarchyNode node)
    {
        if (node.ItemType == HierarchyItemType.Mesh && node.SourceMesh != null)
            return FromMesh(node.Name, node.SourceMesh);

        if (node.SourceNode is not SceneGameObjectNode goNode) return null;

        var vm = new InspectorViewModel
        {
            ObjectName = node.Name,
            ObjectTag  = goNode.GameObject?.Tag ?? "",
        };

        // via.GameObject is parsed separately into a typed struct; add it first.
        if (goNode.GameObject is { } go)
            vm.Components.Add(MakeGameObjectComponent(go));

        foreach (var component in goNode.Components)
        {
            vm.Components.Add(new ComponentViewModel
            {
                Name   = component.Name,
                Fields = component.Fields
                    .Select(f => new FieldViewModel
                    {
                        Name         = f.Name,
                        Type         = f.Type,
                        DisplayValue = FormatValue(f.Value),
                    })
                    .ToList(),
            });
        }

        return vm;
    }

    private static InspectorViewModel FromMesh(string displayName, MeshData mesh)
    {
        var vm = new InspectorViewModel
        {
            ObjectName = displayName,
            ObjectTag  = "Mesh",
        };

        // --- Mesh summary ---
        var layout = mesh.MeshLayout;
        int submeshCount = 0;
        long vertexCount = 0;
        long indexCount  = 0;
        if (layout.MeshBodies is { Count: > 0 })
        {
            var lod0 = layout.MeshBodies[0];
            if (lod0.Parts != null)
            {
                foreach (var part in lod0.Parts)
                {
                    vertexCount += part.VertexCount;
                    indexCount  += part.IndexCount;
                    submeshCount += part.Clusters?.Length ?? 0;
                }
            }
        }

        vm.Components.Add(new ComponentViewModel
        {
            Name       = "Mesh Info",
            IsExpanded = true,
            Fields = new List<FieldViewModel>
            {
                new() { Name = "Version",     Type = "u32",  DisplayValue = mesh.Header.Version.ToString() },
                new() { Name = "LOD Count",   Type = "u8",   DisplayValue = layout.LODCount.ToString() },
                new() { Name = "UV Count",    Type = "u8",   DisplayValue = layout.UVCount.ToString() },
                new() { Name = "Vertices",    Type = "u32",  DisplayValue = vertexCount.ToString("N0") },
                new() { Name = "Indices",     Type = "u32",  DisplayValue = indexCount.ToString("N0") },
                new() { Name = "Submeshes",   Type = "i32",  DisplayValue = submeshCount.ToString() },
                new() { Name = "Materials",   Type = "i32",  DisplayValue = mesh.Materials.Count.ToString() },
                new() { Name = "Joints",      Type = "i32",  DisplayValue = mesh.Joints.Count.ToString() },
                new() { Name = "Skinned",     Type = "bool", DisplayValue = mesh.IsSkinning ? "True" : "False" },
            },
        });

        // --- Bounds ---
        vm.Components.Add(new ComponentViewModel
        {
            Name       = "Bounding Box",
            IsExpanded = true,
            Fields = new List<FieldViewModel>
            {
                new() { Name = "Min", Type = "vec3", DisplayValue = $"X {layout.AabbMinX:0.###}  Y {layout.AabbMinY:0.###}  Z {layout.AabbMinZ:0.###}" },
                new() { Name = "Max", Type = "vec3", DisplayValue = $"X {layout.AabbMaxX:0.###}  Y {layout.AabbMaxY:0.###}  Z {layout.AabbMaxZ:0.###}" },
                new() { Name = "Center", Type = "vec3", DisplayValue = $"X {layout.boundingX:0.###}  Y {layout.boundingY:0.###}  Z {layout.boundingZ:0.###}" },
                new() { Name = "Radius", Type = "f32",  DisplayValue = layout.boundingRadius.ToString("0.###") },
            },
        });

        // --- Materials ---
        if (mesh.Materials.Count > 0)
        {
            var matFields = new List<FieldViewModel>();
            int idx = 0;
            foreach (var pair in mesh.Materials)
            {
                matFields.Add(new FieldViewModel
                {
                    Name = $"[{idx}]",
                    Type = "string",
                    DisplayValue = pair.Value,
                });
                idx++;
            }
            vm.Components.Add(new ComponentViewModel { Name = "Materials", Fields = matFields });
        }

        return vm;
    }

    private static ComponentViewModel MakeGameObjectComponent(ViaGameObject go) => new()
    {
        Name       = "via.GameObject",
        IsExpanded = true,
        Fields     = new List<FieldViewModel>
        {
            new() { Name = "Name",       Type = "string", DisplayValue = go.Name },
            new() { Name = "Tag",        Type = "string", DisplayValue = go.Tag },
            new() { Name = "UpdateSelf", Type = "bool",   DisplayValue = go.UpdateSelf ? "True" : "False" },
            new() { Name = "DrawSelf",   Type = "bool",   DisplayValue = go.DrawSelf   ? "True" : "False" },
            new() { Name = "TimeScale",  Type = "f32",    DisplayValue = go.TimeScale.ToString("0.###") },
        },
    };

    // ── Value formatting ────────────────────────────────────────────────────

    private static string FormatValue(object? value) => value switch
    {
        null             => "null",
        string s         => s.Length == 0 ? "(empty)" : s,
        float f          => f.ToString("0.###"),
        bool b           => b ? "True" : "False",
        int i            => i.ToString(),
        uint u           => u.ToString(),
        byte by          => by.ToString(),
        RszVec2 v        => $"X {v.X:0.###}  Y {v.Y:0.###}",
        RszVec3 v        => $"X {v.X:0.###}  Y {v.Y:0.###}  Z {v.Z:0.###}",
        RszVec4 v        => $"X {v.X:0.###}  Y {v.Y:0.###}  Z {v.Z:0.###}  W {v.W:0.###}",
        RszQuaternion q  => $"X {q.X:0.###}  Y {q.Y:0.###}  Z {q.Z:0.###}  W {q.W:0.###}",
        RszColor c       => $"R {c.R}  G {c.G}  B {c.B}  A {c.A}",
        RszGuid g        => g.Value.ToString(),
        RszObjectRef r   => $"→ [{r.InstanceIndex}]",
        RszMat4          => "(matrix 4×4)",
        byte[] arr       => arr.Length == 0 ? "[]"
                            : $"[{arr.Length} B]  {BitConverter.ToString(arr[..Math.Min(8, arr.Length)])}",
        Array arr        => $"[{arr.Length} items]",
        _                => value.ToString() ?? "null",
    };
}

// ── Component view model ────────────────────────────────────────────────────

public class ComponentViewModel : INotifyPropertyChanged
{
    private bool _isExpanded;

    public string Name { get; init; } = "";
    public List<FieldViewModel> Fields { get; init; } = new();
    public bool HasFields => Fields.Count > 0;

    public bool IsExpanded
    {
        get => _isExpanded;
        set { if (_isExpanded == value) return; _isExpanded = value; OnPropertyChanged(); }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? n = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(n));
}

// ── Field view model ────────────────────────────────────────────────────────

public class FieldViewModel
{
    public string Name         { get; init; } = "";
    public string Type         { get; init; } = "";
    public string DisplayValue { get; init; } = "";
}
