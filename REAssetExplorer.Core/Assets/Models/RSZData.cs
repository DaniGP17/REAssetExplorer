using REAssetExplorer.Core.Rsz;

namespace REAssetExplorer.Core.Assets.Models;

public class RSZData
{
    public uint Version { get; set; }
    public int ObjectCount { get; set; }
    public int InstanceCount { get; set; }
    public int UserDataCount { get; set; }
    public List<int> ObjectTable { get; set; } = new();
    public List<RszInstanceInfo> Instances { get; set; } = new();
    public List<RszUserDataInfo> UserData { get; set; } = new();
    public List<RszClass> Classes { get; set; }
}

public struct RszInstanceInfo
{
    public uint TypeId { get; set; }
    public uint CRC { get; set; }
    public string Name { get; set; }
}

public struct RszUserDataInfo
{
    public uint InstanceId { get; set; }
    public uint TypeId { get; set; }
    public string Name { get; set; }
}

public struct RszClassField
{
    public string Name { get; set; }
    public string Type { get; set; }
    public object Value { get; set; }
}

public class RszClass
{
    public string Name { get; set; }
    public List<RszClassField> Fields { get; set; } = new();

    private Dictionary<string, object?>? _fieldMap;
    private Dictionary<string, object?> FieldMap =>
        _fieldMap ??= Fields.ToDictionary(f => f.Name, f => f.Value);

    public bool TryGet<T>(string name, out T? value)
    {
        if (FieldMap.TryGetValue(name, out var raw) && raw is T typed)
        {
            value = typed;
            return true;
        }
        value = default;
        return false;
    }

    public T? Get<T>(string name) => TryGet<T>(name, out var v) ? v : default;

    /// <summary>Reads a bool field, with fallback for byte/byte[] storage.</summary>
    public bool GetBool(string name)
    {
        if (TryGet<bool>(name, out var b)) return b;
        if (TryGet<byte>(name, out var by)) return by != 0;
        if (TryGet<byte[]>(name, out var arr) && arr?.Length > 0) return arr[0] != 0;
        return false;
    }

    /// <summary>Reads a float field, with fallback for byte[] storage.</summary>
    public float GetFloat(string name)
    {
        if (TryGet<float>(name, out var f)) return f;
        if (TryGet<byte[]>(name, out var arr) && arr?.Length >= 4) return BitConverter.ToSingle(arr, 0);
        return 0f;
    }

    public RszDynamic AsDynamic(RSZData ctx) => new(this, ctx);
}