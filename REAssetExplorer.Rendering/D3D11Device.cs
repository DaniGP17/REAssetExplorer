using System;
using SharpDX.Direct3D;
using SharpDX.Direct3D11;
using SharpDX.DXGI;
using Device = SharpDX.Direct3D11.Device;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Encapsula el dispositivo DirectX 11 y recursos relacionados
/// </summary>
public class D3D11Device : IDisposable
{
    public Device Device { get; private set; }
    public DeviceContext Context { get; private set; }
    public SwapChain SwapChain { get; private set; }
    public RenderTargetView? RenderTargetView { get; private set; }
    public DepthStencilView? DepthStencilView { get; private set; }
    public Texture2D? DepthStencilBuffer { get; private set; }

    // G-Buffers for deferred rendering
    public Texture2D? GBuffer0 { get; private set; }
    public Texture2D? GBuffer1 { get; private set; }
    public Texture2D? GBuffer2 { get; private set; }
    public Texture2D? GBuffer3 { get; private set; }
    
    public RenderTargetView? GBuffer0RTV { get; private set; }
    public RenderTargetView? GBuffer1RTV { get; private set; }
    public RenderTargetView? GBuffer2RTV { get; private set; }
    public RenderTargetView? GBuffer3RTV { get; private set; }
    
    public ShaderResourceView? GBuffer0SRV { get; private set; }
    public ShaderResourceView? GBuffer1SRV { get; private set; }
    public ShaderResourceView? GBuffer2SRV { get; private set; }
    public ShaderResourceView? GBuffer3SRV { get; private set; }
    
    // HDR Render Target for lighting pass (before tone mapping)
    public Texture2D? HDRRenderTarget { get; private set; }
    public RenderTargetView? HDRRenderTargetView { get; private set; }
    public ShaderResourceView? HDRShaderResourceView { get; private set; }

    public int Width { get; private set; }
    public int Height { get; private set; }
    
    public bool UseGBuffer { get; set; } = true; // Deferred rendering por defecto
    
    // Dummy white texture for missing texture slots
    public Texture2D? DummyTexture { get; private set; }
    public ShaderResourceView? DummySRV { get; private set; }
    
    // Constant Buffers
    public SharpDX.Direct3D11.Buffer? SceneInfoBuffer { get; private set; }
    public SharpDX.Direct3D11.Buffer? UserMaterialBuffer { get; private set; }
    public SharpDX.Direct3D11.Buffer? GBufferTypeBuffer { get; private set; }
    
    // Additional Pixel Shader Constant Buffers
    public SharpDX.Direct3D11.Buffer? RootConstantBuffer { get; private set; }        // PS slot 0
    public SharpDX.Direct3D11.Buffer? CheckerBoardInfoBuffer { get; private set; }    // PS slot 1
    public SharpDX.Direct3D11.Buffer? LightInfoBuffer { get; private set; }           // PS slot 2
    public SharpDX.Direct3D11.Buffer? OutdoorLightProbeParamBuffer { get; private set; } // PS slot 3

    private D3D11Device(Device device, DeviceContext context, SwapChain swapChain, int width, int height)
    {
        Device = device;
        Context = context;
        SwapChain = swapChain;
        Width = width;
        Height = height;
    }

    public static D3D11Device Create(IntPtr windowHandle, int width, int height)
    {
        var swapChainDesc = new SwapChainDescription
        {
            BufferCount = 1,
            ModeDescription = new ModeDescription(
                width, height,
                new Rational(60, 1),
                Format.R8G8B8A8_UNorm),
            IsWindowed = true,
            OutputHandle = windowHandle,
            SampleDescription = new SampleDescription(1, 0),
            SwapEffect = SwapEffect.Discard,
            Usage = Usage.RenderTargetOutput
        };

        Device device;
        SwapChain swapChain;
        Device.CreateWithSwapChain(
            DriverType.Hardware,
            DeviceCreationFlags.None,
            swapChainDesc,
            out device,
            out swapChain);

        var context = device.ImmediateContext;
        var d3dDevice = new D3D11Device(device, context, swapChain, width, height);
        d3dDevice.CreateRenderTargets();
        d3dDevice.CreateGBuffers();
        d3dDevice.CreateHDRRenderTarget();
        d3dDevice.CreateDummyTexture();
        d3dDevice.CreateConstantBuffers();

        return d3dDevice;
    }

    private void CreateRenderTargets()
    {
        // Crear render target view desde el backbuffer
        using (var backBuffer = SwapChain.GetBackBuffer<Texture2D>(0))
        {
            RenderTargetView = new RenderTargetView(Device, backBuffer);
        }

        // Crear depth stencil buffer
        var depthBufferDesc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.D24_UNorm_S8_UInt,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.DepthStencil,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };

