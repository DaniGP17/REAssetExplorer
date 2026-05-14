using System.Text;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Rsz;

public class RszValueReader
{
    private static RszVec3 ReadVec3(BinaryReader br)
    {
        float x = br.ReadSingle(), y = br.ReadSingle(), z = br.ReadSingle();
        br.ReadSingle(); // w padding
        return new RszVec3(x, y, z);
    }

    private static RszVec3 ReadFloat3(BinaryReader br) =>
        new(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());

    private static RszVec2 ReadVec2(BinaryReader br)
    {
        var vec = new RszVec2(br.ReadSingle(), br.ReadSingle());
        br.BaseStream.Seek(8, SeekOrigin.Current);
        return vec;
    }

    private static RszVec4 ReadVec4(BinaryReader br) =>
        new(br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle());

    private static RszQuaternion ReadQuaternion(BinaryReader br) =>
        new(br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle());

    private static RszMat4 ReadMat4(BinaryReader br) =>
        new(br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle(),
            br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle(),
            br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle(),
            br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle());

    private static RszColor ReadColor(int packed) =>
        new((byte)(packed & 0xFF), (byte)((packed >> 8) & 0xFF),
            (byte)((packed >> 16) & 0xFF), (byte)((packed >> 24) & 0xFF));


    private static T[] ReadArray<T>(BinaryReader br, Func<BinaryReader, T> reader, int align)
    {
        uint count = br.ReadUInt32();
        var result = new T[count];

        for (int i = 0; i < count; i++)
        {
            br.Align(align);
            result[i] = reader(br);
        }

        return result;
    }
    
    private static string ReadString(BinaryReader br)
    {
        uint length = br.ReadUInt32();
        return br.ReadFixedString((int)length * 2, Encoding.Unicode);
    }
    
    public static object ReadValue(BinaryReader br, RszField field, string name)
    {
        br.Align(field.Array ? 4 : field.Align);
        //Console.WriteLine("Name: " + name + "  Field: " + field.Name + " - Position: 0x" + (br.BaseStream.Position).ToString("X"));
        
        return field.Type.ToLower() switch
        {
            "string" => field.Array
                ? ReadArray(br, ReadString, field.Align)
                : ReadString(br),

            "data" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "s32" => field.Array
                ? ReadArray(br, r => r.ReadInt32(), field.Align)
                : br.ReadInt32(),
            
            "u8" => field.Array
                ? ReadArray(br, r => r.ReadByte(), field.Align)
                : br.ReadByte(),

            "u32" => field.Array
                ? ReadArray(br, r => r.ReadUInt32(), field.Align)
                : br.ReadUInt32(),

            "f32" => field.Array
                ? ReadArray(br, r => r.ReadSingle(), field.Align)
                : br.ReadSingle(),

            "bool" => field.Array
                ? ReadArray(br, r => r.ReadByte() != 0, field.Align)
                : br.ReadByte() != 0,
            
            "vec2" => field.Array
                ? ReadArray(br, _ => ReadVec2(br), field.Align)
                : ReadVec2(br),

            "vec3" => field.Array
                ? ReadArray(br, _ => ReadVec3(br), field.Align)
                : ReadVec3(br),

            "float3" => field.Array
                ? ReadArray(br, _ => ReadFloat3(br), field.Align)
                : ReadFloat3(br),

            "float4" => field.Array
                ? ReadArray(br, _ => ReadVec4(br), field.Align)
                : ReadVec4(br),

            "quaternion" => field.Array
                ? ReadArray(br, _ => ReadQuaternion(br), field.Align)
                : ReadQuaternion(br),

            "mat4" => field.Array
                ? ReadArray(br, _ => ReadMat4(br), field.Align)
                : ReadMat4(br),

            "color" => field.Array
                ? ReadArray(br, r => ReadColor(r.ReadInt32()), field.Align)
                : ReadColor(br.ReadInt32()),

            "guid" => field.Array
                ? ReadArray(br, _ => new RszGuid(new Guid(br.ReadBytes(16))), field.Align)
                : new RszGuid(new Guid(br.ReadBytes(field.Size))),

            "object" => field.Array
                ? ReadArray(br, r => new RszObjectRef(r.ReadUInt32()), field.Align)
                : new RszObjectRef(br.ReadUInt32()),

            "userdata" => field.Array
                ? ReadArray(br, r => new RszObjectRef(r.ReadUInt32()), field.Align)
                : new RszObjectRef(br.ReadUInt32()),

            "gameobjectref" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "size" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "capsule" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "plane" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "sphere" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "obb" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            "range" => field.Array
                ? ReadArray(br, _ => br.ReadBytes(field.Size), field.Align)
                : br.ReadBytes(field.Size),

            _ => throw new NotSupportedException($"Unsupported type: {field.Type}")
        };
    }
}