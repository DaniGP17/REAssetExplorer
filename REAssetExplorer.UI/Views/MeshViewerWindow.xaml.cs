using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Forms.Integration;
using System.Windows.Threading;
using REAssetExplorer.App.Views;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Rendering;
using REAssetExplorer.Rendering.Handlers;
using REAssetExplorer.UI.Enums;
using REAssetExplorer.UI.Models;
using Wpf.Ui.Controls;

namespace REAssetExplorer.UI.Views;

/// <summary>
/// Mesh viewer window for displaying 3D models with DirectX rendering.
/// </summary>
public partial class MeshViewerWindow : FluentWindow
{
    private readonly string _fileName;
    private readonly PakEntry _pakEntry;
    private MeshData? _meshData;
    private Mesh? _mesh;
    private Renderer? _renderer;
    private D3D11Device? _device;
    private RenderPanel? _renderPanel;
    private DispatcherTimer? _fpsTimer;
    private DateTime _lastFrameTime;
    private int _frameCount;
    private bool _isRendering;
    private Vector3 _meshCenter = Vector3.Zero;
    private float _meshBoundingRadius = 5f;
    private List<MeshHierarchyNode> _hierarchyNodes = new();
    
    public MeshViewerWindow(string fileName, PakEntry? pakEntry = null)
    {
        InitializeComponent();
        
        _fileName = fileName;
        _pakEntry = pakEntry ?? default;
        
        Loaded += OnWindowLoaded;
        Closed += OnWindowClosed;
    }
    
    private async void OnWindowLoaded(object sender, RoutedEventArgs e)
    {
        await InitializeRenderingAsync();
        await LoadMeshAsync();
    }
    
