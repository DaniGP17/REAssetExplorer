using System;
using System.Runtime.InteropServices;
using Vortice.Direct3D12;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.DX12;

/// <summary>
/// Manages a constant buffer with CPU-GPU upload capability and per-frame buffering
/// </summary>
/// <typeparam name="T">Struct type for constant buffer data (must be aligned to 256 bytes)</typeparam>
public class ConstantBuffer<T> : IDisposable where T : unmanaged
{
    private readonly ID3D12Device _device;
    private readonly int _frameCount;
    private ID3D12Resource[]? _uploadBuffers;
    private IntPtr[]? _mappedData;
    private int _alignedSize;
    private bool _disposed;

    public int AlignedSize => _alignedSize;
    
    public ConstantBuffer(ID3D12Device device, int frameCount = 3)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
        _frameCount = frameCount;
        
        // Calculate aligned size (constant buffers must be 256-byte aligned)
        int structSize = Marshal.SizeOf<T>();
        _alignedSize = (structSize + 255) & ~255;
        
        Initialize();
    }

    private void Initialize()
    {
        _uploadBuffers = new ID3D12Resource[_frameCount];
        _mappedData = new IntPtr[_frameCount];

        var heapProperties = new HeapProperties(HeapType.Upload);
        var resourceDesc = ResourceDescription.Buffer((ulong)_alignedSize);

        for (int i = 0; i < _frameCount; i++)
        {
            _uploadBuffers[i] = _device.CreateCommittedResource(
                heapProperties,
                HeapFlags.None,
                resourceDesc,
                ResourceStates.GenericRead
            );

            // Map once and keep mapped (upload heaps can stay mapped)
            unsafe
            {
                IntPtr pData;
                _uploadBuffers[i].Map(0, null, &pData);
                _mappedData[i] = pData;
            }
        }
    }

    /// <summary>
    /// Updates the constant buffer data for the specified frame
    /// </summary>
    public void Update(int frameIndex, ref T data)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ConstantBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        unsafe
        {
            // Copy data to mapped memory
            void* src = Unsafe.AsPointer(ref data);
            Buffer.MemoryCopy(src, _mappedData![frameIndex].ToPointer(), _alignedSize, Marshal.SizeOf<T>());
        }
    }

    /// <summary>
    /// Gets the GPU virtual address for the specified frame
    /// </summary>
    public ulong GetGPUVirtualAddress(int frameIndex)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ConstantBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        return _uploadBuffers![frameIndex].GPUVirtualAddress;
    }

    /// <summary>
    /// Gets the resource for the specified frame
    /// </summary>
    public ID3D12Resource GetResource(int frameIndex)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(ConstantBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        return _uploadBuffers![frameIndex];
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        if (_uploadBuffers != null)
        {
            for (int i = 0; i < _frameCount; i++)
            {
                if (_uploadBuffers[i] != null)
                {
                    // Unmap before disposing
                    _uploadBuffers[i].Unmap(0);
                    _uploadBuffers[i].Dispose();
                }
            }
            _uploadBuffers = null;
        }

        _mappedData = null;
        _disposed = true;
    }
}

// Helper class for methods not in Vortice
internal static class Unsafe
{
    public static unsafe void* AsPointer<T>(ref T value)
    {
        return System.Runtime.CompilerServices.Unsafe.AsPointer(ref value);
    }
}