        DepthStencilBuffer = new Texture2D(Device, depthBufferDesc);
        DepthStencilView = new DepthStencilView(Device, DepthStencilBuffer);
    }
    
    private void CreateGBuffers()
    {
        // RT0: Emissive (HDR float16)
        var gbuffer0Desc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R16G16B16A16_Float,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };
        GBuffer0 = new Texture2D(Device, gbuffer0Desc);
        GBuffer0RTV = new RenderTargetView(Device, GBuffer0);
        GBuffer0SRV = new ShaderResourceView(Device, GBuffer0);
        
        // RT1: Albedo + Metallic
        var gbuffer1Desc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R8G8B8A8_UNorm,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };
        GBuffer1 = new Texture2D(Device, gbuffer1Desc);
        GBuffer1RTV = new RenderTargetView(Device, GBuffer1);
        GBuffer1SRV = new ShaderResourceView(Device, GBuffer1);
        
        // RT2: Normal (octahedral) + Roughness + GBufferType
        var gbuffer2Desc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R8G8B8A8_UNorm,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };
        GBuffer2 = new Texture2D(Device, gbuffer2Desc);
        GBuffer2RTV = new RenderTargetView(Device, GBuffer2);
        GBuffer2SRV = new ShaderResourceView(Device, GBuffer2);
        
        // RT3: AO + ScreenUV + Flags
        var gbuffer3Desc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R8G8B8A8_UNorm,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };
        GBuffer3 = new Texture2D(Device, gbuffer3Desc);
        GBuffer3RTV = new RenderTargetView(Device, GBuffer3);
        GBuffer3SRV = new ShaderResourceView(Device, GBuffer3);
    }
    
    private void CreateHDRRenderTarget()
    {
        // HDR Render Target (R16G16B16A16_Float) for lighting pass output
        var hdrDesc = new Texture2DDescription
        {
            Width = Width,
            Height = Height,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R16G16B16A16_Float,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Default,
            BindFlags = BindFlags.RenderTarget | BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };
        HDRRenderTarget = new Texture2D(Device, hdrDesc);
        HDRRenderTargetView = new RenderTargetView(Device, HDRRenderTarget);
        HDRShaderResourceView = new ShaderResourceView(Device, HDRRenderTarget);
    }

    private void CreateDummyTexture()
    {
        // Create a 1x1 white texture for missing texture slots
        var textureDesc = new Texture2DDescription
        {
            Width = 1,
            Height = 1,
            MipLevels = 1,
            ArraySize = 1,
            Format = Format.R8G8B8A8_UNorm,
            SampleDescription = new SampleDescription(1, 0),
            Usage = ResourceUsage.Immutable,
            BindFlags = BindFlags.ShaderResource,
            CpuAccessFlags = CpuAccessFlags.None,
            OptionFlags = ResourceOptionFlags.None
        };

        // White pixel data (RGBA = 255, 255, 255, 255)
        var pixelData = new byte[] { 255, 255, 255, 255 };

        // Pin the array and create the texture
        var handle =
            System.Runtime.InteropServices.GCHandle.Alloc(pixelData,
                System.Runtime.InteropServices.GCHandleType.Pinned);
        try
        {
            var dataBox = new SharpDX.DataBox(
                handle.AddrOfPinnedObject(),
                4, // row pitch (4 bytes per pixel * 1 pixel wide)
                4 // depth pitch
            );

            DummyTexture = new Texture2D(Device, textureDesc, new[] { dataBox });
            DummySRV = new ShaderResourceView(Device, DummyTexture);
        }
        finally
        {
            handle.Free();
        }
    }


    private void CreateConstantBuffers()
    {
        Console.WriteLine("[D3D11Device] Creating constant buffers...");
        
        // Create SceneInfo constant buffer (register b0)
        int sceneInfoSize = SharpDX.Utilities.SizeOf<Shaders.SceneInfoBuffer>();
        Console.WriteLine($"[D3D11Device] SceneInfoBuffer size: {sceneInfoSize} bytes");
        
        // DirectX constant buffers must be multiple of 16 bytes
        int alignedSize = (sceneInfoSize + 15) & ~15;
        if (alignedSize != sceneInfoSize)
        {
            Console.WriteLine($"[D3D11Device] WARNING: SceneInfoBuffer size adjusted from {sceneInfoSize} to {alignedSize} bytes");
        }
        
        try
        {
            var sceneInfoDesc = new BufferDescription
            {
                Usage = ResourceUsage.Dynamic,
                SizeInBytes = alignedSize,
                BindFlags = BindFlags.ConstantBuffer,
                CpuAccessFlags = CpuAccessFlags.Write,
                OptionFlags = ResourceOptionFlags.None,
                StructureByteStride = 0
            };
            
            // Don't use DataStream in using block - it disposes the buffer!
            SceneInfoBuffer = new SharpDX.Direct3D11.Buffer(Device, sceneInfoDesc);
        Console.WriteLine($"[D3D11Device] SceneInfoBuffer created successfully (ptr: {SceneInfoBuffer.NativePointer}, IsDisposed: {SceneInfoBuffer.IsDisposed})");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[D3D11Device] ERROR creating SceneInfoBuffer: {ex.Message}");
            throw;
        }
        
        // Create UserMaterial constant buffer (register b1)
        int userMaterialSize = SharpDX.Utilities.SizeOf<Shaders.UserMaterialBuffer>();
        int alignedUserMatSize = (userMaterialSize + 15) & ~15;
        Console.WriteLine($"[D3D11Device] UserMaterialBuffer size: {userMaterialSize} bytes (aligned: {alignedUserMatSize})");
        
        var userMaterialDesc = new BufferDescription
        {
            Usage = ResourceUsage.Dynamic,
            SizeInBytes = alignedUserMatSize,
            BindFlags = BindFlags.ConstantBuffer,
            CpuAccessFlags = CpuAccessFlags.Write,
            OptionFlags = ResourceOptionFlags.None,
            StructureByteStride = 0
        };
        UserMaterialBuffer = new SharpDX.Direct3D11.Buffer(Device, userMaterialDesc);
        Console.WriteLine($"[D3D11Device] UserMaterialBuffer created successfully");
        
        // Create GBufferType constant buffer (register b2)
        int gbufferTypeSize = SharpDX.Utilities.SizeOf<Shaders.GBufferTypeBuffer>();
        int alignedGBufferSize = (gbufferTypeSize + 15) & ~15;
        Console.WriteLine($"[D3D11Device] GBufferTypeBuffer size: {gbufferTypeSize} bytes (aligned: {alignedGBufferSize})");
        
        var gbufferTypeDesc = new BufferDescription
        {
            Usage = ResourceUsage.Dynamic,
            SizeInBytes = alignedGBufferSize,
            BindFlags = BindFlags.ConstantBuffer,
            CpuAccessFlags = CpuAccessFlags.Write,
            OptionFlags = ResourceOptionFlags.None,
            StructureByteStride = 0
        };
        GBufferTypeBuffer = new SharpDX.Direct3D11.Buffer(Device, gbufferTypeDesc);
        Console.WriteLine($"[D3D11Device] GBufferTypeBuffer created successfully");
        
        // Create additional Pixel Shader constant buffers with dummy data
        RootConstantBuffer = CreateHelperConstantBuffer(typeof(Shaders.RootConstantBuffer), "RootConstantBuffer");
        CheckerBoardInfoBuffer = CreateHelperConstantBuffer(typeof(Shaders.CheckerBoardInfoBuffer), "CheckerBoardInfoBuffer");
        LightInfoBuffer = CreateHelperConstantBuffer(typeof(Shaders.LightInfoBuffer), "LightInfoBuffer");
        OutdoorLightProbeParamBuffer = CreateHelperConstantBuffer(typeof(Shaders.OutdoorLightProbeParamBuffer), "OutdoorLightProbeParamBuffer");
        
        Console.WriteLine("[D3D11Device] All constant buffers created successfully!");
    }
    
    private SharpDX.Direct3D11.Buffer CreateHelperConstantBuffer(Type structType, string name)
    {
        int size = System.Runtime.InteropServices.Marshal.SizeOf(structType);
        int alignedSize = (size + 15) & ~15;
        
        var desc = new BufferDescription
        {
            Usage = ResourceUsage.Dynamic,
            SizeInBytes = alignedSize,
            BindFlags = BindFlags.ConstantBuffer,
            CpuAccessFlags = CpuAccessFlags.Write,
            OptionFlags = ResourceOptionFlags.None,
            StructureByteStride = 0
        };
        
        var buffer = new SharpDX.Direct3D11.Buffer(Device, desc);
        Console.WriteLine($"[D3D11Device] {name} created successfully ({size} bytes, aligned: {alignedSize})");
        return buffer;
    }

    public void Resize(int width, int height)
    {
        if (width == Width && height == Height)
            return;

        Console.WriteLine($"[D3D11Device] Resizing from {Width}x{Height} to {width}x{height}");
        
        Width = width;
        Height = height;

        // Dispose constant buffers - may be invalidated by swapchain resize
        SceneInfoBuffer?.Dispose();
        UserMaterialBuffer?.Dispose();
        GBufferTypeBuffer?.Dispose();
        RootConstantBuffer?.Dispose();
        CheckerBoardInfoBuffer?.Dispose();
        LightInfoBuffer?.Dispose();
        OutdoorLightProbeParamBuffer?.Dispose();

        // Limpiar recursos anteriores
        RenderTargetView?.Dispose();
        DepthStencilView?.Dispose();
        DepthStencilBuffer?.Dispose();
        
        // Limpiar G-Buffers
        GBuffer0RTV?.Dispose();
        GBuffer1RTV?.Dispose();
        GBuffer2RTV?.Dispose();
        GBuffer3RTV?.Dispose();
        GBuffer0SRV?.Dispose();
        GBuffer1SRV?.Dispose();
        GBuffer2SRV?.Dispose();
        GBuffer3SRV?.Dispose();
        GBuffer0?.Dispose();
        GBuffer1?.Dispose();
        GBuffer2?.Dispose();
        GBuffer3?.Dispose();
        
        // Limpiar HDR render target
        HDRRenderTargetView?.Dispose();
        HDRShaderResourceView?.Dispose();
        HDRRenderTarget?.Dispose();

        // Resize del swapchain
        SwapChain.ResizeBuffers(1, width, height, Format.R8G8B8A8_UNorm, SwapChainFlags.None);

        // Recrear render targets
        CreateRenderTargets();
        CreateGBuffers();
        CreateHDRRenderTarget();
        
        // Recrear constant buffers (can be invalidated by swapchain resize)
        CreateConstantBuffers();
        
        Console.WriteLine($"[D3D11Device] Resize complete");
    }

    public void Clear(float r, float g, float b, float a = 1.0f)
    {
        if (RenderTargetView != null)
        {
            Context.ClearRenderTargetView(RenderTargetView, new SharpDX.Mathematics.Interop.RawColor4(r, g, b, a));
        }

        if (DepthStencilView != null)
        {
            Context.ClearDepthStencilView(DepthStencilView, DepthStencilClearFlags.Depth | DepthStencilClearFlags.Stencil, 1.0f, 0);
        }
        
        // Clear G-Buffers
        if (UseGBuffer)
        {
            if (GBuffer0RTV != null)
                Context.ClearRenderTargetView(GBuffer0RTV, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0));
            if (GBuffer1RTV != null)
                Context.ClearRenderTargetView(GBuffer1RTV, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 1)); // NEGRO para ver si algo se dibuja
            if (GBuffer2RTV != null)
                Context.ClearRenderTargetView(GBuffer2RTV, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0));
            if (GBuffer3RTV != null)
                Context.ClearRenderTargetView(GBuffer3RTV, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0));
        }
        
        // Clear HDR render target
        if (HDRRenderTargetView != null)
            Context.ClearRenderTargetView(HDRRenderTargetView, new SharpDX.Mathematics.Interop.RawColor4(0, 0, 0, 0));
    }

    public void Present()
    {
        SwapChain.Present(1, PresentFlags.None);
    }

    public void Dispose()
    {
        Console.WriteLine($"[D3D11Device] Dispose() called");
        
        // Dispose constant buffers
        SceneInfoBuffer?.Dispose();
        UserMaterialBuffer?.Dispose();
        GBufferTypeBuffer?.Dispose();
        RootConstantBuffer?.Dispose();
        CheckerBoardInfoBuffer?.Dispose();
        LightInfoBuffer?.Dispose();
        OutdoorLightProbeParamBuffer?.Dispose();
        
        RenderTargetView?.Dispose();
        DepthStencilView?.Dispose();
        DepthStencilBuffer?.Dispose();
        
        // Dispose dummy texture
        DummySRV?.Dispose();
        DummyTexture?.Dispose();
        
        // Dispose G-Buffers
        GBuffer0RTV?.Dispose();
        GBuffer1RTV?.Dispose();
        GBuffer2RTV?.Dispose();
        GBuffer3RTV?.Dispose();
        GBuffer0SRV?.Dispose();
        GBuffer1SRV?.Dispose();
        GBuffer2SRV?.Dispose();
        GBuffer3SRV?.Dispose();
        GBuffer0?.Dispose();
        GBuffer1?.Dispose();
        GBuffer2?.Dispose();
        GBuffer3?.Dispose();
        
        // Dispose HDR render target
        HDRRenderTargetView?.Dispose();
        HDRShaderResourceView?.Dispose();
        HDRRenderTarget?.Dispose();
        
        SwapChain?.Dispose();
        Context?.Dispose();
        Device?.Dispose();
    }
}
