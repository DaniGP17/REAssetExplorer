using REAssetExplorer.Core.Render;

namespace REAssetExplorer.Games.RE8;

public class RE8ShaderSystemDeps : IShaderSystemDeps
{
    public IEnumerable<string> GetShaderSystemDeps()
    {
        // return empty list
        return new List<string>();
    }

    public string GameName  => "RE8";
}