using System;
using System.Runtime.InteropServices;
using Vortice.Direct3D12;

namespace REAssetExplorer.RenderTest2.DX12;

public class DX12CommandQueue : IDisposable
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateEvent(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, string? lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint INFINITE     = 0xFFFFFFFF;
    private const int  FrameCount   = 2;

    private ID3D12CommandQueue?               _commandQueue;
    // One allocator per back-buffer slot for double-buffered rendering.
    private readonly ID3D12CommandAllocator?[] _frameAllocators = new ID3D12CommandAllocator?[FrameCount];
    // Dedicated allocator for one-off uploads (texture upload, etc.).
    private ID3D12CommandAllocator?            _uploadAllocator;
    private ID3D12GraphicsCommandList?         _commandList;
    private ID3D12Fence?                       _fence;
    private ulong                              _nextFenceValue = 1;
    // Last fence value signaled for each frame slot; used in BeginFrame to wait for previous use.
    private readonly ulong[]                   _frameFenceValues = new ulong[FrameCount];
    private IntPtr                             _fenceEvent;
    private bool                               _disposed;

    public ID3D12CommandQueue        CommandQueue    => _commandQueue    ?? throw new InvalidOperationException("CommandQueue not initialized");
    public ID3D12GraphicsCommandList CommandList     => _commandList     ?? throw new InvalidOperationException("CommandList not initialized");
    // Legacy property used by one-off GPU operations (texture uploads).
    public ID3D12CommandAllocator    CommandAllocator => _uploadAllocator ?? throw new InvalidOperationException("Upload allocator not initialized");

    public bool Initialize(ID3D12Device device)
    {
        try
        {
            _commandQueue = device.CreateCommandQueue(new CommandQueueDescription
            {
                Type  = CommandListType.Direct,
                Flags = CommandQueueFlags.None
            });
            if (_commandQueue == null)
            {
                Console.WriteLine("Failed to create command queue");
                return false;
            }

            for (int i = 0; i < FrameCount; i++)
            {
                _frameAllocators[i] = device.CreateCommandAllocator(CommandListType.Direct);
                if (_frameAllocators[i] == null)
                {
                    Console.WriteLine($"Failed to create frame command allocator {i}");
                    return false;
                }
            }

            _uploadAllocator = device.CreateCommandAllocator(CommandListType.Direct);
            if (_uploadAllocator == null)
            {
                Console.WriteLine("Failed to create upload command allocator");
                return false;
            }

            _commandList = device.CreateCommandList<ID3D12GraphicsCommandList>(CommandListType.Direct, _frameAllocators[0]!, null);
            if (_commandList == null)
            {
                Console.WriteLine("Failed to create command list");
                return false;
            }
            _commandList.Close();

            _fence = device.CreateFence(0);
            if (_fence == null)
            {
                Console.WriteLine("Failed to create fence");
                return false;
            }

            _fenceEvent = CreateEvent(IntPtr.Zero, false, false, null);
            if (_fenceEvent == IntPtr.Zero)
            {
                Console.WriteLine("Failed to create fence event");
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during command queue initialization: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Call at the start of each frame. Waits (if needed) for the previous submission that
    /// used this frame slot's allocator, then resets the allocator and command list.
    /// With FrameCount=2 this lets the CPU run one frame ahead of the GPU.
    /// </summary>
    public void BeginFrame(int frameIndex)
    {
        if (_fence == null || _commandList == null || _frameAllocators[frameIndex] == null || _fenceEvent == IntPtr.Zero)
            return;

        try
        {
            var waitValue = _frameFenceValues[frameIndex];
            if (waitValue > 0 && _fence.CompletedValue < waitValue)
            {
                _fence.SetEventOnCompletion(waitValue, _fenceEvent);
                WaitForSingleObject(_fenceEvent, INFINITE);
            }

            _frameAllocators[frameIndex]!.Reset();
            _commandList.Reset(_frameAllocators[frameIndex], null);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception in BeginFrame: {ex.Message}");
        }
    }

    /// <summary>
    /// Call after ExecuteCommandList for the given frame slot.
    /// Signals the fence so BeginFrame can know when this slot is safe to reuse.
    /// </summary>
    public void EndFrame(int frameIndex)
    {
        if (_commandQueue == null || _fence == null)
            return;

        try
        {
            _commandQueue.Signal(_fence, _nextFenceValue);
            _frameFenceValues[frameIndex] = _nextFenceValue;
            _nextFenceValue++;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception in EndFrame: {ex.Message}");
        }
    }

    /// <summary>
    /// Full GPU flush — blocks until all previously submitted work completes.
    /// Use for resize, resource uploads, and shutdown only.
    /// </summary>
    public void WaitForGPU()
    {
        if (_commandQueue == null || _fence == null || _fenceEvent == IntPtr.Zero)
            return;

        try
        {
            _commandQueue.Signal(_fence, _nextFenceValue);
            if (_fence.CompletedValue < _nextFenceValue)
            {
                _fence.SetEventOnCompletion(_nextFenceValue, _fenceEvent);
                WaitForSingleObject(_fenceEvent, INFINITE);
            }
            _nextFenceValue++;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during WaitForGPU: {ex.Message}");
        }
    }

    public void Shutdown() => Dispose();

    public void Dispose()
    {
        if (_disposed)
            return;

        WaitForGPU();

        if (_fenceEvent != IntPtr.Zero)
        {
            CloseHandle(_fenceEvent);
            _fenceEvent = IntPtr.Zero;
        }

        _fence?.Dispose();
        _commandList?.Dispose();
        for (int i = 0; i < FrameCount; i++)
        {
            _frameAllocators[i]?.Dispose();
            _frameAllocators[i] = null;
        }
        _uploadAllocator?.Dispose();
        _commandQueue?.Dispose();

        _fence           = null;
        _commandList     = null;
        _uploadAllocator = null;
        _commandQueue    = null;
        _disposed        = true;
    }
}
