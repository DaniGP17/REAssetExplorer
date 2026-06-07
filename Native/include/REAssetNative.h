#ifndef REASSETNATIVE_H
#define REASSETNATIVE_H
#include <stdint.h>

#ifdef __cplusplus
#define RAE_API extern "C" __declspec(dllexport)
#else
#define RAE_API __declspec(dllexport)
#endif

// Strings are UTF-8. Failing calls return 0 or NULL and leave a message in rae_last_error() (per
// thread). Returned strings and buffers stay valid until the next call on the same thread unless noted.

typedef struct RaeGame RaeGame;
typedef struct RaeViewport RaeViewport;

// level: 0 info, 1 warning, 2 error. Invoked from any thread.
typedef void (*RaeLogCallback)(int32_t level, const char* message);

typedef struct RaeViewportStats {
    float fps;
    float frameMs;
    uint32_t width;
    uint32_t height;
    uint32_t draws;
    uint32_t instances;
    uint32_t materials;
    uint32_t textures;
    uint32_t lights;
    int32_t hasScene;
    int32_t deviceLost;
    // Averages in ms. CPU: frustum culling, recording and submitting the frame, blocked in
    // Present (vsync), waiting for the GPU. GPU: alpha prepass, GBuffer draws, the rest.
    float cpuCullMs;
    float cpuRecordMs;
    float presentMs;
    float gpuWaitMs;
    float gpuPrepassMs;
    float gpuGBufferMs;
    float gpuRestMs;
    uint32_t drawn;  // draws that survived culling in the last frame
    uint64_t triangles;  // drawn in the last frame
} RaeViewportStats;

// Colors are display-space RGB 0..1.
typedef struct RaeViewportBackground {
    int32_t mode;        // 0 sky texture, 1 solid color, 2 vertical gradient
    float color[3];      // solid color; the gradient's top
    float bottom[3];     // the gradient's bottom
    float skyIntensity;  // 1 = the default exposure
    float skyRotation;   // degrees about the up axis
    float skyBlur;       // 0 sharp .. 1
} RaeViewportBackground;

typedef struct RaeSkeletonStyle {
    int32_t bones;          // 0 none, 1 octahedral wire, 2 octahedral solid, 3 stick
    int32_t joints;         // 0 none, 1 wire sphere, 2 solid sphere, 3 cross
    int32_t axes;           // 1 draws each joint's local axes (X red, Y green, Z blue)
    int32_t occlusion;      // 0 on top, 1 dimmed behind the scene, 2 hidden behind the scene
    int32_t scope;          // 0 all; 1 selected, 2 and its parents, 3 and its children (scenes: selected objects)
    int32_t colors;         // 0 uniform, 1 by side (L_/R_ names), 2 by hierarchy depth, 3 by skeleton
    uint32_t color;         // RGBA8, red in the low byte
    uint32_t selectedColor;
    float size;             // joint radius scale
    float opacity;
} RaeSkeletonStyle;

RAE_API const char* rae_last_error(void);
RAE_API void rae_set_log_callback(RaeLogCallback callback);
// On a crash (unhandled SEH exception, a fatal one from this DLL on a .NET thread, std::terminate, abort)
// writes <local time>.dmp and a .txt report (exception, stack with this DLL's function names, logPath) into
// crashDir. Only the first crash is written.
RAE_API void rae_install_crash_handler(const char* crashDir, const char* logPath);
// Writes the same files now with reason as the report's text, for crashes the runtime above reports.
RAE_API void rae_write_crash_report(const char* reason);

// gameId: "re7" or "re8". gameDir: the install folder (where re_chunk_000.pak is).
// assetsDir holds FileLists/ and Rsz/.
RAE_API RaeGame* rae_game_open(const char* gameId, const char* gameDir, const char* assetsDir);
RAE_API void rae_game_close(RaeGame* game);
// One "path\tuncompressedSize\n" line per unique entry (patch paks win); owned by the game.
// Unlisted entries are "Unknown_<hash>", or "Unknown_<hash>.msg.17" when the data identifies them.
RAE_API const char* rae_game_file_list(RaeGame* game, int64_t* length);
// Top-down RGBA8 of the first mip no larger than maxSize (0: mip 0).
// texPath: pak path or source path ("UI/x/y.tex").
RAE_API const uint8_t* rae_texture_rgba(RaeGame* game, const char* texPath, int32_t maxSize, int32_t* width,
                                        int32_t* height);
