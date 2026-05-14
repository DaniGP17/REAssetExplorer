using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using REAssetExplorer.Rendering.Handlers;
using REAssetExplorer.Rendering.Pipeline;
using REAssetExplorer.Rendering.Shaders;
using SharpDX.Direct3D;
using SharpDX.Direct3D11;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Sistema de renderizado principal usando DirectX 11
/// </summary>
public class Renderer : IDisposable
{
    public D3D11Device Device { get; }
    public Camera Camera { get; set; }

    private Shader? _defaultShader;
    private RasterizerState? _rasterizerState;
    private DepthStencilState? _depthStencilState;
    private BlendState? _blendState;
    private bool _firstDrawLogged = false;
    private bool _submeshStatsLogged = false;
    private DeferredLightingPass? _deferredLightingPass;
    private UnlitPass? _unlitPass;
    private ToneMappingPass? _toneMappingPass;
    private Grid? _grid;
    
    // Lighting mode toggle
    private bool _isLit = false;
    public bool IsLit 
    { 
        get => _isLit;
        set => _isLit = value;
    }
    
    // HDR Rendering properties
    public float Exposure 
    { 
        get => _toneMappingPass?.Exposure ?? 1.0f;
        set { if (_toneMappingPass != null) _toneMappingPass.Exposure = value; }
    }
    
    public float Gamma 
    { 
        get => _toneMappingPass?.Gamma ?? 2.2f;
        set { if (_toneMappingPass != null) _toneMappingPass.Gamma = value; }
    }
    
    public ToneMappingMode ToneMappingMode
    {
        get => _toneMappingPass?.Mode ?? ToneMappingMode.ACES;
        set { if (_toneMappingPass != null) _toneMappingPass.Mode = value; }
    }

    public Renderer(D3D11Device device)
    {
        Device = device ?? throw new ArgumentNullException(nameof(device));
        Camera = new Camera();

        InitializeStates();
        InitializeDefaultShader();
        
        // Crear grid de referencia
        _grid = new Grid(Device, size: 40, spacing: 1.0f);
        
        if (Device.UseGBuffer)
        {
            _deferredLightingPass = new DeferredLightingPass();
            _deferredLightingPass.Initialize(Device);
            
            _unlitPass = new UnlitPass();
            _unlitPass.Initialize(Device);
            
            _toneMappingPass = new ToneMappingPass();
            _toneMappingPass.Initialize(Device);
        }
    }

    private void InitializeStates()
    {
        // Configurar rasterizer state
        var rasterizerDesc = new RasterizerStateDescription
        {
            FillMode = FillMode.Solid,
            CullMode = CullMode.None, // Sin culling para ver todas las caras
            IsFrontCounterClockwise = true, // Cambiar winding order
            DepthBias = 0,
            DepthBiasClamp = 0,
            SlopeScaledDepthBias = 0,
            IsDepthClipEnabled = true,
            IsScissorEnabled = false,
            IsMultisampleEnabled = false,
            IsAntialiasedLineEnabled = false
        };
        _rasterizerState = new RasterizerState(Device.Device, rasterizerDesc);

        // Configurar depth stencil state
        var depthStencilDesc = new DepthStencilStateDescription
        {
            IsDepthEnabled = true,
            DepthWriteMask = DepthWriteMask.All,
            DepthComparison = Comparison.LessEqual, // Cambiar a LessEqual para ser más permisivo
            IsStencilEnabled = false
        };
        _depthStencilState = new DepthStencilState(Device.Device, depthStencilDesc);

        // Configurar blend state (OPACO - sin blending)
        var blendDesc = new BlendStateDescription
        {
            AlphaToCoverageEnable = false,
            IndependentBlendEnable = false
        };
        blendDesc.RenderTarget[0] = new RenderTargetBlendDescription
        {
            IsBlendEnabled = false,
            RenderTargetWriteMask = ColorWriteMaskFlags.All
        };
        _blendState = new BlendState(Device.Device, blendDesc);
    }

