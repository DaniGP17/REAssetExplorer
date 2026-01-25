using Xunit;
using Xunit.Abstractions;
using Xunit.Sdk;

namespace REAssetExplorer.Testing.TestHelpers;

public class SkipIfRE7NotInstalledAttribute : FactAttribute
{
    public SkipIfRE7NotInstalledAttribute()
    {
        if (!TestDataPaths.IsRE7Installed())
        {
            Skip = "RE7 not installed.";
        }
    }
}

public class SkipIfRE8NotInstalledAttribute : FactAttribute
{
    public SkipIfRE8NotInstalledAttribute()
    {
        if (!TestDataPaths.IsRE8Installed())
        {
            Skip = "RE8 not installed.";
        }
    }
}

[TraitDiscoverer("REAssetExplorer.Testing.TestHelpers.CategoryDiscoverer", "REAssetExplorer.Testing")]
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)]
public class SlowTestAttribute : Attribute, ITraitAttribute
{
}

[TraitDiscoverer("REAssetExplorer.Testing.TestHelpers.CategoryDiscoverer", "REAssetExplorer.Testing")]
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Method, AllowMultiple = true)]
public class IntegrationTestAttribute : Attribute, ITraitAttribute
{
}

public class CategoryDiscoverer : ITraitDiscoverer
{
    public IEnumerable<KeyValuePair<string, string>> GetTraits(IAttributeInfo traitAttribute)
    {
        var name = traitAttribute.GetType().Name;
        var categoryName = name.Replace("Attribute", "").Replace("Test", "");
        yield return new KeyValuePair<string, string>("Category", categoryName);
    }
}