// .uvs (pak or source path) as lines: "T path" per texture, "S first count" per sequence,
// "P texture left top right bottom" per pattern (uv 0..1).
RAE_API const char* rae_game_uvs(RaeGame* game, const char* uvsPath);
// Format: GuiClipTable in Explorer/AssetOutline.h.
RAE_API const char* rae_gui_clips(RaeGame* game, const char* guiPath, int64_t* length);
// .motfsm2 / .fsmv2; format: FsmGraph in Explorer/AssetOutline.h.
RAE_API const char* rae_fsm_graph(RaeGame* game, const char* fsmPath, int64_t* length);
// language: engine id (1 English). One "slot<TAB>fontPath<TAB>offsetX<TAB>offsetY<TAB>scaleX<TAB>scaleY"
// line per font, each slot's first font first. Slot N is Text FontSlot "SlotN".
RAE_API const char* rae_gui_fonts(RaeGame* game, int32_t language);
// .oft (pak or source path) decrypted to OpenType/TrueType bytes.
RAE_API const uint8_t* rae_font_data(RaeGame* game, const char* fontPath, int64_t* length);
// Format: OutlineAsset in Explorer/AssetOutline.h.
RAE_API const char* rae_game_outline(RaeGame* game, const char* pakPath, int64_t* length);
// Writes the file as stored in the paks (decompressed).
RAE_API int32_t rae_game_extract(RaeGame* game, const char* pakPath, const char* outputFile);
// .msg as a table; format: MessageTable in Explorer/AssetOutline.h.
RAE_API const char* rae_game_messages(RaeGame* game, const char* pakPath, int64_t* length);
// Blocks while a background thread renders it. rgba holds size * size * 4 bytes (RGBA8).
// Meshes, textures and materials; "file.mdf2.N|MaterialName" previews one material.
RAE_API int32_t rae_thumbnail(RaeGame* game, const char* pakPath, int32_t size, uint8_t* rgba);

// container: pak path of a .bnk, .pck or .wem; mediaId: the .wem id inside a .bnk/.pck. Media a
// bank only prefetches are read from its streaming package. Blocks while decoding.
RAE_API int32_t rae_audio_play(RaeGame* game, const char* container, uint32_t mediaId);
RAE_API void rae_audio_stop(void);
// 0 stopped or ended, 1 playing, 2 paused. The last sound played stays loaded for seeking.
RAE_API int32_t rae_audio_status(float* position, float* duration);
// Plays from there, also after the end or a stop; a pause is kept.
RAE_API void rae_audio_seek(float seconds);
RAE_API void rae_audio_set_paused(int32_t paused);
// minMax: columns * maxChannels * 2 floats, per channel then column, min then max (-1..1).
// Returns the channels written.
RAE_API int32_t rae_audio_waveform(float* minMax, int32_t columns, int32_t maxChannels);
// 16-bit PCM WAV.
RAE_API int32_t rae_audio_export_wav(RaeGame* game, const char* container, uint32_t mediaId, const char* outputFile);
// Call before the process exits.
RAE_API void rae_audio_shutdown(void);

// Child window of parentHwnd (NULL parks it) with its own render thread.
RAE_API RaeViewport* rae_viewport_create(void* parentHwnd);
RAE_API void* rae_viewport_hwnd(RaeViewport* viewport);
// NULL parks the window and pauses rendering.
RAE_API void rae_viewport_set_parent(RaeViewport* viewport, void* parentHwnd);
RAE_API void rae_viewport_destroy(RaeViewport* viewport);
// Blocking: parses on the calling thread, uploads on the render thread. overrides (may be NULL):
// "key\tpak path" lines replacing asset references, keyed by outline override keys.
RAE_API int32_t rae_viewport_load_mesh(RaeViewport* viewport, RaeGame* game, const char* meshPath,
                                       const char* overrides, int32_t keepCamera);
