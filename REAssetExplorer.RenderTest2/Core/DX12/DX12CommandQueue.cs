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

    private const uint INFINITE = 0xFFFFFFFF;
    private ID3D12CommandQueue? _commandQueue;
    private ID3D12CommandAllocator? _commandAllocator;
    private ID3D12GraphicsCommandList? _commandList;
    private ID3D12Fence? _fence;
    private ulong _fenceValue;
    private IntPtr _fenceEvent;
    private bool _disposed;

    public ID3D12CommandQueue CommandQueue => _commandQueue ?? throw new InvalidOperationException("CommandQueue not initialized");
    public ID3D12CommandAllocator CommandAllocator => _commandAllocator ?? throw new InvalidOperationException("CommandAllocator not initialized");
    public ID3D12GraphicsCommandList CommandList => _commandList ?? throw new InvalidOperationException("CommandList not initialized");

    public bool Initialize(ID3D12Device device)
    {
        try
        {
            // Create command queue
            var queueDesc = new CommandQueueDescription
            {
                Type = CommandListType.Direct,
                Flags = CommandQueueFlags.None
            };

            _commandQueue = device.CreateCommandQueue(queueDesc);
            if (_commandQueue == null)
            {
                Console.WriteLine("Failed to create command queue");
                return false;
            }

            // Create command allocator
            _commandAllocator = device.CreateCommandAllocator(CommandListType.Direct);
            if (_commandAllocator == null)
            {
                Console.WriteLine("Failed to create command allocator");
                return false;
            }

            // Create command list
            _commandList = device.CreateCommandList<ID3D12GraphicsCommandList>(CommandListType.Direct, _commandAllocator, null);
            if (_commandList == null)
            {
                Console.WriteLine("Failed to create command list");
                return false;
            }

            // Close the command list (it starts in recording state)
            _commandList.Close();

            // Create synchronization objects
            _fence = device.CreateFence(0);
            if (_fence == null)
            {
                Console.WriteLine("Failed to create fence");
                return false;
            }

            _fenceValue = 1;
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

    public void WaitForGPU()
    {
        if (_commandQueue == null || _fence == null || _fenceEvent == IntPtr.Zero)
            return;

        try
        {
            _commandQueue.Signal(_fence, _fenceValue);

            if (_fence.CompletedValue < _fenceValue)
            {
                _fence.SetEventOnCompletion(_fenceValue, _fenceEvent);
                WaitForSingleObject(_fenceEvent, INFINITE);
            }

            _fenceValue++;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Exception during WaitForGPU: {ex.Message}");
        }
    }

    public void Shutdown()
    {
        Dispose();
    }

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
        _commandAllocator?.Dispose();
        _commandQueue?.Dispose();

        _fence = null;
        _commandList = null;
        _commandAllocator = null;
        _commandQueue = null;
        _disposed = true;
    }
}
