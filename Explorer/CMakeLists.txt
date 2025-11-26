add_library(Explorer STATIC
    include/Explorer/AssetOutline.h
    include/Explorer/AsyncLoads.h
    include/Explorer/CharacterIndex.h
    include/Explorer/EffectParams.h
    include/Explorer/EffectSimulator.h
    include/Explorer/FsmMotions.h
    include/Explorer/GameFactory.h
    include/Explorer/GuiFonts.h
    include/Explorer/Log.h
    include/Explorer/MessageIndex.h
    include/Explorer/SceneAiMap.h
    include/Explorer/SceneBuilder.h
    include/Explorer/SceneCollision.h
    include/Explorer/SceneLod.h
    include/Explorer/SceneUpload.h
    include/Explorer/SceneVisibility.h
    include/Explorer/SkeletalAnimator.h
    include/Explorer/WemLocator.h
    src/AssetOutline.cpp
    src/AsyncLoads.cpp
    src/CharacterIndex.cpp
    src/EffectParams.cpp
    src/EffectSimulator.cpp
    src/FsmMotions.cpp
    src/GuiFonts.cpp
    src/Log.cpp
    src/MessageIndex.cpp
    src/SceneAiMap.cpp
    src/SceneBuilder.cpp
    src/SceneCollision.cpp
    src/SceneSave.cpp
    src/LightComponents.cpp
    src/SceneLod.cpp
    src/PostProcessZones.cpp
    src/SceneUpload.cpp
    src/SceneVisibility.cpp
    src/SkeletalAnimator.cpp
    src/WemLocator.cpp
)

target_include_directories(Explorer PUBLIC include)
target_link_libraries(Explorer PUBLIC Core Renderer Games)