RAE_API int32_t rae_viewport_load_scene(RaeViewport* viewport, RaeGame* game, const char* scenePath,
                                        const char* overrides, int32_t keepCamera);
// A sphere per material; rae_viewport_select with lod i shows material i.
RAE_API int32_t rae_viewport_load_material(RaeViewport* viewport, RaeGame* game, const char* mdfPath,
                                           const char* overrides, int32_t keepCamera);
// Only Billboard3D emitters are simulated; fails without any. Edits come as "param:" override
// lines (see rae_viewport_set_material_param).
RAE_API int32_t rae_viewport_load_effect(RaeViewport* viewport, RaeGame* game, const char* efxPath,
                                         const char* overrides, int32_t keepCamera);
RAE_API void rae_viewport_effect_set_paused(RaeViewport* viewport, int32_t paused);
// rae_viewport_select picks what is shown: for a texture lod = image, submesh = mip (-1 = auto);
// for a .uvs lod = sequence, submesh = pattern (global index), also picked by clicks.
// A .rtex shows a placeholder of its size.
RAE_API int32_t rae_viewport_load_texture(RaeViewport* viewport, RaeGame* game, const char* texPath, int32_t keepView);
RAE_API int32_t rae_viewport_load_uvs(RaeViewport* viewport, RaeGame* game, const char* uvsPath, int32_t keepView);
// Drawn over its render mesh (found by name). Hidden object keys: "rendermesh", "layer:<i>".
RAE_API int32_t rae_viewport_load_collision(RaeViewport* viewport, RaeGame* game, const char* mcolPath, int32_t keepView);
// .aimap, .ainvm, .aiwayp or .aivspc. Hidden object keys: "aigroup:<main|secondary>:<i>",
// "ailinks:<main|secondary>", "ailayer:<i|none>".
RAE_API int32_t rae_viewport_load_aimap(RaeViewport* viewport, RaeGame* game, const char* path, int32_t keepView);
// parts: lines as CharacterPartsText writes them (Explorer/CharacterIndex.h). editedPart gets
// every LOD and the submesh selection. Hidden object keys: "part:<i>".
RAE_API int32_t rae_viewport_load_character(RaeViewport* viewport, RaeGame* game, const char* parts, int32_t editedPart,
                                            const char* overrides, int32_t keepCamera);
