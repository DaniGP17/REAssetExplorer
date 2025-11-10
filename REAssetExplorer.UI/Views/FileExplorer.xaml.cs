using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using REAssetExplorer.UI.Enums;
using REAssetExplorer.UI.Helpers;
using REAssetExplorer.UI.Models;

namespace REAssetExplorer.App.Views;

public partial class FileExplorer : Window, INotifyPropertyChanged
{
    private ObservableCollection<DataGridFile> _dataGridFiles;
    private TreeItem? _currentTreeItem;
    private bool _isUpdatingFromCode;
    private Stack<TreeItem> _navigationHistory;
    private string _currentPath;
    
    public TreeManager TreeManager => TreeManager.Instance;
    
    public ObservableCollection<DataGridFile> DataGridFiles
    {
        get => _dataGridFiles;
        set
        {
            _dataGridFiles = value;
            OnPropertyChanged();
        }
    }
    
    public string CurrentPath
    {
        get => _currentPath;
        set
        {
            _currentPath = value;
            OnPropertyChanged();
        }
    }
    
    public FileExplorer()
    {
        InitializeComponent();
        _dataGridFiles = new ObservableCollection<DataGridFile>();
        _navigationHistory = new Stack<TreeItem>();
        _currentPath = string.Empty;
        DataContext = this;
        
        // Initialize DataGrid with root items
        _currentTreeItem = TreeManager.Root;
        LoadTreeItemIntoDataGrid(TreeManager.Root);
    }
    
    private void TreeView_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
    {
        if (_isUpdatingFromCode) return;
        
        if (e.NewValue is TreeItem selectedItem)
        {
            if (!selectedItem.IsFolder)
                return;
            
            // Add current to history before navigating
            if (_currentTreeItem != null && _currentTreeItem != selectedItem)
            {
                _navigationHistory.Push(_currentTreeItem);
            }
            
            _currentTreeItem = selectedItem;
            LoadTreeItemIntoDataGrid(selectedItem);
        }
    }
    
