using System;
using System.Collections.Generic;
using System.Linq;
using REAssetExplorer.RenderTest2.Scene;
using Vortice.Direct3D12;

namespace REAssetExplorer.RenderTest2.Core;

/// <summary>
/// Base class for all render layers
/// </summary>
public abstract class RenderLayer
{
    public string Name { get; }
    public bool Enabled { get; set; } = true;
    public int RenderOrder { get; set; }

    protected RenderLayer(string name, int renderOrder = 0)
    {
        Name = name ?? throw new ArgumentNullException(nameof(name));
        RenderOrder = renderOrder;
    }

    /// <summary>
    /// Execute rendering for this layer
    /// </summary>
    public abstract void Execute(ID3D12GraphicsCommandList commandList, int width, int height);
}

/// <summary>
/// Scene layer for rendering 3D geometry
/// </summary>
public class SceneLayer : RenderLayer
{
    private readonly Action<ID3D12GraphicsCommandList, int, int>? _renderAction;

    public SceneLayer(string name, int renderOrder = 100, Action<ID3D12GraphicsCommandList, int, int>? renderAction = null)
        : base(name, renderOrder)
    {
        _renderAction = renderAction;
    }

    public override void Execute(ID3D12GraphicsCommandList commandList, int width, int height)
    {
        _renderAction?.Invoke(commandList, width, height);
    }
}

/// <summary>
/// Overlay layer for 2D overlays (HUD, debug info, etc.)
/// </summary>
public class OverlayLayer : RenderLayer
{
    private readonly Action<ID3D12GraphicsCommandList, int, int>? _renderAction;

    public OverlayLayer(string name, int renderOrder = 200, Action<ID3D12GraphicsCommandList, int, int>? renderAction = null)
        : base(name, renderOrder)
    {
        _renderAction = renderAction;
    }

    public override void Execute(ID3D12GraphicsCommandList commandList, int width, int height)
    {
        _renderAction?.Invoke(commandList, width, height);
    }
}

/// <summary>
/// Transparent layer for rendering transparent objects (sorted back-to-front)
/// </summary>
public class TransparentLayer : RenderLayer
{
    private readonly Action<ID3D12GraphicsCommandList, int, int>? _renderAction;

    public TransparentLayer(string name, int renderOrder = 150, Action<ID3D12GraphicsCommandList, int, int>? renderAction = null)
        : base(name, renderOrder)
    {
        _renderAction = renderAction;
    }

    public override void Execute(ID3D12GraphicsCommandList commandList, int width, int height)
    {
        _renderAction?.Invoke(commandList, width, height);
    }
}

/// <summary>
/// UI layer for rendering user interface
/// </summary>
public class UILayer : RenderLayer
{
    private readonly Action<ID3D12GraphicsCommandList, int, int>? _renderAction;

    public UILayer(string name, int renderOrder = 300, Action<ID3D12GraphicsCommandList, int, int>? renderAction = null)
        : base(name, renderOrder)
    {
        _renderAction = renderAction;
    }

    public override void Execute(ID3D12GraphicsCommandList commandList, int width, int height)
    {
        _renderAction?.Invoke(commandList, width, height);
    }
}

/// <summary>
/// Manages render layers and their execution order
/// </summary>
public class LayerManager
{
    private readonly Dictionary<string, RenderLayer> _layers = new Dictionary<string, RenderLayer>();
    private List<RenderLayer> _sortedLayers = new List<RenderLayer>();
    private bool _isDirty = false;

    /// <summary>
    /// Add a render layer
    /// </summary>
    public void AddLayer(RenderLayer layer)
    {
        if (layer == null)
            throw new ArgumentNullException(nameof(layer));

        if (_layers.ContainsKey(layer.Name))
        {
            Console.WriteLine($"Warning: Layer '{layer.Name}' already exists, replacing");
            _layers[layer.Name] = layer;
        }
        else
        {
            _layers.Add(layer.Name, layer);
        }

        _isDirty = true;
    }

    /// <summary>
    /// Remove a render layer by name
    /// </summary>
    public bool RemoveLayer(string name)
    {
        if (_layers.Remove(name))
        {
            _isDirty = true;
            return true;
        }
        return false;
    }

    /// <summary>
    /// Get a layer by name
    /// </summary>
    public RenderLayer? GetLayer(string name)
    {
        _layers.TryGetValue(name, out var layer);
        return layer;
    }

    /// <summary>
    /// Get a layer by name with type cast
    /// </summary>
    public T? GetLayer<T>(string name) where T : RenderLayer
    {
        return GetLayer(name) as T;
    }

    /// <summary>
    /// Execute all enabled layers in sorted order
    /// </summary>
    public void ExecuteAll(ID3D12GraphicsCommandList commandList, int width, int height)
    {
        if (_isDirty)
        {
            RebuildSortedList();
        }

        foreach (var layer in _sortedLayers)
        {
            if (layer.Enabled)
            {
                layer.Execute(commandList, width, height);
            }
        }
    }

    /// <summary>
    /// Clear all layers
    /// </summary>
    public void Clear()
    {
        _layers.Clear();
        _sortedLayers.Clear();
        _isDirty = false;
    }

    /// <summary>
    /// Get count of layers
    /// </summary>
    public int Count => _layers.Count;

    private void RebuildSortedList()
    {
        _sortedLayers = _layers.Values.OrderBy(l => l.RenderOrder).ToList();
        _isDirty = false;
    }
}
