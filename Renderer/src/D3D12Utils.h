#ifndef REASSETEXPLORER_D3D12UTILS_H
#define REASSETEXPLORER_D3D12UTILS_H
#include <cstdio>
#include <d3d12.h>
#include <stdexcept>

inline void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "%s (hr=0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(msg);
    }
}

inline D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

#endif
