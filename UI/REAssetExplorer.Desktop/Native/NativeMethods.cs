using System.Runtime.InteropServices;

namespace REAssetExplorer.Desktop.Native;

[StructLayout(LayoutKind.Sequential)]
public struct ViewportStats
{
    public float Fps;
    public float FrameMs;
    public uint Width;
    public uint Height;
    public uint Draws;
    public uint Instances;
    public uint Materials;
    public uint Textures;
    public uint Lights;
    public int HasScene;
    public int DeviceLost;
    public float CpuCullMs;
    public float CpuRecordMs;
    public float PresentMs;
    public float GpuWaitMs;
    public float GpuPrepassMs;
    public float GpuGBufferMs;
    public float GpuRestMs;
    public uint Drawn;
    public ulong Triangles;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ViewportBackground
{
    public int Mode;
    public fixed float Color[3];
    public fixed float Bottom[3];
    public float SkyIntensity;
    public float SkyRotation;
    public float SkyBlur;
}

[StructLayout(LayoutKind.Sequential)]
public struct SkeletonStyle
{
    public int Bones;
    public int Joints;
    public int Axes;
    public int Occlusion;
    public int Scope;
    public int Colors;
    public uint Color;
    public uint SelectedColor;
    public float Size;
    public float Opacity;
}

// Mirrors Native/include/REAssetNative.h.
internal static unsafe partial class NativeMethods
{
    private const string Library = "REAssetNative";

    [LibraryImport(Library)]
    public static partial IntPtr rae_last_error();

    [LibraryImport(Library)]
    public static partial void rae_set_log_callback(delegate* unmanaged[Cdecl]<int, byte*, void> callback);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_install_crash_handler(string crashDir, string logPath);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_write_crash_report(string reason);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr rae_game_open(string gameId, string gameDir, string assetsDir);

    [LibraryImport(Library)]
    public static partial void rae_game_close(IntPtr game);



