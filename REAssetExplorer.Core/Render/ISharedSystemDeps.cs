using System.Collections.Generic;

namespace REAssetExplorer.Core.Render;

public interface IShaderSystemDeps
{
    IEnumerable<string> GetShaderSystemDeps();
    
    string GameName { get; }
}
