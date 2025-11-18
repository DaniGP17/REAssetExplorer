#ifndef REASSETEXPLORER_GAMES_RE8_H
#define REASSETEXPLORER_GAMES_RE8_H
#include "Core/IGame.h"
#include "Core/Assets/Readers/EfxReader.h"
#include "Core/Assets/Readers/FolReader.h"
#include "Core/Assets/Readers/MdfReader.h"
#include "Core/Assets/Readers/MeshReader.h"
#include "Core/Assets/Readers/MotlistReader.h"
#include "Core/Assets/Readers/MotReader.h"
#include "Core/Assets/Readers/SceneReader.h"
#include "Core/Assets/Readers/McolReader.h"
#include "Core/Assets/Readers/MsgReader.h"
#include "Core/Assets/Readers/GuiReader.h"
#include "Core/Assets/Readers/AimpReader.h"
#include "Core/Assets/Readers/LightProbeReader.h"
#include "Core/Assets/Readers/MeshLodSettingsReader.h"
#include "Core/Assets/Readers/MotbankReader.h"
#include "Core/Assets/Readers/FsmReader.h"
#include "Core/Assets/Readers/PrefabReader.h"
#include "Core/Assets/Readers/UserReader.h"
#include "Core/Assets/Readers/SdfReader.h"
#include "Core/Assets/Readers/TerrReader.h"
#include "Core/Assets/Readers/TexReader.h"
#include "Core/Assets/Readers/UvsReader.h"
#include "Core/Assets/Readers/WwiseReader.h"

class RE8 : public IGame {
public:
    std::string GetId() const override {
        return "re8";
    }

    std::string GetName() const override {
        return "Resident Evil Village";
    }


    std::vector<std::filesystem::path> GetPaksLocations() const override {
        return {
            "re_chunk_000.pak"
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
        registry.Register(std::make_unique<ProbeNetworkReader>());
        registry.Register(std::make_unique<LightProbeReader>());
        registry.Register(std::make_unique<FsmReader>(40));
        registry.Register(std::make_unique<MotlistReader>());
        registry.Register(std::make_unique<MotReader>());
        registry.Register(std::make_unique<TerrReader>());
        registry.Register(std::make_unique<FolReader>());
        registry.Register(std::make_unique<UvsReader>());
        registry.Register(std::make_unique<EfxReader>(EfxReader::Game::RE8));
        return registry;
    }

    std::string GetTexExt() const override { return ".30"; }
    std::string GetUvsExt() const override { return ".7"; }
    std::string GetEfxExt() const override { return ".2621998"; }
    std::string GetMdfExt() const override { return ".19"; }
    std::string GetMotlistExt() const override { return ".486"; }
    std::string GetMotExt() const override { return ".458"; }
    std::string GetFolExt() const override { return ".0"; }
    std::string GetTerrExt() const override { return ".10008"; }
    std::string GetMcolExt() const override { return ".10019"; }
    std::string GetAiMapExt(std::string_view type) const override {
        if (type == ".ainvm") return ".8";
        if (type == ".aiwayp") return ".3";
        if (type == ".aivspc") return ".4";
        return "";
    }
    std::string GetMeshExt() const override { return ".2101050001"; }
    std::string GetMasterExt() const override { return ".2102188797"; }
    std::string GetDefaultSkyPath() const override {
        return "natives/stm/light/sky/area09/ibl_20160419_0700_08_edit.tex.30";
    }
    // RE8 master shaders declare StructuredBuffer<InstanceWorld> with stride 112,
    // as RE7; a larger stride breaks every instance index > 0.
    uint32_t GetInstanceStride() const override { return 112; }
};

#endif
