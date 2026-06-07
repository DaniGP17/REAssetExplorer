add_library(REAssetNative SHARED
    include/REAssetNative.h
    src/NativeApi.cpp
    src/NativeCrash.cpp
    src/NativeViewport.h
    src/NativeViewport.cpp
    src/TransformGizmo.cpp
    src/NativeViewportTools.cpp
    src/NativeThumbnails.cpp
    src/NativeAudio.h
    src/NativeAudio.cpp
    src/NativeMovie.h
    src/NativeMovie.cpp
)

target_include_directories(REAssetNative PUBLIC include)
target_include_directories(REAssetNative PRIVATE ${CMAKE_SOURCE_DIR}/external/miniaudio)
target_link_libraries(REAssetNative PRIVATE Explorer mfplat mfreadwrite mfuuid ole32 uuid dbghelp gdi32)
set_target_properties(REAssetNative PROPERTIES PREFIX "")

if (MINGW)
    # The .NET host does not ship the MinGW runtime. The toolchain's static
    # winpthread does not link into a DLL, so that one is shipped alongside.
    target_link_options(REAssetNative PRIVATE -static-libgcc -static-libstdc++)
    add_custom_command(TARGET REAssetNative POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:REAssetNative>
    )
endif()
