using System;
using System.Runtime.InteropServices;
using Vortice.Direct3D12;
using Vortice.DXGI;

namespace REAssetExplorer.RenderTest2.DX12;

/// <summary>
/// Manages a structured buffer for GPU-readable array data (e.g., light parameters)
/// </summary>
/// <typeparam name="T">Element type for the structured buffer</typeparam>
public class StructuredBuffer<T> : IDisposable where T : unmanaged
{
    private readonly ID3D12Device _device;
    private readonly int _frameCount;
    private readonly int _maxElements;
    private ID3D12Resource[]? _uploadBuffers;
    private ID3D12Resource[]? _defaultBuffers;
    private IntPtr[]? _mappedData;
    private int _elementSize;
    private int _bufferSize;
    private bool _disposed;

    public int MaxElements => _maxElements;
    public int ElementSize => _elementSize;
    public int BufferSize => _bufferSize;

    public StructuredBuffer(ID3D12Device device, int maxElements, int frameCount = 3)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
        _frameCount = frameCount;
        _maxElements = maxElements;
        _elementSize = Marshal.SizeOf<T>();
        _bufferSize = _elementSize * _maxElements;

        Initialize();
    }

    private void Initialize()
    {
        _uploadBuffers = new ID3D12Resource[_frameCount];
        _defaultBuffers = new ID3D12Resource[_frameCount];
        _mappedData = new IntPtr[_frameCount];

        var uploadHeapProps = new HeapProperties(HeapType.Upload);
        var defaultHeapProps = new HeapProperties(HeapType.Default);
        var bufferDesc = ResourceDescription.Buffer((ulong)_bufferSize);

        for (int i = 0; i < _frameCount; i++)
        {
            // Create upload buffer for CPU writes
            _uploadBuffers[i] = _device.CreateCommittedResource(
                uploadHeapProps,
                HeapFlags.None,
                bufferDesc,
                ResourceStates.GenericRead
            );

            // Create default buffer for GPU reads (better performance)
            _defaultBuffers[i] = _device.CreateCommittedResource(
                defaultHeapProps,
                HeapFlags.None,
                bufferDesc,
                ResourceStates.CopyDest
            );

            // Map upload buffer
            unsafe
            {
                IntPtr pData;
                _uploadBuffers[i].Map(0, null, &pData);
                _mappedData[i] = pData;
            }
        }
    }

    /// <summary>
    /// Updates buffer data from an array
    /// </summary>
    public unsafe void Update(int frameIndex, T[] data, int count)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(StructuredBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        if (count > _maxElements)
            throw new ArgumentException($"Count {count} exceeds max elements {_maxElements}");

        int bytesToCopy = _elementSize * count;
        fixed (void* src = data)
        {
            Buffer.MemoryCopy(src, _mappedData![frameIndex].ToPointer(), _bufferSize, bytesToCopy);
        }
    }

    /// <summary>
    /// Updates buffer data from a span
    /// </summary>
    public unsafe void Update(int frameIndex, ReadOnlySpan<T> data)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(StructuredBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        if (data.Length > _maxElements)
            throw new ArgumentException($"Data length {data.Length} exceeds max elements {_maxElements}");

        int bytesToCopy = _elementSize * data.Length;
        fixed (void* src = data)
        {
            Buffer.MemoryCopy(src, _mappedData![frameIndex].ToPointer(), _bufferSize, bytesToCopy);
        }
    }

    /// <summary>
    /// Copies data from upload buffer to default buffer for GPU consumption
    /// Must be called on a command list before using the buffer in shaders
    /// </summary>
    public void CopyToGPU(ID3D12GraphicsCommandList commandList, int frameIndex)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(StructuredBuffer<T>));

        commandList.CopyResource(_defaultBuffers![frameIndex], _uploadBuffers![frameIndex]);
        
        // Transition to shader resource
        var barrier = new ResourceBarrier(
            new ResourceTransitionBarrier(
                _defaultBuffers[frameIndex],
                ResourceStates.CopyDest,
                ResourceStates.AllShaderResource
            )
        );
        commandList.ResourceBarrier(barrier);
    }

    /// <summary>
    /// Gets the default buffer resource for GPU reads
    /// </summary>
    public ID3D12Resource GetResource(int frameIndex)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(StructuredBuffer<T>));
        
        if (frameIndex < 0 || frameIndex >= _frameCount)
            throw new ArgumentOutOfRangeException(nameof(frameIndex));

        return _defaultBuffers![frameIndex];
    }

    /// <summary>
    /// Gets the GPU virtual address
    /// </summary>
    public ulong GetGPUVirtualAddress(int frameIndex)
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(StructuredBuffer<T>));
        
        return _defaultBuffers![frameIndex].GPUVirtualAddress;
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
                    _uploadBuffers[i].Unmap(0);
                    _uploadBuffers[i].Dispose();
                }
                _defaultBuffers?[i]?.Dispose();
            }
            _uploadBuffers = null;
            _defaultBuffers = null;
        }

        _mappedData = null;
        _disposed = true;
    }
}