    private void DataGridRow_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (sender is DataGridRow row && row.Item is DataGridFile selectedFile)
        {
            // If it's a folder, navigate into it
            if (selectedFile.IsFolder && selectedFile.TreeItemReference != null)
            {
                // Add current to history before navigating
                if (_currentTreeItem != null)
                {
                    _navigationHistory.Push(_currentTreeItem);
                }
                
                _currentTreeItem = selectedFile.TreeItemReference;
                LoadTreeItemIntoDataGrid(selectedFile.TreeItemReference);
                
                // Sync TreeView selection
                SelectTreeItemInTreeView(selectedFile.TreeItemReference);
            }
        }
    }
    
    private void SelectTreeItemInTreeView(TreeItem targetItem)
    {
        _isUpdatingFromCode = true;
        
        try
        {
            // Build path from root to target
            var path = BuildPathToItem(targetItem);
            if (path.Count == 0) return;
            
            // Navigate through the path, expanding each level
            ItemsControl currentContainer = FileTreeView;
            TreeViewItem? targetTreeViewItem = null;
            
            foreach (var item in path)
            {
                // Force the container to generate items
                currentContainer.UpdateLayout();
                
                var treeViewItem = currentContainer.ItemContainerGenerator.ContainerFromItem(item) as TreeViewItem;
                
                if (treeViewItem == null)
                {
                    // If container is not generated, wait and try again
                    currentContainer.Dispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.Background);
                    treeViewItem = currentContainer.ItemContainerGenerator.ContainerFromItem(item) as TreeViewItem;
                }
                
                if (treeViewItem == null) break;
                
                treeViewItem.IsExpanded = true;
                treeViewItem.UpdateLayout();
                
                targetTreeViewItem = treeViewItem;
                currentContainer = treeViewItem;
            }
            
            // Select the final item
            if (targetTreeViewItem != null)
            {
                targetTreeViewItem.IsSelected = true;
                targetTreeViewItem.BringIntoView();
            }
        }
        finally
        {
            _isUpdatingFromCode = false;
        }
    }
    
    private List<TreeItem> BuildPathToItem(TreeItem targetItem)
    {
        var path = new List<TreeItem>();
        var visited = new HashSet<TreeItem>();
        
        if (FindPathRecursive(TreeManager.Root, targetItem, path, visited))
        {
            return path;
        }
        
        return new List<TreeItem>();
    }
    
    private bool FindPathRecursive(TreeItem current, TreeItem target, List<TreeItem> path, HashSet<TreeItem> visited)
    {
        if (visited.Contains(current)) return false;
        visited.Add(current);
        
        if (current == target)
        {
            return true;
        }
        
        foreach (var child in current.Children)
        {
            if (FindPathRecursive(child, target, path, visited))
            {
                path.Insert(0, child);
                return true;
            }
        }
        
        return false;
    }
    
    private void LoadTreeItemIntoDataGrid(TreeItem treeItem)
    {
        DataGridFiles.Clear();
        
        foreach (var child in treeItem.Children)
        {
            var fileItem = new DataGridFile
            {
                Name = child.Name,
                Icon = child.Icon,
                Type = child.IsFolder ? "Folder" : GetFileType(child.Name),
                TreeItemReference = child,
                IsFolder = child.IsFolder
            };
            
            // Fill metadata for files
            if (!child.IsFolder && child.Metadata != null)
            {
                var metadata = child.Metadata;
                fileItem.Size = FormatFileSize(metadata.UncompressedSize);
                fileItem.CompressedSize = metadata.IsCompressed ? FormatFileSize(metadata.CompressedSize) : "-";
                fileItem.Checksum = $"0x{metadata.Checksum:X16}";
            }
            else
            {
                fileItem.Size = "";
                fileItem.CompressedSize = "";
                fileItem.Checksum = "";
            }
            
            DataGridFiles.Add(fileItem);
        }
        
        // Update the current path
        UpdateCurrentPath(treeItem);
    }
    
    private void UpdateCurrentPath(TreeItem treeItem)
    {
        if (treeItem == TreeManager.Root)
        {
            CurrentPath = string.Empty;
            return;
        }
        
        var pathParts = new List<string>();
        BuildPathString(TreeManager.Root, treeItem, pathParts);
        CurrentPath = string.Join("/", pathParts);
    }
    
    private bool BuildPathString(TreeItem current, TreeItem target, List<string> pathParts)
    {
        if (current == target)
        {
            return true;
        }
        
        foreach (var child in current.Children)
        {
            if (BuildPathString(child, target, pathParts))
            {
                pathParts.Insert(0, child.Name);
                return true;
            }
        }
        
        return false;
    }
    
    private string GetFileType(string fileName)
    {
        var extension = System.IO.Path.GetExtension(fileName);
        return string.IsNullOrEmpty(extension) ? "File" : extension.TrimStart('.').ToUpper() + " File";
    }
    
    private string FormatFileSize(long bytes)
    {
        string[] sizes = { "B", "KB", "MB", "GB", "TB" };
        double len = bytes;
        int order = 0;
        
        while (len >= 1024 && order < sizes.Length - 1)
        {
            order++;
            len /= 1024;
        }
        
        return $"{len:0.##} {sizes[order]}";
    }
    
    // Button event handlers
    private void BackButton_Click(object sender, RoutedEventArgs e)
    {
        if (_navigationHistory.Count > 0)
        {
            var previousItem = _navigationHistory.Pop();
            _currentTreeItem = previousItem;
            LoadTreeItemIntoDataGrid(previousItem);
            SelectTreeItemInTreeView(previousItem);
        }
    }
    
    private void PathTextBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            var path = CurrentPath?.Trim();
            if (string.IsNullOrEmpty(path))
            {
                // Navigate to root
                NavigateToTreeItem(TreeManager.Root);
                return;
            }
            
            // Split the path and navigate
            var pathParts = path.Split(new[] { '/', '\\' }, StringSplitOptions.RemoveEmptyEntries);
            var targetItem = FindTreeItemByPath(pathParts);
            
            if (targetItem != null)
            {
                NavigateToTreeItem(targetItem);
            }
            else
            {
                // Path not found, restore the current path
                UpdateCurrentPath(_currentTreeItem ?? TreeManager.Root);
                StatusWindow warning = new StatusWindow(StatusType.Warning);
                warning.UpdateMessage($"Path not found: {path}");
                warning.Show();
            }
        }
    }
    
    private TreeItem? FindTreeItemByPath(string[] pathParts)
    {
        var currentNode = TreeManager.Root;
        
        foreach (var part in pathParts)
        {
            var child = currentNode.Children.FirstOrDefault(c => 
                c.Name.Equals(part, StringComparison.OrdinalIgnoreCase));
            
            if (child == null)
            {
                return null;
            }
            
            currentNode = child;
        }
        
        return currentNode;
    }
    
    private void NavigateToTreeItem(TreeItem targetItem)
    {
        // Add current to history before navigating
        if (_currentTreeItem != null && _currentTreeItem != targetItem)
        {
            _navigationHistory.Push(_currentTreeItem);
        }
        
        _currentTreeItem = targetItem;
        LoadTreeItemIntoDataGrid(targetItem);
        SelectTreeItemInTreeView(targetItem);
    }
    
    private void UpButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentTreeItem == null || _currentTreeItem == TreeManager.Root)
            return;
        
        // Find parent of current item
        var parent = FindParentTreeItem(TreeManager.Root, _currentTreeItem);
        if (parent != null)
        {
            // Add current to history before navigating
            if (_currentTreeItem != null)
            {
                _navigationHistory.Push(_currentTreeItem);
            }
            
            _currentTreeItem = parent;
            LoadTreeItemIntoDataGrid(parent);
            SelectTreeItemInTreeView(parent);
        }
    }
    
    private void RefreshButton_Click(object sender, RoutedEventArgs e)
    {
        if (_currentTreeItem != null)
        {
            LoadTreeItemIntoDataGrid(_currentTreeItem);
        }
    }
    
    private TreeItem? FindParentTreeItem(TreeItem root, TreeItem target)
    {
        foreach (var child in root.Children)
        {
            if (child == target)
            {
                return root;
            }
            
            var found = FindParentTreeItem(child, target);
            if (found != null)
            {
                return found;
            }
        }
        
        return null;
    }
    
    public event PropertyChangedEventHandler? PropertyChanged;
    
    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}