    private void InitializeDefaultShader()
    {
        _defaultShader = new Shader();
        //_defaultShader.CompileFromSource(Device, GetDefaultVertexShader(), GetDefaultPixelShader());
    }

    public void BeginFrame(float r = 0.1f, float g = 0.1f, float b = 0.15f, float a = 1.0f)
    {
        Device.Clear(r, g, b, a);
        if (Device.UseGBuffer)
        {
            var renderTargets = new RenderTargetView[]
            {
                Device.GBuffer0RTV!,
                Device.GBuffer1RTV!,
                Device.GBuffer2RTV!,
                Device.GBuffer3RTV!
            };
            Device.Context.OutputMerger.SetRenderTargets(Device.DepthStencilView, renderTargets);
        }
        else
        {
            // Forward rendering normal al backbuffer
            Device.Context.OutputMerger.SetRenderTargets(Device.DepthStencilView, Device.RenderTargetView);
        }
        
        Device.Context.OutputMerger.SetDepthStencilState(_depthStencilState);
        
        Device.Context.OutputMerger.SetBlendState(_blendState);
        Device.Context.Rasterizer.State = _rasterizerState;
        
        Device.Context.Rasterizer.SetViewport(0, 0, Device.Width, Device.Height);
        Device.Context.InputAssembler.PrimitiveTopology = PrimitiveTopology.TriangleList;
        
        if (!_firstDrawLogged)
        {
            Console.WriteLine($"[BeginFrame] Viewport: {Device.Width}x{Device.Height}, DepthTest: {_depthStencilState != null}, Culling: {_rasterizerState != null}");
        }
    }

    public void EndFrame()
    {
        Console.WriteLine($"[Renderer.EndFrame] UseGBuffer={Device.UseGBuffer}, IsLit={_isLit}");
        
        // Si usamos deferred rendering y está habilitado el lighting, ejecutar el lighting pass
        if (Device.UseGBuffer && _deferredLightingPass != null && _isLit)
        {
            Console.WriteLine("[Renderer.EndFrame] Executing LIGHTING pass");
            PerformLightingPass();
        }
        else if (Device.UseGBuffer && !_isLit)
        {
            Console.WriteLine("[Renderer.EndFrame] Executing UNLIT pass (albedo copy)");
            // Modo unlit: copiar el albedo (GBuffer1) directamente al backbuffer
            CopyAlbedoToBackbuffer();
        }
        else
        {
            Console.WriteLine("[Renderer.EndFrame] No post-processing (forward rendering)");
        }
        
        // Dibujar grid DESPUÉS del composite, directamente sobre el backbuffer
        // El render target ya debería estar en el backbuffer después del UnlitPass
        if (_grid != null)
        {
            var view = Camera.GetViewMatrix();
            var projection = Camera.GetProjectionMatrix();
            _grid.Draw(Device, view, projection);
        }
        
        Device.Present();
    }
    
    private void PerformLightingPass()
    {
        // CRITICAL: Unbind GBuffers as RTVs BEFORE using them as SRVs
        Device.Context.OutputMerger.SetRenderTargets(null, (RenderTargetView?)null);
        
        // Step 1: Render lighting to HDR render target
        Device.Context.OutputMerger.SetRenderTargets(null, Device.HDRRenderTargetView);
        
        // Clear HDR render target
        Device.Context.ClearRenderTargetView(Device.HDRRenderTargetView!, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0));
        
        // Ensure viewport is set correctly
        Device.Context.Rasterizer.SetViewport(0, 0, Device.Width, Device.Height);
        
        // Ejecutar el lighting pass (writes HDR values)
        _deferredLightingPass!.ExecuteLightingPass(Device);
        
        // Step 2: Tone mapping pass (HDR to LDR)
        Device.Context.OutputMerger.SetRenderTargets(null, Device.RenderTargetView);
        