// Assemblies using the mesh, most used first: a "C  index  name  source  motbank  jointMap  uses"
// line, then its parts as CharacterPartsText writes them. The first call per game builds an
// index (seconds) unless cached.
RAE_API const char* rae_character_assemblies(RaeGame* game, const char* meshPath, int64_t* length);
// motbankPath: pak path or as Motion components store it. One "bankId<TAB>motlist pak path" line each.
RAE_API const char* rae_motbank_motlists(RaeGame* game, const char* motbankPath);
// Starts playing, with its audio.
RAE_API int32_t rae_viewport_load_movie(RaeViewport* viewport, RaeGame* game, const char* movPath, int32_t keepView);
// Play at the end starts over.
RAE_API void rae_viewport_movie_set_paused(RaeViewport* viewport, int32_t paused);
RAE_API void rae_viewport_movie_seek(RaeViewport* viewport, float seconds);
// 0 unless a movie is loaded.
RAE_API int32_t rae_viewport_movie_status(RaeViewport* viewport, float* position, float* duration, int32_t* paused);
// Bit 0 R, 1 G, 2 B, 3 A (blended over a checkerboard).
RAE_API void rae_viewport_set_channels(RaeViewport* viewport, int32_t mask);
// Plays the selected .uvs sequence.
RAE_API void rae_viewport_set_flipbook(RaeViewport* viewport, int32_t playing, float fps);
// 0 unless an image is loaded; zoom is frame pixels per texel, pattern the one shown or selected.
RAE_API int32_t rae_viewport_image_status(RaeViewport* viewport, float* zoom, int32_t* pattern);
RAE_API void rae_viewport_effect_restart(RaeViewport* viewport);
RAE_API void rae_viewport_effect_set_speed(RaeViewport* viewport, float speed);
// 0 without an effect; time is since the last restart.
RAE_API int32_t rae_viewport_effect_status(RaeViewport* viewport, float* time, int32_t* particles);
// Texture paths of the game's skies, the default first; one per line.
RAE_API const char* rae_game_skies(RaeGame* game);
// Shown while empty and behind every load. skyPath: one of rae_game_skies, NULL or "" for the
// default. Each sky is decoded once per game.
RAE_API int32_t rae_viewport_set_sky(RaeViewport* viewport, RaeGame* game, const char* skyPath);
// Texture and movie previews keep their own backdrop.
RAE_API void rae_viewport_set_background(RaeViewport* viewport, const RaeViewportBackground* background);
RAE_API void rae_viewport_clear(RaeViewport* viewport);
// 0 lit, 1 unlit, 2 wireframe, 3 collision (scene colliders), 4 AI map (scene AI maps).
RAE_API void rae_viewport_set_shading(RaeViewport* viewport, int32_t shading);
RAE_API void rae_viewport_set_grid(RaeViewport* viewport, int32_t on);
// Bit 31 shows gizmos at all (the transform widget and the grid included); bits 0-2 point, spot and directional
// light icons, bit 3 the selection outline.
RAE_API void rae_viewport_set_gizmos(RaeViewport* viewport, uint32_t mask);
// The transform widget: mode 0 move, 1 rotate, 2 scale; local 1 aligns move and rotate with the object.
// W/E/R, Space and Ctrl+` change it from the viewport too; the getter reports the current tool.
// Writes the scenes the edits touch under outDir (natives/... kept), with their via.Transform values patched.
// edits: a line per Transform parameter, "param:xform:<object key>#field<TAB>x,y,z". Returns the files
// written, -1 on error (rae_last_error).
RAE_API int32_t rae_game_save_edited_scenes(RaeGame* game, const char* edits, const char* outDir);
// Show menu, ViewportShow bits: 1 static meshes, 2 skinned meshes, 4 decals, 8 effects, 16 fog, 32 volumetric fog,
// 64 shadows, 128 post-process, 256 sky, 512 local cubemaps.
RAE_API void rae_viewport_set_show_flags(RaeViewport* viewport, uint32_t mask);
// forcedLod -1 lets distance choose; streamAll 1 draws every zone and room whatever the camera.
RAE_API void rae_viewport_set_lod(RaeViewport* viewport, int32_t forcedLod, int32_t streamAll);
// 0 perspective, 1 top, 2 bottom, 3 left, 4 right, 5 front, 6 back.
RAE_API void rae_viewport_set_view(RaeViewport* viewport, int32_t view);
RAE_API int32_t rae_viewport_get_view(RaeViewport* viewport);
// Fly speed in meters per second.
RAE_API void rae_viewport_set_camera_speed(RaeViewport* viewport, float speed);
RAE_API float rae_viewport_get_camera_speed(RaeViewport* viewport);
RAE_API void rae_viewport_set_fov(RaeViewport* viewport, float degrees);
// Slots 0-9; save 1 stores the camera, 0 goes to it. The mask has bit i per saved slot.
RAE_API void rae_viewport_bookmark(RaeViewport* viewport, int32_t slot, int32_t save);
RAE_API uint32_t rae_viewport_get_bookmarks(RaeViewport* viewport);
// Off renders only after input or changes.
RAE_API void rae_viewport_set_realtime(RaeViewport* viewport, int32_t on);
// Fraction of the window the frame is rendered at (0.25 - 2).
RAE_API void rae_viewport_set_render_scale(RaeViewport* viewport, float scale);
RAE_API void rae_viewport_set_temporal_aa(RaeViewport* viewport, int32_t on);
// fixed 0 keeps the scene's exposure; 1 uses ev instead, auto exposure off.
RAE_API void rae_viewport_set_exposure(RaeViewport* viewport, int32_t fixed, float ev);
RAE_API void rae_viewport_undo(RaeViewport* viewport);
RAE_API void rae_viewport_redo(RaeViewport* viewport);
// Bit 0 something to undo, bit 1 to redo.
RAE_API uint32_t rae_viewport_get_undo_state(RaeViewport* viewport);
// Drops the selected objects onto the mesh below them (End in the viewport).
RAE_API void rae_viewport_snap_to_floor(RaeViewport* viewport);
// On, a left drag measures between two surface points.
RAE_API void rae_viewport_set_measuring(RaeViewport* viewport, int32_t on);
RAE_API void rae_viewport_set_transform_tool(RaeViewport* viewport, int32_t mode, int32_t local);
RAE_API void rae_viewport_get_transform_tool(RaeViewport* viewport, int32_t* mode, int32_t* local);
// flags: bit 0 move, 1 rotate, 2 scale snapping; steps in meters, degrees and scale units.
RAE_API void rae_viewport_set_snap(RaeViewport* viewport, int32_t flags, float move, float rotate, float scale);
// Objects the widget moved since the last call, a line each: key, position, rotation (XYZ euler degrees),
// scale, tab-separated with "x,y,z" values.
RAE_API const char* rae_viewport_take_transform_changes(RaeViewport* viewport);
// FPS, frame and GPU/CPU times, draws and triangles drawn over the viewport's corner.
RAE_API void rae_viewport_set_stats_overlay(RaeViewport* viewport, int32_t on);
// Game shaders' EnvironmentInfo, to reproduce a captured frame: timeMs / frame < 0 keep the viewer's clock and
// frame counter; userGlobalParams is floatCount floats (up to 128) of float4s, the rest zero.
// NDC offset added to every projected position, as a captured frame's TAA jitter.
RAE_API void rae_viewport_set_projection_jitter(RaeViewport* viewport, float x, float y);
RAE_API void rae_viewport_set_environment(RaeViewport* viewport, int64_t timeMs, int64_t frame, const float* userGlobalParams,
                                          int32_t floatCount);
