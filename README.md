# REAssetExplorer

**A from-scratch asset explorer, viewer for the RE Engine games *Resident Evil 7* and *Resident Evil Village (8)* (for now).**

My goal for this project was to focus on rendering that is faithful to the game, so the standout feature is the visual quality of the display.

<p align="center">
  <a href="#screenshots">Screenshots</a> ·
  <a href="#features">Features</a> ·
  <a href="#supported-formats">Supported Formats</a> ·
  <a href="#architecture">Architecture</a> ·
  <a href="#building">Building</a> ·
  <a href="#legal--asset-disclaimer">Legal</a>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20x64-0078D6">
  <img alt="Language" src="https://img.shields.io/badge/C%2B%2B-20-blue">
  <img alt="Language" src="https://img.shields.io/badge/.NET-10-512BD4">
  <img alt="License" src="https://img.shields.io/badge/license-MIT-green">
</p>

---

## Overview

REAssetExplorer opens a game's `.pak` archives directly and lets you browse, preview and inspect assets that RE Engine ships: 3D models with full skeletal skinning, terrain and collision, foliage placement, particle effects, in-game GUI screens with their fonts, localized text tables, Wwise audio banks, AI navigation maps, and the state machines that drive character behavior. All of it renders through a real-time Direct3D 12 viewport that reproduces the engine's own lighting pipeline (G-buffer, light probes, shadows, fog, HDR post-processing) rather than a generic "3D model preview."

The project is split into two layers:

- A **C++20 core** (`Core`, `Explorer`, `Renderer`, `Games`, `Native`) that parses every file format, builds scenes from the game's RSZ component data, and renders everything with Direct3D 12.
- A **C# desktop application** (`UI`, built with [Avalonia](https://avaloniaui.net/)) that hosts that native renderer inside a docked, multi-panel editor and exposes it through a small C ABI (`REAssetNative.dll`).

## Screenshots

<table>
<tr>
<td width="50%">

https://github.com/user-attachments/assets/ab3c4265-2b73-40af-84d8-24930071a443

</td>
<td width="50%">

https://github.com/user-attachments/assets/d4a7cd45-31ce-42d4-ae19-bf9ca908d3a5

</td>
</tr>
<tr>
<td width="50%">

https://github.com/user-attachments/assets/711e9b43-7d85-4796-8ef8-881979944f63

</td>
<td width="50%">

https://github.com/user-attachments/assets/c1827e3d-e851-40bb-a958-f75d7c1ab42d

</td>
</tr>
<tr>
<td width="50%">
<img width="440" alt="Captura de pantalla 2026-09-27 201018" src="https://github.com/user-attachments/assets/8dc579ef-a1a6-477e-a442-6ee2424a893e" />
</td>
<td width="50%">
<img width="440" alt="Captura de pantalla 2026-09-27 203855" src="https://github.com/user-attachments/assets/20e7beb7-dfef-4345-ae7d-496f816932d0" />
</td>
</tr>
</table>

## Features

### 3D viewport
- Real-time Direct3D 12 renderer: meshes, materials (`.mdf2`), and textures.
- **Deferred skinning**.
- Multi-part **character assemblies**, a character made of several linked meshes/prefabs is indexed and animated as one unit, exactly like in-game.
- Camera, grid, selection and gizmo tools; an interactive transform gizmo for repositioning objects in a scene.

### Animation & state machines
- Parses `.mot` / `.motlist` / `.motbank` animation clips and plays them back on the skeleton.
- Parses `motfsm2` / `fsmv2` state machines (`PlayMotion` nodes resolved against motion banks) with a visual **graph view**.

### World, scenes & streaming
- Loads `.scn` / `.pfb` / `.user` scenes and prefabs straight from their RSZ data.
- Terrain (`.terr`) and mesh collision (`.mcol`) with the game's own BVH, rendered in a dedicated collision view grouped by collider filter.
- Procedural **foliage** (`.fol`) placement, matching the engine's runtime point-baking rather than static instances.
- Scene visibility and **LOD streaming** that reproduces the game's real rules (terrain rings, `DrawOn*` radii, room culling via `EnvCullingData`).
- Parallelized scene prefetching and persistent pak handles for fast load times.

### Rendering pipeline
- **GPU-driven culling** with indirect draw dispatch.
- Deferred lighting: G-buffer, global illumination probes, shadow mapping, height fog.
- Full HDR post-process stack matching the game's own passes: histogram-based auto-exposure, TAA, soft bloom, timeline-driven LUT color grading, FXAA, CAS sharpening.
- A forward pass for glass and other transparents, including the engine's `BlurredSolid` refraction trick.
- Debug overlays for G-buffer channels, wireframe, bounds and more.