        // Ensure viewport is set correctly
        Device.Context.Rasterizer.SetViewport(0, 0, Device.Width, Device.Height);
        
        if (_toneMappingPass != null)
        {
            _toneMappingPass.ExecuteToneMappingPass(Device);
        }
    }
    
    private void CopyAlbedoToBackbuffer()
    {
        Console.WriteLine($"[CopyAlbedoToBackbuffer] _unlitPass={_unlitPass != null}, GBuffer1SRV={Device.GBuffer1SRV != null}");
        
        // CRITICAL: Unbind GBuffers as RTVs BEFORE using them as SRVs
        // Otherwise DirectX 11 will crash with DEVICE_REMOVED
        Device.Context.OutputMerger.SetRenderTargets(null, (RenderTargetView?)null);
        
        // Now set backbuffer as render target
        Device.Context.OutputMerger.SetRenderTargets(null, Device.RenderTargetView);
        
        // Ensure viewport is set correctly
        Device.Context.Rasterizer.SetViewport(0, 0, Device.Width, Device.Height);
        
        // Ejecutar el unlit pass para mostrar solo el albedo
        if (_unlitPass != null && Device.GBuffer1SRV != null)
        {
            Console.WriteLine("[CopyAlbedoToBackbuffer] Executing UnlitPass.ExecuteUnlitPass()");
            _unlitPass.ExecuteUnlitPass(Device);
        }
        else
        {
            Console.WriteLine("[Renderer] Unlit pass not available");
        }
    }
    
    /// <summary>
    /// Toggles between lit (with lighting) and unlit (albedo only) rendering modes
    /// </summary>
    public void ToggleLighting()
    {
        _isLit = !_isLit;
        Console.WriteLine($"[Renderer] Lighting mode: {(_isLit ? "LIT" : "UNLIT")}");
    }
    
    /// <summary>
    /// Sets the lighting mode explicitly
    /// </summary>
    /// <param name="lit">True for lit mode, false for unlit mode</param>
    public void SetLightingMode(bool lit)
    {
        _isLit = lit;
        Console.WriteLine($"[Renderer] Lighting mode set to: {(_isLit ? "LIT" : "UNLIT")}");
    }
    
    /// <summary>
    /// Creates a SceneInfoBuffer with current camera and scene information
    /// </summary>
    /*public SceneInfoBuffer GetSceneInfoBuffer()
    {
        var view = Camera.GetViewMatrix();
        var projection = Camera.GetProjectionMatrix();
        var viewProj = view * projection;
        
        Matrix4x4.Invert(view, out var viewInv);
        Matrix4x4.Invert(projection, out var projInv);
        Matrix4x4.Invert(viewProj, out var viewProjInv);
        
        var frustumPlanes = Camera.GetFrustumPlanes();
        
        // Calculate Z to linear parameters
        // These convert non-linear depth buffer values to linear depth
        float n = Camera.NearPlane;
        float f = Camera.FarPlane;
        var zToLinear = new Vector3(
            (f - n) / (f * n),  // x: scale
            1.0f / n,            // y: bias
            -1.0f / f            // z: additional term
        );
        
        return new SceneInfoBuffer
        {
            ViewProjMat = ToSharpDXMatrix(viewProj),
            ViewMat = ToSharpDXMatrix(view),
            ViewInvMat = ToSharpDXMatrix(viewInv),
            ProjMat = ToSharpDXMatrix(projection),
            ProjInvMat = ToSharpDXMatrix(projInv),
            ViewProjInvMat = ToSharpDXMatrix(viewProjInv),
            PrevViewProjMat = ToSharpDXMatrix(Camera.PreviousViewProjectionMatrix),
            PrevCameraPos = new SharpDX.Vector3(Camera.PreviousPosition.X, Camera.PreviousPosition.Y, Camera.PreviousPosition.Z),
            CameraNearPlane = Camera.NearPlane,
            PrevCameraDir = new SharpDX.Vector3(Camera.GetDirection().X, Camera.GetDirection().Y, Camera.GetDirection().Z),
            CameraFarPlane = Camera.FarPlane,
            ViewFrustumPlane0 = new SharpDX.Vector4(frustumPlanes[0].X, frustumPlanes[0].Y, frustumPlanes[0].Z, frustumPlanes[0].W),
            ViewFrustumPlane1 = new SharpDX.Vector4(frustumPlanes[1].X, frustumPlanes[1].Y, frustumPlanes[1].Z, frustumPlanes[1].W),
            ViewFrustumPlane2 = new SharpDX.Vector4(frustumPlanes[2].X, frustumPlanes[2].Y, frustumPlanes[2].Z, frustumPlanes[2].W),
            ViewFrustumPlane3 = new SharpDX.Vector4(frustumPlanes[3].X, frustumPlanes[3].Y, frustumPlanes[3].Z, frustumPlanes[3].W),
            ViewFrustumPlane4 = new SharpDX.Vector4(frustumPlanes[4].X, frustumPlanes[4].Y, frustumPlanes[4].Z, frustumPlanes[4].W),
            ViewFrustumPlane5 = new SharpDX.Vector4(frustumPlanes[5].X, frustumPlanes[5].Y, frustumPlanes[5].Z, frustumPlanes[5].W),
            ZToLinear = new SharpDX.Vector3(zToLinear.X, zToLinear.Y, zToLinear.Z),
            SubdivisionLevel = 0.0f, // Can be set based on LOD system
            ScreenSize = new SharpDX.Vector2(Device.Width, Device.Height),
            ScreenInverseSize = new SharpDX.Vector2(1.0f / Device.Width, 1.0f / Device.Height)
        };
    }*/
    
    private static SharpDX.Matrix ToSharpDXMatrix(Matrix4x4 matrix)
    {
        return new SharpDX.Matrix(
            matrix.M11, matrix.M12, matrix.M13, matrix.M14,
            matrix.M21, matrix.M22, matrix.M23, matrix.M24,
            matrix.M31, matrix.M32, matrix.M33, matrix.M34,
            matrix.M41, matrix.M42, matrix.M43, matrix.M44);
    }

    public void DrawGrid()
    {
        if (_grid == null)
            return;
        
        var view = Camera.GetViewMatrix();
        var projection = Camera.GetProjectionMatrix();
        
        _grid.Draw(Device, view, projection);
    }

    public void DrawMesh(Mesh mesh, Matrix4x4 worldMatrix)
    {
        if (mesh.VertexBuffer == null || mesh.IndexBuffer == null || _defaultShader == null)
            return;

        // Configurar shaders
        Device.Context.VertexShader.Set(_defaultShader.VertexShader);
        Device.Context.PixelShader.Set(_defaultShader.PixelShader);
        Device.Context.InputAssembler.InputLayout = _defaultShader.InputLayout;

        // Actualizar matrices
        var view = Camera.GetViewMatrix();
        var projection = Camera.GetProjectionMatrix();
        _defaultShader.SetMatrices(Device, worldMatrix, view, projection);

        // Configurar buffers
        Device.Context.InputAssembler.SetVertexBuffers(0, new VertexBufferBinding(mesh.VertexBuffer, mesh.VertexStride, 0));
        Device.Context.InputAssembler.SetIndexBuffer(mesh.IndexBuffer, SharpDX.DXGI.Format.R32_UInt, 0);

        // Dibujar
        Device.Context.DrawIndexed(mesh.IndexCount, 0, 0);
    }

    /*public void DrawMesh(Mesh mesh, Matrix4x4 worldMatrix, Shader customShader, params Texture[] textures)
    {
        if (mesh.VertexBuffer == null || mesh.IndexBuffer == null || customShader == null)
        {
            Console.WriteLine($"[DrawMesh] Null check failed: VB={mesh.VertexBuffer != null}, IB={mesh.IndexBuffer != null}, Shader={customShader != null}");
            return;
        }

        if (!_firstDrawLogged)
        {
            Console.WriteLine($"[DrawMesh] First draw: {mesh.IndexCount} indices, {mesh.VertexCount} vertices, stride={mesh.VertexStride}");
            Console.WriteLine($"[DrawMesh] Camera Pos: {Camera.Position}, Target: {Camera.Target}");
            _firstDrawLogged = true;
        }

        Device.Context.VertexShader.Set(customShader.VertexShader);
        Device.Context.PixelShader.Set(customShader.PixelShader);
        Device.Context.InputAssembler.InputLayout = customShader.InputLayout;

        var view = Camera.GetViewMatrix();
        var projection = Camera.GetProjectionMatrix();
        customShader.SetMatrices(Device, worldMatrix, view, projection);

        if (textures != null && textures.Length > 0)
        {
            customShader.SetTextures(Device, textures);
        }

        Device.Context.InputAssembler.SetVertexBuffers(0, new VertexBufferBinding(mesh.VertexBuffer, mesh.VertexStride, 0));
        Device.Context.InputAssembler.SetIndexBuffer(mesh.IndexBuffer, SharpDX.DXGI.Format.R32_UInt, 0);

        Device.Context.DrawIndexed(mesh.IndexCount, 0, 0);
    }*/

    public void DrawMesh(Mesh mesh, Matrix4x4 worldMatrix, Dictionary<byte, MaterialInstance> materials)
    {
        if (mesh.VertexBuffer == null || mesh.IndexBuffer == null)
        {
            Console.WriteLine("DrawMesh: VertexBuffers or IndexBuffer is null!");
            return;
        }

        Device.Context.InputAssembler.SetVertexBuffers(0, new VertexBufferBinding(mesh.VertexBuffer, mesh.VertexStride, 0));
        Device.Context.InputAssembler.SetIndexBuffer(mesh.IndexBuffer, SharpDX.DXGI.Format.R32_UInt, 0);

        var view = Camera.GetViewMatrix();
        var projection = Camera.GetProjectionMatrix();
        
        var renderState = mesh.RenderState;
        var activeLod = renderState.ActiveLodIndex;
        
        Console.WriteLine($"[DrawMesh] Total submeshes: {mesh.SubMeshes.Count}, ActiveLOD: {activeLod}");
        
        // If no LOD is active, don't render anything
        if (activeLod < 0)
        {
            Console.WriteLine("[DrawMesh] WARNING: No active LOD! Cannot render.");
            return;
        }
        
        int totalSubmeshes = mesh.SubMeshes.Count;
        int renderedSubmeshes = 0;
        int skippedByLod = 0;
        int skippedByVisibility = 0;
        
        foreach (var subMesh in mesh.SubMeshes)
        {
            // Skip if not in active LOD
            if (subMesh.LodIndex != activeLod)
            {
                skippedByLod++;
                if (skippedByLod == 1) // Log first skip
                    Console.WriteLine($"[DrawMesh] Skipping submesh: LodIndex={subMesh.LodIndex} != ActiveLOD={activeLod}");
                continue;
            }
            
            // Skip if part or material is not visible
            if (!renderState.IsPartVisible(subMesh.PartId) || !renderState.IsMaterialVisible(subMesh.MaterialID))
            {
                skippedByVisibility++;
                if (skippedByVisibility == 1) // Log first skip
                    Console.WriteLine($"[DrawMesh] Skipping submesh: PartVisible={renderState.IsPartVisible(subMesh.PartId)}, MaterialVisible={renderState.IsMaterialVisible(subMesh.MaterialID)}");
                continue;
            }
            
            // CRITICAL: Bind material for this submesh
            if (materials.TryGetValue(subMesh.MaterialID, out var material))
            {
                // Bind material pipeline and textures (for GBuffer pass)
                material.Bind(Device, RenderPass.GBuffer);
                
                // IMPORTANT: Update SceneInfo AFTER material.Bind() to prevent unbinding
                UpdateSceneInfo(worldMatrix, view, projection);
            }
            else
            {
                Console.WriteLine($"[DrawMesh] WARNING: Material {subMesh.MaterialID} for submesh not found!");
                continue;
            }
            
            renderedSubmeshes++;
            
            if (renderedSubmeshes == 1) // Log only first draw
            {
                Console.WriteLine($"[DrawMesh] Drawing submesh: IndexCount={subMesh.IndexCount}, StartIndex={subMesh.StartIndex}, BaseVertex={subMesh.BaseVertex}");
                
                // VERIFY: Check what's actually bound to VS slot 0 right before draw
                var boundBuffers = Device.Context.VertexShader.GetConstantBuffers(0, 1);
                if (boundBuffers != null && boundBuffers.Length > 0 && boundBuffers[0] != null)
                {
                    Console.WriteLine($"[DrawMesh] ✓ VS slot 0 HAS buffer bound: ptr={boundBuffers[0].NativePointer}, same as SceneInfo? {boundBuffers[0].NativePointer == Device.SceneInfoBuffer.NativePointer}");
                    foreach (var buf in boundBuffers)
                        buf?.Dispose();
                }
                else
                {
                    Console.WriteLine($"[DrawMesh] ✗ VS slot 0 is NULL - constant buffer was UNBOUND!");
                }
            }
            
            Device.Context.DrawIndexed(subMesh.IndexCount, subMesh.StartIndex, subMesh.BaseVertex);
        }
        
        Console.WriteLine($"[DrawMesh] Rendered {renderedSubmeshes}/{totalSubmeshes} submeshes (SkippedByLOD={skippedByLod}, SkippedByVisibility={skippedByVisibility})");
        
        if (totalSubmeshes > 0 && renderedSubmeshes == 0)
        {
            Console.WriteLine($"DrawMesh WARNING: Total submeshes={totalSubmeshes}, Rendered={renderedSubmeshes}, SkippedByLOD={skippedByLod}, SkippedByVisibility={skippedByVisibility}, ActiveLOD={activeLod}");
        }
    }

    private void UpdateSceneInfo(Matrix4x4 worldMatrix, Matrix4x4 viewMatrix, Matrix4x4 projectionMatrix)
    {
        if (Device.SceneInfoBuffer == null)
            return;
        
        var worldViewProj = worldMatrix * viewMatrix * projectionMatrix;
        
        // LOG matrices detailed - first time only
        if (!_firstDrawLogged)
        {
            Console.WriteLine($"[UpdateSceneInfo] === MATRIX DEBUG ===");
            Console.WriteLine($"World: [{worldMatrix.M11:F2},{worldMatrix.M12:F2},{worldMatrix.M13:F2},{worldMatrix.M14:F2}]");
            Console.WriteLine($"       [{worldMatrix.M21:F2},{worldMatrix.M22:F2},{worldMatrix.M23:F2},{worldMatrix.M24:F2}]");
            Console.WriteLine($"       [{worldMatrix.M31:F2},{worldMatrix.M32:F2},{worldMatrix.M33:F2},{worldMatrix.M34:F2}]");
            Console.WriteLine($"       [{worldMatrix.M41:F2},{worldMatrix.M42:F2},{worldMatrix.M43:F2},{worldMatrix.M44:F2}]");
            
            Console.WriteLine($"View: [{viewMatrix.M11:F2},{viewMatrix.M12:F2},{viewMatrix.M13:F2},{viewMatrix.M14:F2}]");
            Console.WriteLine($"      [{viewMatrix.M21:F2},{viewMatrix.M22:F2},{viewMatrix.M23:F2},{viewMatrix.M24:F2}]");
            Console.WriteLine($"      [{viewMatrix.M31:F2},{viewMatrix.M32:F2},{viewMatrix.M33:F2},{viewMatrix.M34:F2}]");
            Console.WriteLine($"      [{viewMatrix.M41:F2},{viewMatrix.M42:F2},{viewMatrix.M43:F2},{viewMatrix.M44:F2}]");
            
            Console.WriteLine($"Projection: [{projectionMatrix.M11:F2},{projectionMatrix.M12:F2},{projectionMatrix.M13:F2},{projectionMatrix.M14:F2}]");
            Console.WriteLine($"            [{projectionMatrix.M21:F2},{projectionMatrix.M22:F2},{projectionMatrix.M23:F2},{projectionMatrix.M24:F2}]");
            Console.WriteLine($"            [{projectionMatrix.M31:F2},{projectionMatrix.M32:F2},{projectionMatrix.M33:F2},{projectionMatrix.M34:F2}]");
            Console.WriteLine($"            [{projectionMatrix.M41:F2},{projectionMatrix.M42:F2},{projectionMatrix.M43:F2},{projectionMatrix.M44:F2}]");
            
            Console.WriteLine($"Camera Pos=({Camera.Position.X:F2},{Camera.Position.Y:F2},{Camera.Position.Z:F2})");
            _firstDrawLogged = true;
        }
        
        var sceneInfo = new Shaders.SceneInfoBuffer
        {
            World = ToSharpDXMatrix(worldMatrix),
            View = ToSharpDXMatrix(viewMatrix),
            Projection = ToSharpDXMatrix(projectionMatrix),
            WorldViewProjection = ToSharpDXMatrix(worldViewProj),
            CameraPosition = new SharpDX.Vector4(Camera.Position.X, Camera.Position.Y, Camera.Position.Z, 1.0f),
            ViewportSize = new SharpDX.Vector4(Device.Width, Device.Height, 1.0f / Device.Width, 1.0f / Device.Height)
        };
        
        // Update constant buffer - write manually instead of using Utilities.Write
        var dataBox = Device.Context.MapSubresource(Device.SceneInfoBuffer, 0, SharpDX.Direct3D11.MapMode.WriteDiscard, SharpDX.Direct3D11.MapFlags.None);
        
        unsafe
        {
            float* ptr = (float*)dataBox.DataPointer;
            int offset = 0;
            
            // Write World matrix (16 floats)
            ptr[offset++] = sceneInfo.World.M11; ptr[offset++] = sceneInfo.World.M12; ptr[offset++] = sceneInfo.World.M13; ptr[offset++] = sceneInfo.World.M14;
            ptr[offset++] = sceneInfo.World.M21; ptr[offset++] = sceneInfo.World.M22; ptr[offset++] = sceneInfo.World.M23; ptr[offset++] = sceneInfo.World.M24;
            ptr[offset++] = sceneInfo.World.M31; ptr[offset++] = sceneInfo.World.M32; ptr[offset++] = sceneInfo.World.M33; ptr[offset++] = sceneInfo.World.M34;
            ptr[offset++] = sceneInfo.World.M41; ptr[offset++] = sceneInfo.World.M42; ptr[offset++] = sceneInfo.World.M43; ptr[offset++] = sceneInfo.World.M44;
            
            // Write View matrix (16 floats)
            ptr[offset++] = sceneInfo.View.M11; ptr[offset++] = sceneInfo.View.M12; ptr[offset++] = sceneInfo.View.M13; ptr[offset++] = sceneInfo.View.M14;
            ptr[offset++] = sceneInfo.View.M21; ptr[offset++] = sceneInfo.View.M22; ptr[offset++] = sceneInfo.View.M23; ptr[offset++] = sceneInfo.View.M24;
            ptr[offset++] = sceneInfo.View.M31; ptr[offset++] = sceneInfo.View.M32; ptr[offset++] = sceneInfo.View.M33; ptr[offset++] = sceneInfo.View.M34;
            ptr[offset++] = sceneInfo.View.M41; ptr[offset++] = sceneInfo.View.M42; ptr[offset++] = sceneInfo.View.M43; ptr[offset++] = sceneInfo.View.M44;
            
            // Write Projection matrix (16 floats)
            ptr[offset++] = sceneInfo.Projection.M11; ptr[offset++] = sceneInfo.Projection.M12; ptr[offset++] = sceneInfo.Projection.M13; ptr[offset++] = sceneInfo.Projection.M14;
            ptr[offset++] = sceneInfo.Projection.M21; ptr[offset++] = sceneInfo.Projection.M22; ptr[offset++] = sceneInfo.Projection.M23; ptr[offset++] = sceneInfo.Projection.M24;
            ptr[offset++] = sceneInfo.Projection.M31; ptr[offset++] = sceneInfo.Projection.M32; ptr[offset++] = sceneInfo.Projection.M33; ptr[offset++] = sceneInfo.Projection.M34;
            ptr[offset++] = sceneInfo.Projection.M41; ptr[offset++] = sceneInfo.Projection.M42; ptr[offset++] = sceneInfo.Projection.M43; ptr[offset++] = sceneInfo.Projection.M44;
            
            // Write WorldViewProjection matrix (16 floats)
            ptr[offset++] = sceneInfo.WorldViewProjection.M11; ptr[offset++] = sceneInfo.WorldViewProjection.M12; ptr[offset++] = sceneInfo.WorldViewProjection.M13; ptr[offset++] = sceneInfo.WorldViewProjection.M14;
            ptr[offset++] = sceneInfo.WorldViewProjection.M21; ptr[offset++] = sceneInfo.WorldViewProjection.M22; ptr[offset++] = sceneInfo.WorldViewProjection.M23; ptr[offset++] = sceneInfo.WorldViewProjection.M24;
            ptr[offset++] = sceneInfo.WorldViewProjection.M31; ptr[offset++] = sceneInfo.WorldViewProjection.M32; ptr[offset++] = sceneInfo.WorldViewProjection.M33; ptr[offset++] = sceneInfo.WorldViewProjection.M34;
            ptr[offset++] = sceneInfo.WorldViewProjection.M41; ptr[offset++] = sceneInfo.WorldViewProjection.M42; ptr[offset++] = sceneInfo.WorldViewProjection.M43; ptr[offset++] = sceneInfo.WorldViewProjection.M44;
            
            // Write CameraPosition (4 floats)
            ptr[offset++] = sceneInfo.CameraPosition.X; ptr[offset++] = sceneInfo.CameraPosition.Y; ptr[offset++] = sceneInfo.CameraPosition.Z; ptr[offset++] = sceneInfo.CameraPosition.W;
            
            // Write ViewportSize (4 floats)
            ptr[offset++] = sceneInfo.ViewportSize.X; ptr[offset++] = sceneInfo.ViewportSize.Y; ptr[offset++] = sceneInfo.ViewportSize.Z; ptr[offset++] = sceneInfo.ViewportSize.W;
            
            Console.WriteLine($"[UpdateSceneInfo] Manually wrote {offset} floats ({offset * 4} bytes) to buffer");
        }
        
        Device.Context.UnmapSubresource(Device.SceneInfoBuffer, 0);
        
        // Bind to VS slot 0
        Device.Context.VertexShader.SetConstantBuffer(0, Device.SceneInfoBuffer);
    }
    
    public void Resize(int width, int height)
    {
        Device.Resize(width, height);
        Camera.AspectRatio = (float)width / height;
    }

    public void Dispose()
    {
        _defaultShader?.Dispose();
        _rasterizerState?.Dispose();
        _depthStencilState?.Dispose();
        _blendState?.Dispose();
        _deferredLightingPass?.Dispose();
        _unlitPass?.Dispose();
        _toneMappingPass?.Dispose();
        _grid?.Dispose();
    }
}