    private async Task InitializeRenderingAsync()
    {
        try
        {
            // Create a Panel to host DirectX rendering
            _renderPanel = new RenderPanel();
            RenderHost.Child = _renderPanel;
            RenderHost.Visibility = Visibility.Hidden;
            
            // Connect camera control events
            _renderPanel.OnCameraRotate += OnCameraRotate;
            _renderPanel.OnCameraPan += OnCameraPan;
            _renderPanel.OnCameraZoom += OnCameraZoom;
            _renderPanel.OnOrbitModeStart += OnOrbitModeStart;
            _renderPanel.OnOrbitModeEnd += OnOrbitModeEnd;
            _renderPanel.OnKeyPress += OnKeyPress;
            
            // Wait for handle to be created
            await Task.Delay(100);
            
            if (_renderPanel.Handle == IntPtr.Zero)
            {
                ShowError("Failed to create render panel handle.");
                return;
            }
            
            // Initialize DirectX device
            _device = D3D11Device.Create(
                _renderPanel.Handle,
                (int)RenderHost.ActualWidth,
                (int)RenderHost.ActualHeight
            );
            
            // Initialize renderer
            _renderer = new Renderer(_device);
            
            // Camera will be positioned after mesh loads
            // For now, use default position
            _renderer.Camera.Position = new Vector3(0, 1f, 5f);
            _renderer.Camera.Target = new Vector3(0, 0, 0);
            _renderer.Camera.AspectRatio = (float)(RenderHost.ActualWidth / RenderHost.ActualHeight);
            
            // Setup continuous rendering loop for maximum FPS
            // This uses CompositionTarget.Rendering but triggers continuous updates
            _isRendering = true;
            System.Windows.Media.CompositionTarget.Rendering += OnCompositionTargetRendering;
            
            // FPS counter
            _fpsTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(1)
            };
            _fpsTimer.Tick += OnFPSTick;
            _fpsTimer.Start();
            
            _lastFrameTime = DateTime.UtcNow;
            
            // Handle resize
            RenderHost.SizeChanged += OnRenderHostSizeChanged;
            
            StatusText.Text = "Rendering initialized";
            RenderHost.Visibility = Visibility.Visible;
        }
        catch (Exception ex)
        {
            ShowError($"Failed to initialize rendering: {ex.Message}");
        }
    }
    
    private async Task LoadMeshAsync()
    {
        if (string.IsNullOrEmpty(_pakEntry.FilePath))
        {
            ShowError("No mesh data available.");
            return;
        }
        
        StatusWindow? statusWindow = null;
        
        try
        {
            // Show status window
            statusWindow = new StatusWindow(StatusType.Loading, "Initializing mesh loader...");
            statusWindow.Owner = this;
            statusWindow.Show();
            
            await Task.Delay(100); // Allow window to render
            
            var gameProvider = GameManager.CurrentGameProvider;
            if (gameProvider == null)
            {
                ShowError("No game loaded.");
                statusWindow?.Close();
                return;
            }
            
            statusWindow.UpdateMessage("Finding PAK file...");
            await Task.Delay(50);
            
            var pakFile = FindPakFile();
            if (pakFile == null)
            {
                ShowError("PAK file not found.");
                statusWindow?.Close();
                return;
            }
            
            statusWindow.UpdateMessage("Creating asset loader...");
            await Task.Delay(50);
            
            Console.WriteLine($"LoadMeshAsync: Creating AssetLoader with {GameManager.LoadedPakFiles.Count} PAK files");
            var assetLoader = new AssetLoader(
                gameProvider,
                GameManager.LoadedPakFiles.ToDictionary(kvp => kvp.Key, kvp => kvp.Value),
                GameManager.MaterialsCache
            );
            Console.WriteLine($"LoadMeshAsync: AssetLoader created successfully");
            
            statusWindow.UpdateMessage($"Loading mesh: {System.IO.Path.GetFileName(_pakEntry.FilePath)}");
            await Task.Delay(50);
            
            // Load mesh with dependencies (materials, textures)
            var result = await Task.Run(() => assetLoader.LoadAsset<MeshData>(
                _pakEntry.FilePath,
                loadDependencies: true
            ));
            
            if (!result.IsSuccess || result.Value == null)
            {
                ShowError($"Failed to load mesh: {result.Error}");
                statusWindow?.Close();
                return;
            }
            
            _meshData = result.Value;
            
            statusWindow.UpdateMessage("Creating GPU buffers...");
            await Task.Delay(50);
            
            // Create rendering mesh
            if (_device != null)
            {
                Console.WriteLine("LoadMeshAsync: Creating Mesh object...");
                Console.WriteLine($"LoadMeshAsync: AssetLoader is {(assetLoader == null ? "NULL" : "valid")}");
                Console.WriteLine($"LoadMeshAsync: Device is {(_device == null ? "NULL" : "valid")}");
                Console.WriteLine($"LoadMeshAsync: MeshData is {(_meshData == null ? "NULL" : "valid")}");
                _mesh = await Task.Run(() => new Mesh(_device, _meshData, assetLoader));
                Console.WriteLine($"LoadMeshAsync: Mesh created - SubMeshes: {_mesh.SubMeshes.Count}, Materials: {_mesh.Materials.Count}");
            }
            
            statusWindow.UpdateMessage("Updating UI...");
            await Task.Delay(50);
            
            Console.WriteLine("LoadMeshAsync: About to call CalculateMeshCenter");
            // Calculate mesh center and position camera
            CalculateMeshCenter();
            
            Console.WriteLine("LoadMeshAsync: About to call PositionCameraOnMesh");
            PositionCameraOnMesh();
            
            Console.WriteLine("LoadMeshAsync: About to call UpdateMeshInformation");
            // Update UI with mesh information
            try
            {
                UpdateMeshInformation();
            }
            catch (Exception uiEx)
            {
                Console.WriteLine($"LoadMeshAsync: ERROR in UpdateMeshInformation - {uiEx.Message}");
                Console.WriteLine($"Stack trace: {uiEx.StackTrace}");
            }
            
            Console.WriteLine("LoadMeshAsync: Completed successfully");
            StatusText.Text = "Mesh loaded successfully";
            
            statusWindow?.Close();
        }
        catch (Exception ex)
        {
            ShowError($"Error loading mesh: {ex.Message}");
            statusWindow?.Close();
        }
        finally
        {
            LoadingIndicator.Visibility = Visibility.Collapsed;
        }
    }
    
    private void UpdateMeshInformation()
    {
        if (_meshData == null)
        {
            Console.WriteLine("UpdateMeshInformation: _meshData is null!");
            return;
        }
        
        Console.WriteLine($"UpdateMeshInformation: Starting update for {_fileName}");
        
        // General Information
        FileNameText.Text = _fileName;
        LODCountText.Text = _meshData.MeshLayout.LODCount.ToString();
        
        // Calculate total vertices and indices
        int totalVertices = 0;
        int totalIndices = 0;
        
        if (_meshData.MeshLayout.MeshBodies != null && _meshData.MeshLayout.MeshBodies.Count > 0)
        {
            foreach (var meshBody in _meshData.MeshLayout.MeshBodies)
            {
                if (meshBody.Parts != null)
                {
                    foreach (var part in meshBody.Parts)
                    {
                        totalVertices += (int)part.VertexCount;
                        totalIndices += (int)part.IndexCount * 3;
                    }
                }
            }
        }
        
        TotalVerticesText.Text = totalVertices.ToString("N0");
        TotalIndicesText.Text = totalIndices.ToString("N0");
        MaterialCountText.Text = _meshData.Materials?.Count.ToString() ?? "0";
        HasSkeletonText.Text = (_meshData.SkeletonLayout.JointCount > 0) ? "Yes" : "No";
        
        // Bounding Box
        BoundingMinText.Text = $"({_meshData.MeshLayout.AabbMinX:F2}, {_meshData.MeshLayout.AabbMinY:F2}, {_meshData.MeshLayout.AabbMinZ:F2})";
        BoundingMaxText.Text = $"({_meshData.MeshLayout.AabbMaxX:F2}, {_meshData.MeshLayout.AabbMaxY:F2}, {_meshData.MeshLayout.AabbMaxZ:F2})";
        BoundingRadiusText.Text = $"{_meshData.MeshLayout.boundingRadius:F2}";
        
        // Build mesh hierarchy
        BuildMeshHierarchy();
        
        // Update materials list
        if (_meshData.Materials != null && _meshData.Materials.Count > 0)
        {
            MaterialsList.ItemsSource = _meshData.Materials.Values.ToList();
        }
    }
    
    /// <summary>
    /// Calculates the center of the mesh from its bounding box
    /// </summary>
    private void CalculateMeshCenter()
    {
        Console.WriteLine("CalculateMeshCenter: Starting");
        
        if (_meshData == null)
        {
            _meshCenter = Vector3.Zero;
            _meshBoundingRadius = 5f;
            return;
        }
        
        // Calculate center from AABB (Axis-Aligned Bounding Box)
        float centerX = (_meshData.MeshLayout.AabbMinX + _meshData.MeshLayout.AabbMaxX) / 2f;
        float centerY = (_meshData.MeshLayout.AabbMinY + _meshData.MeshLayout.AabbMaxY) / 2f;
        float centerZ = (_meshData.MeshLayout.AabbMinZ + _meshData.MeshLayout.AabbMaxZ) / 2f;
        
        _meshCenter = new Vector3(centerX, centerY, centerZ);
        
        // Calculate bounding radius for camera distance
        // Use the mesh's bounding radius if available, otherwise calculate from AABB
        if (_meshData.MeshLayout.boundingRadius > 0)
        {
            _meshBoundingRadius = _meshData.MeshLayout.boundingRadius;
        }
        else
        {
            // Calculate diagonal of AABB as fallback
            float sizeX = _meshData.MeshLayout.AabbMaxX - _meshData.MeshLayout.AabbMinX;
            float sizeY = _meshData.MeshLayout.AabbMaxY - _meshData.MeshLayout.AabbMinY;
            float sizeZ = _meshData.MeshLayout.AabbMaxZ - _meshData.MeshLayout.AabbMinZ;
            _meshBoundingRadius = MathF.Sqrt(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ) / 2f;
        }
        
        // Ensure minimum radius
        if (_meshBoundingRadius < 1f)
            _meshBoundingRadius = 5f;
        
        Console.WriteLine($"CalculateMeshCenter: Completed - Center: {_meshCenter}, Radius: {_meshBoundingRadius}");
    }
    
    /// <summary>
    /// Positions the camera to look at the mesh center
    /// </summary>
    private void PositionCameraOnMesh()
    {
        Console.WriteLine("PositionCameraOnMesh: Starting");
        
        if (_renderer == null)
        {
            Console.WriteLine("PositionCameraOnMesh: _renderer is null, returning");
            return;
        }
        
        // Position camera at a nice viewing angle
        // Use bounding radius to determine appropriate distance
        float distance = _meshBoundingRadius * 2.5f;
        
        // Position camera diagonally (slightly above and to the side)
        Vector3 offset = new Vector3(distance * 0.5f, distance * 0.4f, distance);
        
        _renderer.Camera.Position = _meshCenter + offset;
        _renderer.Camera.Target = _meshCenter;
        _renderer.Camera.OrbitPivot = _meshCenter;
        
        // Recalculate camera angles based on new position
        var viewDirection = Vector3.Normalize(_meshCenter - _renderer.Camera.Position);
        _renderer.Camera.Pitch = MathF.Asin(viewDirection.Y);
        _renderer.Camera.Yaw = MathF.Atan2(viewDirection.X, viewDirection.Z);
    }
    
    private void BuildMeshHierarchy()
    {
        if (_meshData == null)
        {
            Console.WriteLine("BuildMeshHierarchy: _meshData is null!");
            return;
        }
        
        Console.WriteLine($"BuildMeshHierarchy: Starting for {_meshData.MeshLayout.MeshBodies?.Count ?? 0} LODs");
        
        _hierarchyNodes.Clear();
        MeshHierarchyTree.ItemsSource = null;
        
        if (_meshData.MeshLayout.MeshBodies == null || _meshData.MeshLayout.MeshBodies.Count == 0)
        {
            Console.WriteLine("BuildMeshHierarchy: No MeshBodies found!");
            return;
        }
        
        for (int lodIndex = 0; lodIndex < _meshData.MeshLayout.MeshBodies.Count; lodIndex++)
        {
            var meshBody = _meshData.MeshLayout.MeshBodies[lodIndex];
            var lodNode = new MeshHierarchyNode
            {
                DisplayName = $"LOD {lodIndex} ({meshBody.PartCount} parts)",
                NodeType = MeshNodeType.Lod,
                LodIndex = lodIndex,
                Index = lodIndex,
                IsChecked = lodIndex == 0 // Only LOD 0 checked by default
            };
            
            // Subscribe to visibility changes
            lodNode.PropertyChanged += OnHierarchyNodeChanged;
            
            if (meshBody.Parts != null)
            {
                int totalPartIndex = 0;
                foreach (var part in meshBody.Parts)
                {
                    var partNode = new MeshHierarchyNode
                    {
                        DisplayName = $"Part {part.PartId} - {part.VertexCount:N0} vertices, {part.IndexCount:N0} triangles",
                        NodeType = MeshNodeType.Part,
                        PartId = part.PartId,
                        Index = totalPartIndex++,
                        Parent = lodNode,
                        IsChecked = lodIndex == 0 // Only children of LOD 0 checked by default
                    };
                    
                    partNode.PropertyChanged += OnHierarchyNodeChanged;
                    
                    if (part.Clusters != null)
                    {
                        foreach (var cluster in part.Clusters)
                        {
                            string materialName = "Unknown";
                            if (_meshData.Materials != null && _meshData.Materials.ContainsKey(cluster.MaterialId))
                            {
                                materialName = _meshData.Materials[cluster.MaterialId];
                            }
                            
                            var clusterNode = new MeshHierarchyNode
                            {
                                DisplayName = $"Cluster - Material: {materialName} ({cluster.IndexCount:N0} triangles)",
                                NodeType = MeshNodeType.Cluster,
                                MaterialId = cluster.MaterialId,
                                MaterialName = materialName,
                                Parent = partNode,
                                IsChecked = lodIndex == 0 // Only children of LOD 0 checked by default
                            };
                            
                            clusterNode.PropertyChanged += OnHierarchyNodeChanged;
                            
                            partNode.Children.Add(clusterNode);
                        }
                    }
                    
                    lodNode.Children.Add(partNode);
                }
            }
            
            _hierarchyNodes.Add(lodNode);
        }
        
        // Set the active LOD to 0 by default
        if (_hierarchyNodes.Count > 0 && _mesh != null)
        {
            _mesh.RenderState.ActiveLodIndex = 0;
            Console.WriteLine($"BuildMeshHierarchy: Set active LOD to 0, total hierarchy nodes: {_hierarchyNodes.Count}");
            
            // Log render state for debugging
            Console.WriteLine($"RenderState - VisiblePartIds count: {_mesh.RenderState.VisiblePartIds.Count}");
            Console.WriteLine($"RenderState - VisibleMaterialIds count: {_mesh.RenderState.VisibleMaterialIds.Count}");
            Console.WriteLine($"Mesh - Total SubMeshes: {_mesh.SubMeshes.Count}");
            
            if (_mesh.SubMeshes.Count > 0)
            {
                var lod0Submeshes = _mesh.SubMeshes.Where(s => s.LodIndex == 0).ToList();
                Console.WriteLine($"Mesh - LOD 0 SubMeshes: {lod0Submeshes.Count}");
                if (lod0Submeshes.Count > 0)
                {
                    foreach (var submesh in lod0Submeshes.Take(3))
                    {
                        Console.WriteLine($"  SubMesh - LOD:{submesh.LodIndex}, PartId:{submesh.PartId}, MaterialID:{submesh.MaterialID}, " +
                                        $"PartVisible:{_mesh.RenderState.IsPartVisible(submesh.PartId)}, " +
                                        $"MaterialVisible:{_mesh.RenderState.IsMaterialVisible(submesh.MaterialID)}");
                    }
                }
            }
        }
        else
        {
            Console.WriteLine($"BuildMeshHierarchy: WARNING - _hierarchyNodes.Count={_hierarchyNodes.Count}, _mesh={(_mesh != null ? "not null" : "NULL")}");
        }
        
        MeshHierarchyTree.ItemsSource = _hierarchyNodes;
        Console.WriteLine("BuildMeshHierarchy: Completed");
    }
    
    private void OnHierarchyNodeChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MeshHierarchyNode.IsVisible) && sender is MeshHierarchyNode node && _mesh != null)
        {
            UpdateRenderState(node);
        }
    }
    
    private void UpdateRenderState(MeshHierarchyNode node)
    {
        if (_mesh == null) return;
        
        var meshRenderState = _mesh.RenderState;
        
        Console.WriteLine($"UpdateRenderState: NodeType={node.NodeType}, IsVisible={node.IsVisible}, IsChecked={node.IsChecked}");
        
        switch (node.NodeType)
        {
            case MeshNodeType.Lod:
                // When a LOD is checked, make it active and uncheck others
                // When a LOD is unchecked, deactivate it
                if (node.IsChecked == true)
                {
                    meshRenderState.ActiveLodIndex = node.LodIndex;
                    Console.WriteLine($"  Setting ActiveLodIndex to {node.LodIndex}");
                    
                    // Uncheck other LODs (radio button behavior)
                    foreach (var otherLod in _hierarchyNodes.Where(n => n != node))
                    {
                        if (otherLod.IsChecked == true)
                        {
                            otherLod.PropertyChanged -= OnHierarchyNodeChanged;
                            otherLod.IsChecked = false;
                            otherLod.PropertyChanged += OnHierarchyNodeChanged;
                        }
                    }
                    
                    // Update the render state with all parts and materials from this LOD
                    SyncLodPartsAndMaterialsToRenderState(node, meshRenderState);
                }
                else if (node.IsChecked == false)
                {
                    // If unchecking the active LOD, deactivate rendering
                    if (meshRenderState.ActiveLodIndex == node.LodIndex)
                    {
                        meshRenderState.ActiveLodIndex = -1;
                        Console.WriteLine("  Deactivating LOD (set to -1)");
                        meshRenderState.ShowAll(); // Clear part/material filters
                    }
                }
                break;
                
            case MeshNodeType.Part:
                // Update part visibility within the active LOD
                meshRenderState.SetPartVisibility(node.PartId, node.IsVisible);
                Console.WriteLine($"  Part {node.PartId} visibility = {node.IsVisible}");
                break;
                
            case MeshNodeType.Cluster:
                // Update material visibility within the active LOD
                meshRenderState.SetMaterialVisibility(node.MaterialId, node.IsVisible);
                Console.WriteLine($"  Material {node.MaterialId} visibility = {node.IsVisible}");
                break;
        }
    }
    
    private void SyncLodPartsAndMaterialsToRenderState(MeshHierarchyNode lodNode, MeshRenderState renderState)
    {
        // When a LOD is activated, populate the render state with all its parts and materials
        renderState.ShowAll(); // Start fresh
        
        // If all children are checked, leave the HashSets empty (all visible)
        // If some are unchecked, populate with only the visible ones
        bool hasUncheckedChildren = false;
        
        foreach (var partNode in lodNode.Children)
        {
            if (partNode.IsChecked != true)
            {
                hasUncheckedChildren = true;
                break;
            }
        }
        
        if (hasUncheckedChildren)
        {
            // Need to explicitly track which parts/materials are visible
            foreach (var partNode in lodNode.Children)
            {
                if (partNode.IsChecked == true)
                {
                    renderState.SetPartVisibility(partNode.PartId, true);
                    
                    // Also add all materials from this part
                    foreach (var clusterNode in partNode.Children)
                    {
                        if (clusterNode.IsChecked == true)
                        {
                            renderState.SetMaterialVisibility(clusterNode.MaterialId, true);
                        }
                    }
                }
            }
        }
    }
    
    private void OnUpdateTick(object? sender, EventArgs e)
    {
        if (!_isRendering || _renderer == null || _mesh == null || _renderPanel == null)
            return;
        
        var currentTime = DateTime.UtcNow;
        var deltaTime = (float)(currentTime - _lastFrameTime).TotalSeconds;
        _lastFrameTime = currentTime;
        
        // Handle keyboard input
        HandleKeyboardInput(deltaTime);
        
        // Update camera
        _renderer.Camera.Update(deltaTime);
        
        // Render
        _renderer.BeginFrame(0.2f, 0.3f, 0.4f, 1.0f);
        
        var world = Matrix4x4.Identity;
        _renderer.DrawMesh(_mesh, world, _mesh.Materials);
        
        _renderer.EndFrame();
        
        _frameCount++;
    }
    
    private void OnCompositionTargetRendering(object? sender, EventArgs e)
    {
        OnUpdateTick(sender, e);
    }
    
    private void HandleKeyboardInput(float deltaTime)
    {
        if (_renderer == null || _renderPanel == null || !_renderPanel.Focused)
            return;
        
        var movement = Vector3.Zero;
        
        // WASD movement
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.W))
            movement.Z += 1;
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.S))
            movement.Z -= 1;
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.A))
            movement.X -= 1;
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.D))
            movement.X += 1;
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.E))
            movement.Y += 1;
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.Q))
            movement.Y -= 1;
        
        // Apply speed boost with Shift
        if (_renderPanel.IsKeyPressed(System.Windows.Forms.Keys.ShiftKey))
        {
            deltaTime *= 2f;
        }
        
        if (movement != Vector3.Zero)
        {
            _renderer.Camera.Move(movement, deltaTime);
        }
    }
    
    private void OnCameraRotate(float deltaX, float deltaY, bool isOrbital)
    {
        _renderer?.Camera.Rotate(deltaX, deltaY, isOrbital);
    }
    
    private void OnCameraPan(float deltaX, float deltaY)
    {
        _renderer?.Camera.Pan(deltaX, deltaY);
    }
    
    private void OnCameraZoom(float delta, bool isSpeedAdjustment)
    {
        if (_renderer == null) return;
        
        if (isSpeedAdjustment)
        {
            // Ajustar velocidad de movimiento
            _renderer.Camera.AdjustMoveSpeed(delta);
        }
        else
        {
            // Hacer zoom (mover cámara adelante/atrás)
            _renderer.Camera.Zoom(delta);
        }
    }
    
    private void OnOrbitModeStart()
    {
        _renderer?.Camera.UpdateOrbitPivot();
    }
    
    private void OnOrbitModeEnd()
    {
        _renderer?.Camera.UpdateFreeLookAngles();
    }
    
    private void OnKeyPress(System.Windows.Forms.Keys key)
    {
        if (key == System.Windows.Forms.Keys.F && _renderer != null)
        {
            _renderer.Camera.FocusOn(_meshCenter, _meshBoundingRadius * 2f);
        }
    }
    
    private void OnFPSTick(object? sender, EventArgs e)
    {
        FPSText.Text = _frameCount.ToString();
        _frameCount = 0;
    }
    
    private void OnRenderHostSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (_renderer == null || e.NewSize.Width <= 0 || e.NewSize.Height <= 0)
            return;
        
        try
        {
            _renderer.Resize((int)e.NewSize.Width, (int)e.NewSize.Height);
            _renderer.Camera.AspectRatio = (float)(e.NewSize.Width / e.NewSize.Height);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Resize error: {ex.Message}");
        }
    }
    
    private PakFile? FindPakFile()
    {
        var gameProvider = GameManager.CurrentGameProvider;
        if (gameProvider == null)
            return null;
        
        // Find the PAK file that contains this entry
        foreach (var pakFileKvp in GameManager.LoadedPakFiles)
        {
            if (pakFileKvp.Value.Entries.Any(e => e.FilePath == _pakEntry.FilePath))
            {
                return pakFileKvp.Value;
            }
        }
        
        return null;
    }
    
    private void ShowError(string message)
    {
        ErrorText.Text = message;
        ErrorOverlay.Visibility = Visibility.Visible;
        StatusText.Text = "Error";
    }
    
    private void OnWindowClosed(object? sender, EventArgs e)
    {
        // Stop rendering
        _isRendering = false;
        System.Windows.Media.CompositionTarget.Rendering -= OnCompositionTargetRendering;
        
        // Stop timers
        _fpsTimer?.Stop();
        
        // Cleanup resources
        _mesh?.Dispose();
        _renderer?.Dispose();
        _device?.Dispose();
    }
    
    /// <summary>
    /// Custom Windows Forms Panel for hosting DirectX rendering
    /// </summary>
    private class RenderPanel : System.Windows.Forms.Panel
    {
        public event Action<float, float, bool>? OnCameraRotate;
        public event Action<float, float>? OnCameraPan;
        public event Action<float, bool>? OnCameraZoom;
        public event Action? OnOrbitModeStart;
        public event Action? OnOrbitModeEnd;
        public new event Action<System.Windows.Forms.Keys>? OnKeyPress;
        
        private bool _isRightMouseDown;
        private bool _isMiddleMouseDown;
        private bool _isLeftMouseDown;
        private bool _wasInOrbitMode;
        private System.Drawing.Point _lastMousePosition;
        private HashSet<System.Windows.Forms.Keys> _pressedKeys = new HashSet<System.Windows.Forms.Keys>();
        
        public RenderPanel()
        {
            SetStyle(System.Windows.Forms.ControlStyles.Opaque, true);
            SetStyle(System.Windows.Forms.ControlStyles.UserPaint, true);
            SetStyle(System.Windows.Forms.ControlStyles.AllPaintingInWmPaint, true);
            SetStyle(System.Windows.Forms.ControlStyles.Selectable, true);
            DoubleBuffered = false;
            TabStop = true;
        }
        
        protected override void OnPaint(System.Windows.Forms.PaintEventArgs e)
        {
            // Don't call base.OnPaint - DirectX handles all rendering
        }
        
        protected override void OnPaintBackground(System.Windows.Forms.PaintEventArgs e)
        {
            // Don't paint background - DirectX handles it
        }
        
        protected override void OnMouseDown(System.Windows.Forms.MouseEventArgs e)
        {
            base.OnMouseDown(e);
            Focus(); // Ensure panel has focus for keyboard input
            
            if (e.Button == System.Windows.Forms.MouseButtons.Left)
            {
                _isLeftMouseDown = true;
                _lastMousePosition = e.Location;
            }
            else if (e.Button == System.Windows.Forms.MouseButtons.Right)
            {
                _isRightMouseDown = true;
                _lastMousePosition = e.Location;
            }
            else if (e.Button == System.Windows.Forms.MouseButtons.Middle)
            {
                _isMiddleMouseDown = true;
                _lastMousePosition = e.Location;
            }
        }
        
        protected override void OnMouseUp(System.Windows.Forms.MouseEventArgs e)
        {
            base.OnMouseUp(e);
            
            if (e.Button == System.Windows.Forms.MouseButtons.Left)
            {
                if (_wasInOrbitMode)
                {
                    OnOrbitModeEnd?.Invoke();
                    _wasInOrbitMode = false;
                }
                _isLeftMouseDown = false;
            }
            else if (e.Button == System.Windows.Forms.MouseButtons.Right)
            {
                _isRightMouseDown = false;
            }
            else if (e.Button == System.Windows.Forms.MouseButtons.Middle)
            {
                _isMiddleMouseDown = false;
            }
        }
        
        protected override void OnMouseMove(System.Windows.Forms.MouseEventArgs e)
        {
            base.OnMouseMove(e);
            
            bool isInOrbitMode = _isLeftMouseDown && (ModifierKeys & System.Windows.Forms.Keys.Alt) == System.Windows.Forms.Keys.Alt;
            
            if (isInOrbitMode && !_wasInOrbitMode)
            {
                OnOrbitModeStart?.Invoke();
                _wasInOrbitMode = true;
            }
            
            if (_isRightMouseDown || isInOrbitMode)
            {
                float deltaX = e.X - _lastMousePosition.X;
                float deltaY = e.Y - _lastMousePosition.Y;
                _lastMousePosition = e.Location;
                
                OnCameraRotate?.Invoke(deltaX, deltaY, isInOrbitMode);
            }
            else if (_isMiddleMouseDown)
            {
                float deltaX = e.X - _lastMousePosition.X;
                float deltaY = e.Y - _lastMousePosition.Y;
                _lastMousePosition = e.Location;
                
                OnCameraPan?.Invoke(deltaX, deltaY);
            }
        }
        
        protected override void OnMouseWheel(System.Windows.Forms.MouseEventArgs e)
        {
            base.OnMouseWheel(e);
            
            if (_isRightMouseDown)
            {
                // Con botón derecho: ajustar velocidad de movimiento
                float delta = e.Delta / 120.0f * 0.1f;
                OnCameraZoom?.Invoke(delta, true);
            }
            else
            {
                // Sin botón derecho: zoom
                float delta = e.Delta / 120.0f;
                OnCameraZoom?.Invoke(delta, false);
            }
        }
        
        protected override void OnKeyDown(System.Windows.Forms.KeyEventArgs e)
        {
            base.OnKeyDown(e);
            _pressedKeys.Add(e.KeyCode);
            OnKeyPress?.Invoke(e.KeyCode);
        }
        
        protected override void OnKeyUp(System.Windows.Forms.KeyEventArgs e)
        {
            base.OnKeyUp(e);
            _pressedKeys.Remove(e.KeyCode);
        }
        
        public bool IsKeyPressed(System.Windows.Forms.Keys key)
        {
            return _pressedKeys.Contains(key);
        }
        
        protected override bool IsInputKey(System.Windows.Forms.Keys keyData)
        {
            // Make sure WASD and arrow keys are processed
            switch (keyData)
            {
                case System.Windows.Forms.Keys.W:
                case System.Windows.Forms.Keys.A:
                case System.Windows.Forms.Keys.S:
                case System.Windows.Forms.Keys.D:
                case System.Windows.Forms.Keys.Q:
                case System.Windows.Forms.Keys.E:
                case System.Windows.Forms.Keys.F:
                case System.Windows.Forms.Keys.Up:
                case System.Windows.Forms.Keys.Down:
                case System.Windows.Forms.Keys.Left:
                case System.Windows.Forms.Keys.Right:
                    return true;
            }
            return base.IsInputKey(keyData);
        }
    }
}
