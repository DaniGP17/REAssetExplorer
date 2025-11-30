add_library(Renderer STATIC
    include/Renderer/Camera.h
    include/Renderer/RenderMath.h
    include/Renderer/RenderTypes.h
    include/Renderer/Viewer.h
    src/Camera.cpp
    src/D3D12Utils.h
    src/ParallelFor.h
    src/DxilBindings.h
    src/GameBinds.h
    src/ViewerDevice.cpp
    src/ViewerResources.cpp
    src/ViewerPipelines.cpp
    src/ViewerFrame.cpp
    src/ViewerCulling.cpp
    src/ViewerExposure.cpp
    src/ViewerTargets.cpp
    src/ViewerIndirect.cpp
    src/ViewerCacao.cpp
    src/ViewerShadow.cpp
    src/ViewerFog.cpp
    src/ViewerPost.cpp
    src/ViewerParticles.cpp
    src/ViewerMaterialParticles.cpp
    src/ViewerForward.cpp
    src/ViewerDebug.cpp
    src/ViewerCollision.cpp
    src/ViewerGizmos.cpp
    src/ViewerDebugViews.cpp
    src/ViewerText.cpp
    src/ViewerGrid.cpp
    src/ViewerSelection.cpp
    src/ViewerPreview.cpp
)

target_include_directories(Renderer PUBLIC include)
target_compile_definitions(Renderer PUBLIC UNICODE _UNICODE)
target_link_libraries(Renderer PUBLIC d3d12 dxgi dxguid d3dcompiler)
