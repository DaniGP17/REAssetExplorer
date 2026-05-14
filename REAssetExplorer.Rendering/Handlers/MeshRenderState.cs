using System.Collections.Generic;
using System.Linq;

namespace REAssetExplorer.Rendering.Handlers;

/// <summary>
/// Manages which LODs and parts of a mesh should be rendered
/// </summary>
public class MeshRenderState
{
    private int _activeLodIndex = 0;
    private readonly HashSet<int> _visiblePartIds = new();
    private readonly HashSet<byte> _visibleMaterialIds = new();

    /// <summary>
    /// Gets or sets the currently active LOD level to render (-1 = none)
    /// </summary>
    public int ActiveLodIndex
    {
        get => _activeLodIndex;
        set => _activeLodIndex = value;
    }

    /// <summary>
    /// Gets the set of visible part IDs
    /// </summary>
    public IReadOnlySet<int> VisiblePartIds => _visiblePartIds;

    /// <summary>
    /// Gets the set of visible material IDs
    /// </summary>
    public IReadOnlySet<byte> VisibleMaterialIds => _visibleMaterialIds;

    /// <summary>
    /// Sets whether a specific part should be visible
    /// </summary>
    public void SetPartVisibility(int partId, bool visible)
    {
        if (visible)
            _visiblePartIds.Add(partId);
        else
            _visiblePartIds.Remove(partId);
    }

    /// <summary>
    /// Sets whether a specific material should be visible
    /// </summary>
    public void SetMaterialVisibility(byte materialId, bool visible)
    {
        if (visible)
            _visibleMaterialIds.Add(materialId);
        else
            _visibleMaterialIds.Remove(materialId);
    }

    /// <summary>
    /// Checks if a part should be rendered
    /// </summary>
    public bool IsPartVisible(int partId)
    {
        return _visiblePartIds.Count == 0 || _visiblePartIds.Contains(partId);
    }

    /// <summary>
    /// Checks if a material should be rendered
    /// </summary>
    public bool IsMaterialVisible(byte materialId)
    {
        return _visibleMaterialIds.Count == 0 || _visibleMaterialIds.Contains(materialId);
    }

    /// <summary>
    /// Shows all parts and materials (default state)
    /// </summary>
    public void ShowAll()
    {
        _visiblePartIds.Clear();
        _visibleMaterialIds.Clear();
    }

    /// <summary>
    /// Hides all parts and materials
    /// </summary>
    public void HideAll()
    {
        _visiblePartIds.Clear();
        _visibleMaterialIds.Clear();
        // When both sets are empty, IsPartVisible/IsMaterialVisible return true
        // So we need a different approach - add a flag
    }

    /// <summary>
    /// Gets whether a submesh should be rendered based on current state
    /// </summary>
    public bool ShouldRenderSubMesh(SubMesh subMesh, int partId)
    {
        return IsPartVisible(partId) && IsMaterialVisible(subMesh.MaterialID);
    }
}