RAE_API void rae_viewport_set_skeletons(RaeViewport* viewport, int32_t on);
RAE_API void rae_viewport_set_skeleton_style(RaeViewport* viewport, const RaeSkeletonStyle* style);
// Plays on every loaded skinned mesh, joints matched by name; an empty path or motion -1
// restores the bind pose. The last motlist is cached.
RAE_API int32_t rae_viewport_play_motion(RaeViewport* viewport, RaeGame* game, const char* motlistPath, int32_t motion);
// The camera follows the animated hips; on by default. Flying the camera turns it off (see
// rae_viewport_get_follow).
RAE_API void rae_viewport_set_follow(RaeViewport* viewport, int32_t on);
RAE_API int32_t rae_viewport_get_follow(RaeViewport* viewport);
// Play after the end of a motion that does not loop starts it over.
RAE_API void rae_viewport_motion_set_paused(RaeViewport* viewport, int32_t paused);
RAE_API void rae_viewport_motion_set_speed(RaeViewport* viewport, float speed);
RAE_API void rae_viewport_motion_set_loop(RaeViewport* viewport, int32_t loop);
RAE_API void rae_viewport_motion_seek(RaeViewport* viewport, float frame);
// 0 without a motion. drivenJoints counts the joints the motion animates (0: other skeleton).
RAE_API int32_t rae_viewport_motion_status(RaeViewport* viewport, float* frame, float* frameCount, float* frameRate,
                                           int32_t* paused, int32_t* drivenJoints);
