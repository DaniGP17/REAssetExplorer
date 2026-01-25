using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.UI;
using REAssetExplorer.UI.Enums;
using REAssetExplorer.UI.Helpers;
using REAssetExplorer.UI.Models;
using REAssetExplorer.UI.Views;
using Wpf.Ui.Controls;
using TreeViewItem = System.Windows.Controls.TreeViewItem;

namespace REAssetExplorer.App.Views;

public partial class FileExplorer : FluentWindow, INotifyPropertyChanged
{
    private ObservableCollection<DataGridFile> _dataGridFiles;
    private TreeItem? _currentTreeItem;
    private bool _isUpdatingFromCode;
    private Stack<TreeItem> _navigationHistory;
    private string _currentPath;
    private bool _isSearchMode;
    private List<DataGridFile> _allFilesCache;
    
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
        _allFilesCache = new List<DataGridFile>();
        _isSearchMode = false;
        DataContext = this;
        
        // Initialize DataGrid with root items
        _currentTreeItem = TreeManager.Root;
        LoadTreeItemIntoDataGrid(TreeManager.Root);
        BuildAllFilesCache();
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
            else if (!selectedFile.IsFolder)
            {
                OpenFileViewer(selectedFile);
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
        // Exit search mode when navigating normally
        if (_isSearchMode)
        {
            _isSearchMode = false;
        }
        
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
                fileItem.SizeBytes = metadata.UncompressedSize;
                fileItem.CompressedSize = metadata.IsCompressed ? FormatFileSize(metadata.CompressedSize) : "-";
                fileItem.CompressedSizeBytes = metadata.IsCompressed ? metadata.CompressedSize : 0;
                fileItem.Checksum = $"0x{metadata.Checksum:X16}";
                fileItem.Compression = metadata.Compression.ToString();
            }
            else
            {
                fileItem.Size = "";
                fileItem.SizeBytes = 0;
                fileItem.CompressedSize = "";
                fileItem.CompressedSizeBytes = 0;
                fileItem.Checksum = "";
                fileItem.Compression = "";
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
    
    // Context menu handlers
    private void MenuItem_View_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null || selectedFile.IsFolder)
        {
            return;
        }

