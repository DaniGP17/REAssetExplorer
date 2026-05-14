using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Games.RE7;
using REAssetExplorer.Games.RE7.Assets;
using REAssetExplorer.Rendering;
using REAssetExplorer.Rendering.Handlers;
using REAssetExplorer.Rendering.Shaders;
using REAssetExplorer.Testing;
using REAssetExplorer.Testing.Examples;
using REAssetExplorer.Testing.TestHelpers;

namespace REAssetExplorer.RenderTest;

class Program
{
    private static Renderer? _renderer;
    private static D3D11Device? _device;
    private static Mesh? _mesh;
    private static RenderWindow? _window;

    [STAThread]
    static void Main(string[] args)
    {
        _window = new RenderWindow("RE Asset Explorer - DX11 Model Viewer", 1280, 720);

        _window.Load += () => OnLoad(_window);
        _window.Update += OnUpdate;
        _window.Render += OnRender;
        _window.Resize += (sender, e) => OnResize(_window);
        
        // Camera controls
        _window.OnCameraRotate += OnCameraRotate;
        _window.OnCameraPan += OnCameraPan;
        _window.OnCameraMoveSpeedAdjust += OnCameraMoveSpeedAdjust;
        _window.OnOrbitModeStart += OnOrbitModeStart;
        _window.OnOrbitModeEnd += OnOrbitModeEnd;

        _window.Run();

        Cleanup();
    }

    static async Task OnLoad(RenderWindow window)
    {
        try
        {
            _device = D3D11Device.Create(window.Handle, window.ClientSize.Width, window.ClientSize.Height);

            _renderer = new Renderer(_device);

            // Mesh bounds: X=[0,2], Y=[0,2], Z=[0,2] -> Center at (1,1,1)
            _renderer.Camera.Position = new Vector3(1f, 1f, 8f);    // Aligned with mesh center, far back
            _renderer.Camera.Target = new Vector3(1f, 1f, 0f);      // Look at front of mesh
            _renderer.Camera.AspectRatio = (float)window.ClientSize.Width / window.ClientSize.Height;

            var pakFile = AssetExplorerHelper.LoadRE7Pak();
        
            /*var result = AssetExplorerHelper.LoadAssetByPattern<RE7MeshReader, MeshData>(
                pakFile,
                "pl0000.mesh.220128762",
                new RE7MeshReader()
            );*/

            var pakLoader = new PakLoader();
            var gameProvider = new RE7Provider();
            gameProvider.GameDirectory = TestDataPaths.RE7InstallPath!;
            var pakFilesResult = await pakLoader.LoadPakFilesAsync(
                gameProvider,
                TestDataPaths.RE7FileList
                //onProgress: Console.WriteLine
            );

            var assetLoader = new AssetLoader(
                gameProvider,
                pakFilesResult.Value!
            );

            var result = assetLoader.LoadAsset<MeshData>(
                "sm0000_scale.mesh.220128762",
                loadDependencies: true
            );
        
            if (result.Value != null)
            {
                _mesh = new Mesh(_device, result.Value, assetLoader);
            }
            else
            {
                Console.WriteLine($"Failed to load mesh: {result.Error}");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error al inicializar: {ex.Message}");
            Console.WriteLine($"Stack trace: {ex.StackTrace}");
            throw;
        }
    }

    static void OnUpdate(float deltaTime)
    {
        if (_renderer != null && _window != null)
        {
            // Handle F key for focus
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.F))
            {
                _renderer.Camera.FocusOn(Vector3.Zero, 5f);
                // Pequeño delay para evitar múltiples activaciones
                System.Threading.Thread.Sleep(100);
            }
            
            // Handle WASD movement
            var movement = Vector3.Zero;
            
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.W))
                movement.Z += 1;
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.S))
                movement.Z -= 1;
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.A))
                movement.X -= 1;
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.D))
                movement.X += 1;
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.E))
                movement.Y += 1;
            if (_window.IsKeyPressed(System.Windows.Forms.Keys.Q))
                movement.Y -= 1;
            
            if (movement != Vector3.Zero)
            {
                _renderer.Camera.Move(movement, deltaTime);
            }
            
            _renderer.Camera.Update(deltaTime);
        }
    }

    static void OnRender()
    {
        if (_renderer == null || _mesh == null)
        {
            return;
        }

        _renderer.BeginFrame(0.2f, 0.3f, 0.4f, 1.0f);

        var world = Matrix4x4.Identity;

        _renderer.DrawMesh(_mesh, world, _mesh.Materials);

        _renderer.EndFrame();
    }

    static void OnResize(RenderWindow window)
    {
        Console.WriteLine($"[Program] OnResize called: {window.ClientSize.Width}x{window.ClientSize.Height}");
        if (_renderer != null && window.ClientSize.Width > 0 && window.ClientSize.Height > 0)
        {
            _renderer.Resize(window.ClientSize.Width, window.ClientSize.Height);
        }
    }
    
    static void OnCameraRotate(float deltaX, float deltaY, bool isOrbital)
    {
        _renderer?.Camera.Rotate(deltaX, deltaY, isOrbital);
    }
    
    static void OnCameraPan(float deltaX, float deltaY)
    {
        _renderer?.Camera.Pan(deltaX, deltaY);
    }
    
    static void OnCameraMoveSpeedAdjust(float delta)
    {
        _renderer?.Camera.AdjustMoveSpeed(delta);
    }
    
    static void OnOrbitModeStart()
    {
        _renderer?.Camera.UpdateOrbitPivot();
    }
    
    static void OnOrbitModeEnd()
    {
        _renderer?.Camera.UpdateFreeLookAngles();
    }

    static void Cleanup()
    {
        _mesh?.Dispose();
        _renderer?.Dispose();
        _device?.Dispose();
    }
}

