using System;
using System.Collections.Generic;
using Vortice.Direct3D12;

namespace REAssetExplorer.RenderTest2.DX12;

/// <summary>
/// Manages descriptor heap allocation for CBV/SRV/UAV descriptors
/// </summary>
public class DescriptorHeap : IDisposable
{
    private readonly ID3D12Device _device;
    private readonly ID3D12DescriptorHeap _heap;
    private readonly DescriptorHeapType _heapType;
    private readonly int _descriptorCount;
    private readonly int _descriptorSize;
    private readonly Queue<int> _freeDescriptors;
    private readonly bool _shaderVisible;
    private bool _disposed;

    public ID3D12DescriptorHeap Heap => _heap;
    public int DescriptorSize => _descriptorSize;
    public int FreeCount => _freeDescriptors.Count;
    public int UsedCount => _descriptorCount - _freeDescriptors.Count;

    public DescriptorHeap(ID3D12Device device, DescriptorHeapType type, int descriptorCount, bool shaderVisible = true)
    {
        _device = device ?? throw new ArgumentNullException(nameof(device));
        _heapType = type;
        _descriptorCount = descriptorCount;
        _shaderVisible = shaderVisible;

        var heapDesc = new DescriptorHeapDescription
        {
            Type = type,
            DescriptorCount = descriptorCount,
            Flags = shaderVisible ? DescriptorHeapFlags.ShaderVisible : DescriptorHeapFlags.None
        };

        _heap = device.CreateDescriptorHeap(heapDesc);
        _descriptorSize = device.GetDescriptorHandleIncrementSize(type);

        // Initialize free list
        _freeDescriptors = new Queue<int>(descriptorCount);
        for (int i = 0; i < descriptorCount; i++)
        {
            _freeDescriptors.Enqueue(i);
        }
    }

    /// <summary>
    /// Allocates a descriptor handle
    /// </summary>
    public DescriptorHandle Allocate()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(DescriptorHeap));

        if (_freeDescriptors.Count == 0)
            throw new InvalidOperationException("Descriptor heap is full");

        int index = _freeDescriptors.Dequeue();
        return new DescriptorHandle(this, index);
    }

    /// <summary>
    /// Frees a previously allocated descriptor
    /// </summary>
    internal void Free(int index)
    {
        if (_disposed)
            return;

        if (index < 0 || index >= _descriptorCount)
            throw new ArgumentOutOfRangeException(nameof(index));

        _freeDescriptors.Enqueue(index);
    }

    /// <summary>
    /// Gets CPU descriptor handle at specified index
    /// </summary>
    public CpuDescriptorHandle GetCPUHandle(int index)
    {
        if (index < 0 || index >= _descriptorCount)
            throw new ArgumentOutOfRangeException(nameof(index));

        var baseHandle = _heap.GetCPUDescriptorHandleForHeapStart();
        baseHandle.Ptr += (nuint)(index * _descriptorSize);
        return baseHandle;
    }

    /// <summary>
    /// Gets GPU descriptor handle at specified index (only for shader-visible heaps)
    /// </summary>
    public GpuDescriptorHandle GetGPUHandle(int index)
    {
        if (!_shaderVisible)
            throw new InvalidOperationException("Heap is not shader-visible");

        if (index < 0 || index >= _descriptorCount)
            throw new ArgumentOutOfRangeException(nameof(index));

        var baseHandle = _heap.GetGPUDescriptorHandleForHeapStart();
        baseHandle.Ptr += (ulong)(index * _descriptorSize);
        return baseHandle;
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _heap?.Dispose();
        _disposed = true;
    }
}

/// <summary>
/// Represents an allocated descriptor handle that can be freed
/// </summary>
public class DescriptorHandle : IDisposable
{
    private DescriptorHeap? _heap;
    private int _index;
    private bool _disposed;

    public int Index => _index;
    public CpuDescriptorHandle CPUHandle => _heap?.GetCPUHandle(_index) ?? throw new ObjectDisposedException(nameof(DescriptorHandle));
    public GpuDescriptorHandle GPUHandle => _heap?.GetGPUHandle(_index) ?? throw new ObjectDisposedException(nameof(DescriptorHandle));

    internal DescriptorHandle(DescriptorHeap heap, int index)
    {
        _heap = heap;
        _index = index;
    }

    public void Dispose()
    {
        if (_disposed)
            return;

        _heap?.Free(_index);
        _heap = null;
        _disposed = true;
    }
}