    [LibraryImport(Library)]
    public static partial byte* rae_game_file_list(IntPtr game, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_game_outline(IntPtr game, string pakPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_game_messages(IntPtr game, string pakPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_texture_rgba(IntPtr game, string texPath, int maxSize, out int width, out int height);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr rae_game_uvs(IntPtr game, string uvsPath);

    [LibraryImport(Library)]
    public static partial IntPtr rae_gui_fonts(IntPtr game, int language);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_gui_clips(IntPtr game, string guiPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_fsm_graph(IntPtr game, string fsmPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_font_data(IntPtr game, string fontPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_game_extract(IntPtr game, string pakPath, string outputFile);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_create(IntPtr parentHwnd);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_hwnd(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_parent(IntPtr viewport, IntPtr parentHwnd);

    [LibraryImport(Library)]
    public static partial void rae_viewport_destroy(IntPtr viewport);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_mesh(IntPtr viewport, IntPtr game, string meshPath, string overrides,
                                                     int keepCamera);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_scene(IntPtr viewport, IntPtr game, string scenePath, string overrides,
                                                      int keepCamera);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_material(IntPtr viewport, IntPtr game, string mdfPath, string overrides,
                                                         int keepCamera);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_audio_play(IntPtr game, string container, uint mediaId);

    [LibraryImport(Library)]
    public static partial void rae_audio_stop();

    [LibraryImport(Library)]
    public static partial int rae_audio_status(out float position, out float duration);

    [LibraryImport(Library)]
    public static partial void rae_audio_seek(float seconds);

    [LibraryImport(Library)]
    public static partial void rae_audio_set_paused(int paused);

    [LibraryImport(Library)]
    public static partial int rae_audio_waveform([Out] float[] minMax, int columns, int maxChannels);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_audio_export_wav(IntPtr game, string container, uint mediaId, string outputFile);

    [LibraryImport(Library)]
    public static partial void rae_audio_shutdown();

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_thumbnail(IntPtr game, string pakPath, int size, byte* rgba);

    [LibraryImport(Library)]
    public static partial void rae_viewport_clear(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial IntPtr rae_game_skies(IntPtr game);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_set_sky(IntPtr viewport, IntPtr game, string? skyPath);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_background(IntPtr viewport, in ViewportBackground background);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_effect(IntPtr viewport, IntPtr game, string efxPath, string overrides,
                                                       int keepCamera);

    [LibraryImport(Library)]
    public static partial void rae_viewport_effect_set_paused(IntPtr viewport, int paused);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_texture(IntPtr viewport, IntPtr game, string texPath, int keepView);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_uvs(IntPtr viewport, IntPtr game, string uvsPath, int keepView);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_channels(IntPtr viewport, int mask);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_skeletons(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_skeleton_style(IntPtr viewport, in SkeletonStyle style);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_play_motion(IntPtr viewport, IntPtr game, string motlistPath, int motion);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_character(IntPtr viewport, IntPtr game, string parts, int editedPart,
                                                          string overrides, int keepView);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial byte* rae_character_assemblies(IntPtr game, string meshPath, out long length);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr rae_motbank_motlists(IntPtr game, string motbankPath);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_follow(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial int rae_viewport_get_follow(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_motion_set_paused(IntPtr viewport, int paused);

    [LibraryImport(Library)]
    public static partial void rae_viewport_motion_set_speed(IntPtr viewport, float speed);

    [LibraryImport(Library)]
    public static partial void rae_viewport_motion_set_loop(IntPtr viewport, int loop);

    [LibraryImport(Library)]
    public static partial void rae_viewport_motion_seek(IntPtr viewport, float frame);

    [LibraryImport(Library)]
    public static partial int rae_viewport_motion_status(IntPtr viewport, out float frame, out float frameCount,
                                                         out float frameRate, out int paused, out int drivenJoints);

    [LibraryImport(Library)]
    public static partial void rae_viewport_select_joint(IntPtr viewport, int joint);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_collision(IntPtr viewport, IntPtr game, string mcolPath, int keepView);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_aimap(IntPtr viewport, IntPtr game, string path, int keepView);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_load_movie(IntPtr viewport, IntPtr game, string movPath, int keepView);

    [LibraryImport(Library)]
    public static partial void rae_viewport_movie_set_paused(IntPtr viewport, int paused);

    [LibraryImport(Library)]
    public static partial void rae_viewport_movie_seek(IntPtr viewport, float seconds);

    [LibraryImport(Library)]
    public static partial int rae_viewport_movie_status(IntPtr viewport, out float position, out float duration,
                                                        out int paused);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_flipbook(IntPtr viewport, int playing, float fps);

    [LibraryImport(Library)]
    public static partial int rae_viewport_image_status(IntPtr viewport, out float zoom, out int pattern);

    [LibraryImport(Library)]
    public static partial void rae_viewport_effect_restart(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_effect_set_speed(IntPtr viewport, float speed);

    [LibraryImport(Library)]
    public static partial int rae_viewport_effect_status(IntPtr viewport, out float time, out int particles);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_shading(IntPtr viewport, int shading);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_grid(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_gizmos(IntPtr viewport, uint mask);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_stats_overlay(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_transform_tool(IntPtr viewport, int mode, int local);

    [LibraryImport(Library)]
    public static partial void rae_viewport_get_transform_tool(IntPtr viewport, out int mode, out int local);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_snap(IntPtr viewport, int flags, float move, float rotate, float scale);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_take_transform_changes(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_capture(IntPtr viewport, uint timeoutMs, out int width, out int height);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_show_flags(IntPtr viewport, uint mask);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_lod(IntPtr viewport, int forcedLod, int streamAll);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_view(IntPtr viewport, int view);

    [LibraryImport(Library)]
    public static partial int rae_viewport_get_view(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_camera_speed(IntPtr viewport, float speed);

    [LibraryImport(Library)]
    public static partial float rae_viewport_get_camera_speed(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_fov(IntPtr viewport, float degrees);

    [LibraryImport(Library)]
    public static partial void rae_viewport_bookmark(IntPtr viewport, int slot, int save);

    [LibraryImport(Library)]
    public static partial uint rae_viewport_get_bookmarks(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_realtime(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_render_scale(IntPtr viewport, float scale);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_temporal_aa(IntPtr viewport, int on);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_exposure(IntPtr viewport, int fixedExposure, float ev);

    [LibraryImport(Library)]
    public static partial void rae_viewport_undo(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_redo(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial uint rae_viewport_get_undo_state(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_snap_to_floor(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial void rae_viewport_set_measuring(IntPtr viewport, int on);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_game_save_edited_scenes(IntPtr game, string edits, string outDir);

    [LibraryImport(Library)]
    public static partial void rae_viewport_reset_camera(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_get_camera(IntPtr viewport);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int rae_viewport_set_camera(IntPtr viewport, string pose);

    [LibraryImport(Library)]
    public static partial void rae_viewport_get_stats(IntPtr viewport, out ViewportStats stats);

    [LibraryImport(Library)]
    public static partial void rae_viewport_select(IntPtr viewport, int lod, int submesh);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_viewport_select_objects(IntPtr viewport, string keys);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_viewport_set_hidden_objects(IntPtr viewport, string keys);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_get_collision_groups(IntPtr viewport);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_viewport_set_collision_groups(IntPtr viewport, string names);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_get_aimap_groups(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_get_light_states(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial int rae_viewport_take_hide_request(IntPtr viewport);

    [LibraryImport(Library)]
    public static partial int rae_viewport_take_gizmo_toggles(IntPtr viewport);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_viewport_set_aimap_groups(IntPtr viewport, string names);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void rae_viewport_set_material_param(IntPtr viewport, string key, float[] values, int count);

    [LibraryImport(Library)]
    public static partial uint rae_viewport_get_selection(IntPtr viewport, out int lod, out int submesh);

    [LibraryImport(Library)]
    public static partial IntPtr rae_viewport_get_selected_object(IntPtr viewport);

    public static string LastError() => Marshal.PtrToStringUTF8(rae_last_error()) ?? "unknown error";
}
