using System.Text.Json;

namespace REAssetExplorer.Desktop;

public sealed class AppSettings
{
    private static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "REAssetExplorer", "settings.json");

    public string Game { get; set; } = "re8";

    // Keyed "re7"/"re8"; the command line tools read it too (GameDirectory in Explorer/GameFactory.h).
    public Dictionary<string, string> GamePaths { get; set; } = [];

    public ViewportDisplaySettings Viewport { get; set; } = new();

    public static AppSettings Current { get; } = Load();

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath)) return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath)) ?? new();
        }
        catch (Exception)
        {
        }
        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this));
        }
        catch (Exception)
        {
        }
    }
}

// Colors are display-space RGB(A) 0..1; the ints are RaeViewportBackground / RaeSkeletonStyle values.
public sealed class ViewportDisplaySettings
{
    public int Background { get; set; }
    public Dictionary<string, string> Skies { get; set; } = [];
    public double SkyIntensity { get; set; } = 1;
    public double SkyRotation { get; set; }
    public double SkyBlur { get; set; }
    public float[] BackgroundColor { get; set; } = [0.23f, 0.23f, 0.23f];
    public float[] GradientTop { get; set; } = [0.32f, 0.33f, 0.35f];
    public float[] GradientBottom { get; set; } = [0.09f, 0.09f, 0.1f];

    public int Bones { get; set; } = 1;
    public int Joints { get; set; } = 1;
    public bool JointAxes { get; set; }
    public int Occlusion { get; set; }
    public int Scope { get; set; }
    public int BoneColors { get; set; }
    public double BoneSize { get; set; } = 1;
    public double BoneOpacity { get; set; } = 1;
    public float[] BoneColor { get; set; } = [0.902f, 0.196f, 0.196f, 0.941f];
    public float[] SelectedBoneColor { get; set; } = [1, 0.784f, 0.157f, 1];
}
