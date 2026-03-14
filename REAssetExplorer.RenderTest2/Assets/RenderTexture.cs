using System;
using System.Linq;
using System.Runtime.InteropServices;
using BCnEncoder.Shared.ImageFiles;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.RenderTest2.Assets;
using REAssetExplorer.RenderTest2.DX12;
using Vortice.Direct3D12;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.Interop.Structures;

[StructLayout(LayoutKind.Sequential)]
public struct MipInfo
{
    public ulong Offset;
    public uint Size;
    public uint Width;
    public uint Height;
}

public class RenderTexture : Resource
{
    public override ResourceType Type => ResourceType.Texture;
    
    public uint Width;
    public uint Height;
    public uint Depth;
    public uint MipLevels;
    public uint ArraySize;
    public DxgiFormat Format;
    public byte[] PixelData = Array.Empty<byte>();
    public uint PixelDataSize;
    public MipInfo[] Mips = Array.Empty<MipInfo>();
    public uint MipCount;
    
    // GPU resource - cached after first creation
    private ID3D12Resource? _gpuResource;
    private bool _gpuResourceCreated = false;
    private bool _dataUploaded = false;
    
    /// <summary>
    /// Gets or creates the GPU resource for this texture
    /// </summary>
    /// <param name="device">D3D12 device to create the resource on</param>
    /// <param name="commandQueue">Optional command queue for uploading texture data. If null, resource is created but data is not uploaded.</param>
    /// <returns>GPU texture resource, or null if creation failed</returns>
    public ID3D12Resource? GetOrCreateGpuResource(ID3D12Device device, DX12CommandQueue? commandQueue = null)
    {
        if (_gpuResourceCreated)
        {
            // If resource exists but data wasn't uploaded before and we now have a command queue, upload it
            if (!_dataUploaded && commandQueue != null && _gpuResource != null)
            {
                UploadTextureData(device, _gpuResource, commandQueue);
                _dataUploaded = true;
            }
            return _gpuResource;
        }
        
        _gpuResource = CreateGpuResource(device, commandQueue);
        _gpuResourceCreated = true;
        
        if (_gpuResource != null && commandQueue != null)
        {
            _dataUploaded = true;
        }
        
        return _gpuResource;
    }
    
    /// <summary>
    /// Creates a D3D12 texture resource from this RenderTexture's data
    /// </summary>
    private ID3D12Resource? CreateGpuResource(ID3D12Device device, DX12CommandQueue? commandQueue)
    {
        if (device == null || PixelData == null || PixelData.Length == 0)
        {
            Console.WriteLine($"Cannot create GPU resource: invalid data (PixelData length: {PixelData?.Length ?? 0})");
            return null;
        }
        
        try
        {
            // Convert DxgiFormat to Vortice Format
            var format = (Format)Format;
            
            // Create texture description
            var textureDesc = ResourceDescription.Texture2D(
                format,
                Width,
                Height,
                (ushort)ArraySize,
                (ushort)MipLevels);
            
            // Create resource in default heap
            var heapProps = new HeapProperties(HeapType.Default);
            var texture = device.CreateCommittedResource(
                heapProps,
                HeapFlags.None,
                textureDesc,
                ResourceStates.CopyDest);
            
            if (texture == null)
            {
                Console.WriteLine($"Failed to create texture resource: {Width}x{Height}, Format: {format}");
                return null;
            }
            
            // Upload texture data if command queue is available
            if (commandQueue != null)
            {
                UploadTextureData(device, texture, commandQueue);
            }
            else
            {
                Console.WriteLine($"Texture resource created but data not uploaded (no command queue provided)");
            }
            
            return texture;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception creating GPU texture resource: {ex.Message}");
            Console.WriteLine($"Stack trace: {ex.StackTrace}");
            return null;
        }
    }
    
