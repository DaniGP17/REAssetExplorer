#ifndef REASSETEXPLORER_GAMES_RE7_H
#define REASSETEXPLORER_GAMES_RE7_H
#include "Core/IGame.h"
#include "Core/Assets/Readers/EfxReader.h"
#include "Core/Assets/Readers/MdfReader.h"
#include "Core/Assets/Readers/MeshReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Core/Assets/Readers/MotReader.h"
#include "Core/Assets/Readers/SceneReader.h"
#include "Core/Assets/Readers/McolReader.h"
#include "Core/Assets/Readers/MsgReader.h"
#include "Core/Assets/Readers/GuiReader.h"
#include "Core/Assets/Readers/AimpReader.h"
#include "Core/Assets/Readers/MeshLodSettingsReader.h"
#include "Core/Assets/Readers/MotbankReader.h"
#include "Core/Assets/Readers/PrefabReader.h"
#include "Core/Assets/Readers/UserReader.h"
#include "Core/Assets/Readers/SdfReader.h"
#include "Core/Assets/Readers/TerrReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/Readers/WwiseReader.h"
#include "Core/Assets/Readers/UvsReader.h"

class RE7 : public IGame {
public:
    std::string GetId() const override {
        return "re7";
    }

    std::string GetName() const override {
        return "Resident Evil 7 Biohazard";
    }


    std::vector<std::filesystem::path> GetPaksLocations() const override {
        return {
            "re_chunk_000.pak",
            "dlc/re_dlc_stm_529930.pak",
            "dlc/re_dlc_stm_530610.pak",
            "dlc/re_dlc_stm_530611.pak"
        };
    }

    PakVersions GetPakReaderVersion() const override {
        return PakVersions::V4;
    }

    AssetReaderRegistry CreateAssetReaders() const override {
        AssetReaderRegistry registry;
        registry.Register(std::make_unique<TexReader>());
        registry.Register(std::make_unique<BnkReader>());
        registry.Register(std::make_unique<PckReader>());
        registry.Register(std::make_unique<MdfReader>());
        registry.Register(std::make_unique<SdfReader>());
        registry.Register(std::make_unique<MeshReader>());
        registry.Register(std::make_unique<SceneReader>());
        registry.Register(std::make_unique<UserReader>());
        registry.Register(std::make_unique<PrefabReader>());
        registry.Register(std::make_unique<McolReader>());
        registry.Register(std::make_unique<MsgReader>());
        registry.Register(std::make_unique<GuiReader>());
        registry.Register(std::make_unique<AimpReader>());
        registry.Register(std::make_unique<MotbankReader>());
        registry.Register(std::make_unique<MeshLodSettingsReader>());
        registry.Register(std::make_unique<MotlistReader>());
        registry.Register(std::make_unique<MotReader>());
        registry.Register(std::make_unique<TerrReader>());
        registry.Register(std::make_unique<EfxReader>(EfxReader::Game::RE7));
        registry.Register(std::make_unique<UvsReader>());
        return registry;
    }

    std::string GetTexExt() const override { return ".35"; }
    std::string GetMdfExt() const override { return ".21"; }
    std::string GetUvsExt() const override { return ".7"; }
    std::string GetEfxExt() const override { return ".2818689"; }
    std::string GetMotlistExt() const override { return ".524"; }
    std::string GetMotExt() const override { return ".492"; }
    std::string GetTerrExt() const override { return ".13008"; }
    std::string GetMcolExt() const override { return ".13020"; }
    std::string GetAiMapExt(std::string_view type) const override { return type == ".aimap" ? ".41" : ""; }
    std::string GetMeshExt() const override { return ".220128762"; }
    std::string GetMasterExt() const override { return ".220128762"; }
    std::string GetDefaultSkyPath() const override {
        return "natives/stm/light/sky/cloudy_fisheye_03-20-2015_001_hdr_2048_2048_mirror.tex.35";
    }
    uint32_t GetInstanceStride() const override { return 112; }
};

#endif