### Effects & audio
- **EFX particle system** playback: billboards, ribbons, meshes, and the game's own particle materials.
- **Wwise** audio: `.bnk` bank parsing, `.pck` sound package indexing, and full `.wem` decoding (Vorbis conversion + playback) with a waveform view.

### Text, GUI & tools
- Localized **message table** (`.msg`) browsing and editing.
- **GUI scene** (`.gui`) parser with a live preview canvas, including the engine's custom encrypted font format (`.oft`).
- AI navigation map (`AIMP`) viewer for battle/encounter scenes.
- Crash dump capture and rotating log files for diagnosing issues in the field.
- In-app thumbnail generation and movie (cutscene) playback.


## Supported formats

| Category | Extensions | Notes |
|---|---|---|
| Archives | `.pak` (v4) | RE7 and RE8, zstd/zlib compressed entries |
| Components | RSZ (embedded) | Full type database for both games |
| Textures | `.tex` | BC1–BC7 decoding |
| Materials | `.mdf2` | |
| Meshes | `.mesh`, mesh LOD settings | |
| Animation | `.mot`, `.motlist`, `.motbank` | |
| State machines | `motfsm2`, `fsmv2` | |
| Scenes / prefabs | `.scn`, `.pfb`, `.user` | |
| Terrain / collision | `.terr`, `.mcol` | |
| Foliage | `.fol` | |
| Particles | EFX (`.efx`) | Billboard, ribbon, mesh, materials |
| AI | AIMP (navigation maps) | |
| Audio | Wwise `.bnk`, `.pck`, `.wem` | |
| Text | `.msg` | Localized message tables |
| GUI | `.gui`, `.oft` (encrypted fonts) | |
| Shader binding metadata | `.sdf` | |
| Light probes | light probe data | |

## Architecture

```
Core/       File format readers, hashing, IO, RSZ parsing, pak archive access.
Explorer/   Asset indexing, scene building, LOD/visibility rules, FSM/effect/audio glue
Renderer/   Direct3D 12 renderer: device, pipelines, culling, lighting, post-process, viewport.
Games/      RE7 / RE8 game descriptors (paths, hashes, quirks) implementing a common IGame interface.
Native/     C ABI (REAssetNative.dll) bridging the C++ core to the desktop app: viewport hosting,
            gizmos, audio, movie playback, thumbnails, crash handling.
UI/         Avalonia desktop application (C#): docking layout, asset browser, inspectors, editors.
```

`Explorer` depends on `Core`, `Renderer` and `Games`; `Native` depends on `Explorer`; `UI` talks to
`Native` exclusively through the exported `rae_*` C functions, the desktop app never links against
the C++ code directly.

## Building

Windows x64 only (the renderer targets Direct3D 12 directly).

**Prerequisites**
- CMake ≥ 3.28 and a C++20 toolchain (built and tested with MinGW-w64; MSVC should also work)
- [Ninja](https://ninja-build.org/) (or any CMake generator you prefer)
- .NET 10 SDK
- A Direct3D 12–capable GPU and driver

**1. Build the native core (`REAssetNative.dll`)**

```sh
cmake -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target REAssetNative
```

This fetches zstd, zlib and nlohmann/json automatically via `FetchContent`.

**2. Build and run the desktop app**

```sh
cd UI/REAssetExplorer.Desktop
dotnet run -c Release
```

## Usage

1. Launch the app and open **Settings**, then point it at your legally owned RE7 or RE8 installation
   directory.
2. Browse the asset tree on the left; double-click a mesh, texture, scene, animation, effect, etc. to
   open it in the appropriate editor panel.
3. Scenes and prefabs open directly in the 3D viewport with real lighting, culling and LOD.

## Third-party notices

Some of the REAssetExplorer's file-format readers were ported from [REE-Lib](https://github.com/kagenocookie/RE-Engine-Lib),
its RSZ type databases come from [RE_RSZ](https://github.com/alphazolam/RE_RSZ), and its `.pak` file
lists from [REE.PAK.Tool](https://github.com/Ekey/REE.PAK.Tool). Wwise audio decoding uses
[ww2ogg](https://github.com/hcs64/ww2ogg) and [stb_vorbis](https://github.com/nothings/stb); playback
uses [miniaudio](https://github.com/mackron/miniaudio); texture decoding uses
[bcdec](https://github.com/iOrange/bcdec). The desktop app is built on
[Avalonia](https://avaloniaui.net/) and [Dock](https://github.com/wieslawsoltes/Dock).

Full license texts and per-file attribution: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## License

[MIT](LICENSE)
