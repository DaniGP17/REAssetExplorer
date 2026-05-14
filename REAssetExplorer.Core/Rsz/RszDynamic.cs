using System.Dynamic;
using REAssetExplorer.Core.Assets.Models;

namespace REAssetExplorer.Core.Rsz;

/// <summary>
/// Dynamic wrapper around <see cref="RszClass"/> that exposes fields as properties.
/// Object-reference fields (<see cref="RszObjectRef"/>) are automatically followed
/// and returned as nested <see cref="RszDynamic"/> instances.
/// </summary>
public class RszDynamic : DynamicObject
{
    private readonly RszClass _class;
    private readonly RSZData _ctx;

    public RszDynamic(RszClass rszClass, RSZData ctx)
    {
        _class = rszClass;
        _ctx = ctx;
    }

    public string TypeName => _class.Name;

    public override bool TryGetMember(GetMemberBinder binder, out object? result)
    {
        if (!_class.TryGet<object>(binder.Name, out var value))
        {
            result = null;
            return false;
        }

        if (value is RszObjectRef objRef)
        {
            var idx = (int)objRef.InstanceIndex;
            result = idx > 0 && idx < _ctx.Classes.Count && _ctx.Classes[idx] != null
                ? new RszDynamic(_ctx.Classes[idx], _ctx)
                : null;
            return true;
        }

        result = value;
        return true;
    }

    public override IEnumerable<string> GetDynamicMemberNames()
        => _class.Fields.Select(f => f.Name);

    public override string ToString() => $"RszDynamic({_class.Name})";
}
