using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace REAssetExplorer.UI.Models;

/// <summary>
/// Represents a node in the mesh hierarchy tree with visibility control
/// </summary>
public class MeshHierarchyNode : INotifyPropertyChanged
{
    private bool _isVisible = true;
    private bool? _isChecked = true;

    public string DisplayName { get; set; } = string.Empty;
    public MeshNodeType NodeType { get; set; }
    public int Index { get; set; }
    
    // For LOD nodes
    public int LodIndex { get; set; }
    
    // For Part nodes
    public int PartId { get; set; }
    
    // For Cluster nodes
    public byte MaterialId { get; set; }
    public string MaterialName { get; set; } = string.Empty;
    
    public ObservableCollection<MeshHierarchyNode> Children { get; } = new();
    public MeshHierarchyNode? Parent { get; set; }

    /// <summary>
    /// Gets or sets whether this node is visible (affects rendering)
    /// </summary>
    public bool IsVisible
    {
        get => _isVisible;
        set
        {
            if (_isVisible != value)
            {
                _isVisible = value;
                OnPropertyChanged();
                
                // Update children
                foreach (var child in Children)
                {
                    child.IsVisible = value;
                }
                
                // Update parent checkbox state
                Parent?.UpdateCheckState();
            }
        }
    }

    /// <summary>
    /// Checkbox state (true/false/indeterminate)
    /// </summary>
    public bool? IsChecked
    {
        get => _isChecked;
        set
        {
            if (_isChecked != value)
            {
                _isChecked = value;
                OnPropertyChanged();
                
                // Update IsVisible based on checkbox
                if (value.HasValue)
                {
                    IsVisible = value.Value;
                    
                    // When unchecking, also uncheck all children
                    // When checking, also check all children
                    SetChildrenCheckState(value.Value);
                }
                
                // Update parent
                Parent?.UpdateCheckState();
            }
        }
    }

    /// <summary>
    /// Sets the check state of all children recursively
    /// </summary>
    private void SetChildrenCheckState(bool isChecked)
    {
        foreach (var child in Children)
        {
            child.IsChecked = isChecked;
        }
    }

    /// <summary>
    /// Updates the checkbox state based on children
    /// </summary>
    private void UpdateCheckState()
    {
        if (Children.Count == 0)
            return;

        bool allChecked = true;
        bool allUnchecked = true;

        foreach (var child in Children)
        {
            if (child.IsChecked == true)
                allUnchecked = false;
            else if (child.IsChecked == false)
                allChecked = false;
            else
            {
                allChecked = false;
                allUnchecked = false;
            }
        }

        if (allChecked)
            _isChecked = true;
        else if (allUnchecked)
            _isChecked = false;
        else
            _isChecked = null;

        OnPropertyChanged(nameof(IsChecked));
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}

/// <summary>
/// Types of nodes in the mesh hierarchy
/// </summary>
public enum MeshNodeType
{
    Lod,
    Part,
    Cluster
}