// Mesh assets: joint index to highlight, -1 for none.
RAE_API void rae_viewport_select_joint(RaeViewport* viewport, int32_t joint);
// Frames the selection, or the whole asset without one.
RAE_API void rae_viewport_reset_camera(RaeViewport* viewport);
// "x y z yaw pitch fov": world position, then degrees (yaw 0 looks down +Z, pitch up from the horizon).
RAE_API const char* rae_viewport_get_camera(RaeViewport* viewport);
// Same format, plus an optional roll in degrees; 0 when the text does not parse.
RAE_API int32_t rae_viewport_set_camera(RaeViewport* viewport, const char* pose);
// Blocks until a frame begun after the call is presented. Top-down RGBA8, valid until the next
// call on this thread; NULL on timeout.
RAE_API const uint8_t* rae_viewport_capture(RaeViewport* viewport, uint32_t timeoutMs, int32_t* width, int32_t* height);
typedef struct RaeTargetImage {
    const char* name;
    int32_t width;
    int32_t height;
    int32_t channels;
    const float* pixels;
} RaeTargetImage;
// Float copies of the intermediate targets of a frame begun after the call, top-down, channels
// interleaved: gb0..gb3, gid and gis (decoded), depth, lit (HDR after lighting), fog (HDR after the
// fog, before transparents) and final (the displayed image, 0..1). exposure is the factor applied
// before the tone map. Valid until the next call on this thread; returns the count, 0 on timeout.
RAE_API int32_t rae_viewport_capture_targets(RaeViewport* viewport, uint32_t timeoutMs, const RaeTargetImage** images,
                                             float* exposure);
RAE_API void rae_viewport_get_stats(RaeViewport* viewport, RaeViewportStats* stats);
// Mesh assets: lod < 0 keeps the displayed LOD; submesh < 0 clears the selection.
RAE_API void rae_viewport_select(RaeViewport* viewport, int32_t lod, int32_t submesh);
// Scenes: newline-separated outline keys; NULL or "" clears the selection.
RAE_API void rae_viewport_select_objects(RaeViewport* viewport, const char* keys);
// key: override key of a "Parameters" outline property. Not kept across loads: pass the same key
// and values as an override too.
RAE_API void rae_viewport_set_material_param(RaeViewport* viewport, const char* key, const float* values, int32_t count);
// Scenes: newline-separated outline keys; replaces the previous set ("" shows all). Reset by every load.
RAE_API void rae_viewport_set_hidden_objects(RaeViewport* viewport, const char* keys);
// One "name<TAB>colliders<TAB>RRGGBB<TAB>volume" line per collision filter group of the scene
// (volume 1: drawn see-through); "" without colliders.
RAE_API const char* rae_viewport_get_collision_groups(RaeViewport* viewport);
// Newline-separated group names. Kept across loads; until the first call every group is drawn.
RAE_API void rae_viewport_set_collision_groups(RaeViewport* viewport, const char* names);
// One "name<TAB>type<TAB>nodes<TAB>RRGGBB" line per AI map of the scene; "" without maps.
RAE_API const char* rae_viewport_get_aimap_groups(RaeViewport* viewport);
// Newline-separated: the loaded light environment state, then every state the scene offers;
// "" without any. Load another by passing it as the "lightstate" override.
RAE_API const char* rae_viewport_get_light_states(RaeViewport* viewport);
// 1 when H was pressed over the viewport since the last call.
RAE_API int32_t rae_viewport_take_hide_request(RaeViewport* viewport);
// Times G was pressed in the viewport since the last call.
RAE_API int32_t rae_viewport_take_gizmo_toggles(RaeViewport* viewport);
// Newline-separated map names. Kept across loads; until the first call every map is drawn.
RAE_API void rae_viewport_set_aimap_groups(RaeViewport* viewport, const char* names);
// Displayed LOD and selected submesh (-1 for none). The returned counter changes whenever a click
// changed the selection.
RAE_API uint32_t rae_viewport_get_selection(RaeViewport* viewport, int32_t* lod, int32_t* submesh);
// Outline key of the scene object picked by the last click, "" if none.
RAE_API const char* rae_viewport_get_selected_object(RaeViewport* viewport);
// Draw hit by the last scene click: "owner\tmesh\tmdf|material|master\tpipeline\tdraw".
RAE_API const char* rae_viewport_get_picked_draw(RaeViewport* viewport);

#endif