        OpenFileViewer(selectedFile);
    }
    
    private void OpenFileViewer(DataGridFile selectedFile)
    {
        PakEntry? pakEntry = selectedFile.TreeItemReference?.Metadata?.PakEntry;
        
        if (pakEntry == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "File metadata not available.");
            errorWindow.ShowDialog();
            return;
        }
        
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "No game loaded.");
            errorWindow.ShowDialog();
            return;
        }
        
        var assetRegistry = gameProvider.AssetReaders;
        
        // Try to get appropriate reader based on file extension
        var textureReader = assetRegistry.GetReader<REAssetExplorer.Core.Assets.Models.TextureData>(selectedFile.Name);
        var materialReader = assetRegistry.GetReader<REAssetExplorer.Core.Assets.Models.MaterialData>(selectedFile.Name);
        var audioBankReader = assetRegistry.GetReader<REAssetExplorer.Core.Assets.Models.AudioBankData>(selectedFile.Name);
        
        if (textureReader != null)
        {
            var textureViewer = new TextureViewerWindow(selectedFile.Name, pakEntry);
            textureViewer.Show();
        }
        else if (materialReader != null)
        {
            // Read material data and open Material Viewer
            try
            {
                var pakFile = FindPakFileForEntry(pakEntry.Value);
                if (pakFile == null)
                {
                    var errorWindow = new StatusWindow(StatusType.Error, "PAK file not found for this material.");
                    errorWindow.ShowDialog();
                    return;
                }

                byte[] materialData = gameProvider.PakReader.ExtractFile(pakFile, pakEntry.Value);
                var result = materialReader.Read(materialData, selectedFile.Name);

                if (!result.IsSuccess)
                {
                    var errorWindow = new StatusWindow(StatusType.Error, $"Failed to read material: {result.Error}");
                    errorWindow.ShowDialog();
                    return;
                }

                var material = result.Value;
                if (material != null)
                {
                    var materialViewer = new MaterialViewerWindow(material, 0);
                    materialViewer.Show();
                }
            }
            catch (Exception ex)
            {
                var errorWindow = new StatusWindow(StatusType.Error, $"Error reading material: {ex.Message}");
                errorWindow.ShowDialog();
            }
        }
        else if (audioBankReader != null)
        {
            var pakFile = FindPakFileForEntry(pakEntry.Value);
            if (pakFile == null)
            {
                var errorWindow = new StatusWindow(StatusType.Error, "PAK file not found for this material.");
                errorWindow.ShowDialog();
                return;
            }
            byte[] bnkData = gameProvider.PakReader.ExtractFile(pakFile, pakEntry.Value);
            audioBankReader.Read(bnkData, selectedFile.Name);
        }
        else
        {
            // No viewer available
            var infoWindow = new StatusWindow(StatusType.Warning, 
                $"No viewer available for this file type.\nUse 'View Hex' to see raw data.");
            infoWindow.ShowDialog();
        }
    }
    
    private void MenuItem_ViewHex_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null || selectedFile.IsFolder)
        {
            return;
        }
        
        var metadata = selectedFile.TreeItemReference?.Metadata;
        if (metadata?.PakEntry == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "Cannot view hex: file metadata not available.");
            errorWindow.ShowDialog();
            return;
        }
        
        var hexViewer = new HexViewerWindow(selectedFile.Name, metadata.PakEntry.Value);
        hexViewer.Show();
    }
    
    private void MenuItem_ExportRaw_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null || selectedFile.IsFolder)
        {
            return;
        }
        
        var metadata = selectedFile.TreeItemReference?.Metadata;
        if (metadata?.PakEntry == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "Cannot export: file metadata not available.");
            errorWindow.ShowDialog();
            return;
        }
        
        var pakEntry = metadata.PakEntry.Value;
        
        // Get current game provider
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "No game loaded.");
            errorWindow.ShowDialog();
            return;
        }
        
        var pakFile = FindPakFileForEntry(pakEntry);
        if (pakFile == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "PAK file not found for this entry.");
            errorWindow.ShowDialog();
            return;
        }
        
        try
        {
            var saveDialog = new Microsoft.Win32.SaveFileDialog
            {
                FileName = selectedFile.Name,
                Filter = "All Files (*.*)|*.*",
                DefaultExt = Path.GetExtension(selectedFile.Name)
            };
            
            if (saveDialog.ShowDialog() == true)
            {
                var statusWindow = new StatusWindow(StatusType.Loading, "Exporting raw file data...");
                statusWindow.Show();
                
                // Use the game provider's PAK reader
                byte[] rawData = gameProvider.PakReader.ExtractFile(pakFile, pakEntry);
                File.WriteAllBytes(saveDialog.FileName, rawData);
                
                statusWindow.Close();
                
                var successWindow = new StatusWindow(StatusType.Success, 
                    $"File exported successfully!\nSize: {FormatFileSize(rawData.Length)}");
                successWindow.ShowDialog();
            }
        }
        catch (Exception ex)
        {
            var errorWindow = new StatusWindow(StatusType.Error, $"Failed to export file: {ex.Message}");
            errorWindow.ShowDialog();
        }
    }
    
    private void MenuItem_ExtractUncompressed_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null || selectedFile.IsFolder)
        {
            return;
        }
        
        var metadata = selectedFile.TreeItemReference?.Metadata;
        if (metadata?.PakEntry == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "Cannot extract: file metadata not available.");
            errorWindow.ShowDialog();
            return;
        }
        
        var pakEntry = metadata.PakEntry.Value;
        
        // Get current game provider
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "No game loaded.");
            errorWindow.ShowDialog();
            return;
        }
        
        var pakFile = FindPakFileForEntry(pakEntry);
        if (pakFile == null)
        {
            var errorWindow = new StatusWindow(StatusType.Error, "PAK file not found for this entry.");
            errorWindow.ShowDialog();
            return;
        }
        
        try
        {
            var saveDialog = new Microsoft.Win32.SaveFileDialog
            {
                FileName = selectedFile.Name,
                Filter = "All Files (*.*)|*.*",
                DefaultExt = Path.GetExtension(selectedFile.Name)
            };
            
            if (saveDialog.ShowDialog() == true)
            {
                var statusWindow = new StatusWindow(StatusType.Loading, "Extracting and decompressing file...");
                statusWindow.Show();
                
                // Use the game provider's PAK reader
                byte[] extractedData = gameProvider.PakReader.ExtractFile(pakFile, pakEntry);
                
                // The ExtractFile method already decompresses the data if needed
                File.WriteAllBytes(saveDialog.FileName, extractedData);
                
                statusWindow.Close();
                
                string compressionInfo = pakEntry.IsCompressed 
                    ? $"\nDecompressed from: {FormatFileSize(pakEntry.CompressedSize)}" 
                    : "";
                
                var successWindow = new StatusWindow(StatusType.Success, 
                    $"File extracted successfully!\nSize: {FormatFileSize(extractedData.Length)}{compressionInfo}");
                successWindow.ShowDialog();
            }
        }
        catch (Exception ex)
        {
            var errorWindow = new StatusWindow(StatusType.Error, $"Failed to extract file: {ex.Message}");
            errorWindow.ShowDialog();
        }
    }
    
    private void MenuItem_CopyName_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null)
        {
            return;
        }
        
        try
        {
            Clipboard.SetText(selectedFile.Name);
        }
        catch (Exception ex)
        {
            var errorWindow = new StatusWindow(StatusType.Error, $"Failed to copy name: {ex.Message}");
            errorWindow.ShowDialog();
        }
    }
    
    private void MenuItem_OpenFileLocation_Click(object sender, RoutedEventArgs e)
    {
        var selectedFile = GetSelectedDataGridFile();
        if (selectedFile == null || selectedFile.TreeItemReference == null)
        {
            return;
        }
        
        var parent = FindParentTreeItem(TreeManager.Root, selectedFile.TreeItemReference);
        if (parent != null)
        {
            // Exit search mode and navigate to the parent folder
            ExitSearchMode();
            NavigateToTreeItem(parent);
            
            Dispatcher.BeginInvoke(new Action(() =>
            {
                var dataGrid = FindVisualChild<System.Windows.Controls.DataGrid>(this);
                if (dataGrid != null)
                {
                    foreach (var item in dataGrid.Items)
                    {
                        if (item is DataGridFile file && file.Name == selectedFile.Name)
                        {
                            dataGrid.SelectedItem = file;
                            dataGrid.ScrollIntoView(file);
                            
                            // Focus the DataGrid row
                            var row = (DataGridRow)dataGrid.ItemContainerGenerator.ContainerFromItem(file);
                            if (row != null)
                            {
                                row.Focus();
                            }
                            break;
                        }
                    }
                }
            }), System.Windows.Threading.DispatcherPriority.Loaded);
        }
    }
    
    private void ContextMenu_Opened(object sender, RoutedEventArgs e)
    {
        if (sender is ContextMenu contextMenu)
        {
            foreach (var item in contextMenu.Items)
            {
                if (item is Wpf.Ui.Controls.MenuItem menuItem && menuItem.Header?.ToString() == "Open File Location")
                {
                    menuItem.Visibility = _isSearchMode ? Visibility.Visible : Visibility.Collapsed;
                    break;
                }
            }
        }
    }
    
    private PakFile? FindPakFileForEntry(PakEntry entry)
    {
        var loadedPakFiles = GameManager.LoadedPakFiles;
        
        foreach (var pakFile in loadedPakFiles.Values)
        {
            if (pakFile.Entries.Any(e => e.FilePath == entry.FilePath))
            {
                return pakFile;
            }
        }
        
        return null;
    }
    
    private DataGridFile? GetSelectedDataGridFile()
    {
        var dataGrid = FindVisualChild<System.Windows.Controls.DataGrid>(this);
        return dataGrid?.SelectedItem as DataGridFile;
    }
    
    private T? FindVisualChild<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
        {
            var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
            
            if (child is T typedChild)
            {
                return typedChild;
            }
            
            var result = FindVisualChild<T>(child);
            if (result != null)
            {
                return result;
            }
        }
        
        return null;
    }
    
    // Search functionality
    private void SearchTextBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && sender is System.Windows.Controls.TextBox textBox)
        {
            var searchText = textBox.Text?.Trim();
            
            if (string.IsNullOrEmpty(searchText))
            {
                // Clear search and restore normal view
                ExitSearchMode();
                return;
            }
            
            PerformSearch(searchText);
        }
    }
    
    private void BuildAllFilesCache()
    {
        _allFilesCache.Clear();
        BuildFileCacheRecursive(TreeManager.Root);
    }
    
    private void BuildFileCacheRecursive(TreeItem node)
    {
        foreach (var child in node.Children)
        {
            if (!child.IsFolder)
            {
                var fileItem = new DataGridFile
                {
                    Name = child.Name,
                    Icon = child.Icon,
                    Type = GetFileType(child.Name),
                    TreeItemReference = child,
                    IsFolder = false
                };
                
                // Fill metadata for files
                if (child.Metadata != null)
                {
                    var metadata = child.Metadata;
                    fileItem.Size = FormatFileSize(metadata.UncompressedSize);
                    fileItem.SizeBytes = metadata.UncompressedSize;
                    fileItem.CompressedSize = metadata.IsCompressed ? FormatFileSize(metadata.CompressedSize) : "-";
                    fileItem.CompressedSizeBytes = metadata.IsCompressed ? metadata.CompressedSize : 0;
                    fileItem.Checksum = $"0x{metadata.Checksum:X16}";
                    fileItem.Compression = metadata.Compression.ToString();
                }
                
                _allFilesCache.Add(fileItem);
            }
            
            if (child.Children.Count > 0)
            {
                BuildFileCacheRecursive(child);
            }
        }
    }
    
    private void PerformSearch(string searchText)
    {
        _isSearchMode = true;
        DataGridFiles.Clear();
        
        // Build a list of files relative to the current folder
        var currentFolderFiles = new List<DataGridFile>();
        BuildRelativeFilesCache(_currentTreeItem ?? TreeManager.Root, currentFolderFiles);
        
        var searchResults = currentFolderFiles
            .Where(file => file.Name.Contains(searchText, StringComparison.OrdinalIgnoreCase))
            .OrderBy(file => file.Name)
            .ToList();
        
        if (searchResults.Count == 0)
        {
            StatusWindow info = new StatusWindow(StatusType.Warning);
            info.UpdateMessage($"No files found matching '{searchText}' in current folder");
            info.Show();
        }
        else
        {
            foreach (var file in searchResults)
            {
                DataGridFiles.Add(file);
            }
            
            string folderContext = _currentTreeItem == TreeManager.Root 
                ? "all folders" 
                : $"'{CurrentPath}'";
            
            CurrentPath = $"Search results for: {searchText} in {folderContext} ({searchResults.Count} files found)";
        }
    }
    
    private void BuildRelativeFilesCache(TreeItem node, List<DataGridFile> cache)
    {
        foreach (var child in node.Children)
        {
            if (!child.IsFolder)
            {
                var fileItem = new DataGridFile
                {
                    Name = child.Name,
                    Icon = child.Icon,
                    Type = GetFileType(child.Name),
                    TreeItemReference = child,
                    IsFolder = false
                };
                
                if (child.Metadata != null)
                {
                    var metadata = child.Metadata;
                    fileItem.Size = FormatFileSize(metadata.UncompressedSize);
                    fileItem.SizeBytes = metadata.UncompressedSize;
                    fileItem.CompressedSize = metadata.IsCompressed ? FormatFileSize(metadata.CompressedSize) : "-";
                    fileItem.CompressedSizeBytes = metadata.IsCompressed ? metadata.CompressedSize : 0;
                    fileItem.Checksum = $"0x{metadata.Checksum:X16}";
                    fileItem.Compression = metadata.Compression.ToString();
                }
                
                cache.Add(fileItem);
            }
            
            if (child.Children.Count > 0)
            {
                BuildRelativeFilesCache(child, cache);
            }
        }
    }
    
    private void ExitSearchMode()
    {
        if (!_isSearchMode) return;
        
        _isSearchMode = false;
        
        // Clear search text box
        var searchBox = FindVisualChild<System.Windows.Controls.TextBox>(this);
        if (searchBox?.Name == "SearchTextBox")
        {
            searchBox.Text = string.Empty;
        }
        
        // Restore the current folder view
        if (_currentTreeItem != null)
        {
            LoadTreeItemIntoDataGrid(_currentTreeItem);
        }
    }
    
    public event PropertyChangedEventHandler? PropertyChanged;
    
    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}