    /// <summary>
    /// Uploads texture data to GPU using an upload buffer
    /// </summary>
    private void UploadTextureData(ID3D12Device device, ID3D12Resource texture, DX12CommandQueue commandQueue)
    {
        try
        {
            // Calculate upload buffer size
            var layouts = new PlacedSubresourceFootPrint[MipLevels * ArraySize];
            var numRows = new int[MipLevels * ArraySize];
            var rowSizeInBytes = new ulong[MipLevels * ArraySize];
            
            device.GetCopyableFootprints(texture.Description, 0, (int)(MipLevels * ArraySize), 
                0, layouts, numRows, rowSizeInBytes, out var totalBytes);
            
            // Create upload buffer
            var uploadHeapProps = new HeapProperties(HeapType.Upload);
            var uploadBufferDesc = ResourceDescription.Buffer(totalBytes);
            
            var uploadBuffer = device.CreateCommittedResource(
                uploadHeapProps,
                HeapFlags.None,
                uploadBufferDesc,
                ResourceStates.GenericRead);
            
            if (uploadBuffer == null)
            {
                Console.WriteLine("Failed to create upload buffer for texture");
                return;
            }
            
            try
            {
                // Map upload buffer and copy data
                unsafe
                {
                    IntPtr pData;
                    uploadBuffer.Map(0, null, &pData);
                    
                    try
                    {
                        // Copy each subresource (mip level)
                        for (uint arraySlice = 0; arraySlice < ArraySize; arraySlice++)
                        {
                            for (uint mipLevel = 0; mipLevel < MipLevels; mipLevel++)
                            {
                                uint subresourceIndex = mipLevel + arraySlice * MipLevels;
                                
                                if (subresourceIndex >= Mips.Length)
                                    continue;
                                
                                var mipInfo = Mips[subresourceIndex];
                                var layout = layouts[subresourceIndex];
                                
                                // Calculate destination pointer in upload buffer
                                byte* destPtr = (byte*)pData.ToPointer() + layout.Offset;
                                
                                // Copy row by row (handling pitch alignment)
                                ulong srcRowBytes = rowSizeInBytes[subresourceIndex]; // bytes reales por fila (sin padding)
                                int rows = numRows[subresourceIndex];                 // filas reales (para BCn serán filas de bloques)
                                uint dstRowPitch = (uint)layout.Footprint.RowPitch;

                                for (int row = 0; row < rows; row++)
                                {
                                    long srcOffset = (long)mipInfo.Offset + row * (long)srcRowBytes;
                                    ulong dstOffset = (ulong)row * dstRowPitch;

                                    // Copia solo los bytes útiles por fila (no el RowPitch completo)
                                    if (srcOffset >= 0 && srcOffset + (long)srcRowBytes <= PixelData.Length)
                                    {
                                        Marshal.Copy(
                                            PixelData,
                                            (int)srcOffset,
                                            (IntPtr)(destPtr + dstOffset),
                                            (int)srcRowBytes);
                                    }
                                }
                            }
                        }
                    }
                    finally
                    {
                        uploadBuffer.Unmap(0);
                    }
                }
                
                // Record copy commands
                var commandAllocator = commandQueue.CommandAllocator;
                var commandList = commandQueue.CommandList;
                
                commandAllocator.Reset();
                commandList.Reset(commandAllocator, null);
                
                // Copy from upload buffer to texture
                for (uint i = 0; i < MipLevels * ArraySize; i++)
                {
                    var dstLocation = new TextureCopyLocation(texture, (int)i);
                    var srcLocation = new TextureCopyLocation(uploadBuffer, layouts[i]);
                    
                    commandList.CopyTextureRegion(dstLocation, 0, 0, 0, srcLocation);
                }
                
                // Transition texture to shader resource state
                commandList.ResourceBarrierTransition(texture, 
                    ResourceStates.CopyDest, 
                    ResourceStates.PixelShaderResource);
                
                commandList.Close();
                
                // Execute and wait
                commandQueue.CommandQueue.ExecuteCommandList(commandList);
                commandQueue.WaitForGPU();
                
                Console.WriteLine($"Texture uploaded successfully: {Width}x{Height}, {MipLevels} mips, Format: {Format}");
            }
            finally
            {
                uploadBuffer.Dispose();
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception uploading texture data: {ex.Message}");
            Console.WriteLine($"Stack trace: {ex.StackTrace}");
        }
    }
    
    public override void Dispose()
    {
        _gpuResource?.Dispose();
        _gpuResource = null;
        _gpuResourceCreated = false;
        
        base.Dispose();
    }
}

public static class TextureMapper
{
    public static RenderTexture MapToRenderTexture(string name, TextureData textureData)
    {
        var renderText = new RenderTexture
        {
            Width = textureData.Width,
            Height = textureData.Height,
            Depth = 1,
            MipLevels = (uint)textureData.Mips.Length,
            ArraySize = (uint)textureData.NumImages,
            Format = (DxgiFormat)textureData.Format,
            PixelData = textureData.RawMipData,
            Mips = new MipInfo[textureData.Mips.Length]
        };

        ulong baseOffset = textureData.Mips.Min(m => m.Offset);
        
        int mipsPerImage = textureData.MipsPerImage;
        for (int i = 0; i < textureData.Mips.Length; i++)
        {
            int mipIndex = i % mipsPerImage;

            uint mipWidth = Math.Max(1u, renderText.Width >> mipIndex);
            uint mipHeight = Math.Max(1u, renderText.Height >> mipIndex);

            renderText.Mips[i] = new MipInfo
            {
                Offset = textureData.Mips[i].Offset - baseOffset,
                Size = textureData.Mips[i].Size,
                Width = mipWidth,
                Height = mipHeight
            };
        }

        return renderText;
    }
}