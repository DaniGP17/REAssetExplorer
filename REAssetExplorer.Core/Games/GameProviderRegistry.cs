using System.Reflection;

namespace REAssetExplorer.Core.Games;

/// <summary>
/// Central registry for game provider plugins.
/// </summary>
public static class GameProviderRegistry
{
    private const string GameProviderDllPattern = "*.Games.*.dll";
    
    private static readonly List<IGameProvider> _providers = new();
    
    /// <summary>
    /// Gets all registered game providers.
    /// </summary>
    public static IReadOnlyList<IGameProvider> Providers => _providers.AsReadOnly();

    /// <summary>
    /// Automatically discovers and registers game providers from DLL files.
    /// </summary>
    public static void AutoRegisterProviders()
    {
        LoadGameProviderAssemblies();
        var providers = DiscoverProviders();
        RegisterProviders(providers);
    }

    /// <summary>
    /// Registers a single game provider.
    /// </summary>
    /// <param name="provider">The provider to register.</param>
    /// <exception cref="ArgumentNullException">Thrown when provider is null.</exception>
    public static void Register(IGameProvider provider)
    {
        ArgumentNullException.ThrowIfNull(provider);
        
        if (!_providers.Contains(provider))
        {
            _providers.Add(provider);
        }
    }

    /// <summary>
    /// Clears all registered providers.
    /// </summary>
    public static void Clear()
    {
        _providers.Clear();
    }

    private static void LoadGameProviderAssemblies()
    {
        var dllFiles = Directory.GetFiles(AppContext.BaseDirectory, GameProviderDllPattern);
        
        foreach (var dll in dllFiles)
        {
            try
            {
                Assembly.LoadFrom(dll);
            }
            catch (Exception ex)
            {
                // Log error but continue loading other providers
                Console.WriteLine($"Failed to load assembly {dll}: {ex.Message}");
            }
        }
    }

    private static IEnumerable<IGameProvider> DiscoverProviders()
    {
        return AppDomain.CurrentDomain
            .GetAssemblies()
            .SelectMany(GetTypesFromAssembly)
            .Where(IsValidProviderType)
            .Select(CreateProviderInstance)
            .Where(p => p != null)!;
    }

    private static Type[] GetTypesFromAssembly(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException)
        {
            return Array.Empty<Type>();
        }
    }

    private static bool IsValidProviderType(Type type)
    {
        return typeof(IGameProvider).IsAssignableFrom(type) 
               && !type.IsInterface 
               && !type.IsAbstract
               && type.GetConstructor(Type.EmptyTypes) != null;
    }

    private static IGameProvider? CreateProviderInstance(Type type)
    {
        try
        {
            return (IGameProvider?)Activator.CreateInstance(type);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to create instance of {type.Name}: {ex.Message}");
            return null;
        }
    }

    private static void RegisterProviders(IEnumerable<IGameProvider> providers)
    {
        foreach (var provider in providers)
        {
            Register(provider);
        }
    }
}
