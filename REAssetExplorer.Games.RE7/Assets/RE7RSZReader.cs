using System.Text;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Rsz;

namespace REAssetExplorer.Games.RE7.Assets;

public class RE7RSZReader : IAssetReader<RSZData>
{
    public AssetType AssetType => AssetType.Scene;
    
    private static readonly HashSet<string> _extensions = new(StringComparer.OrdinalIgnoreCase)
    {
        
    };

    /// <inheritdoc/>
    public IReadOnlySet<string> SupportedExtensions => _extensions;

    /// <inheritdoc/>
    public bool CanRead(string fileName, ReadOnlySpan<byte> header)
    {
        if (header[0] != 'R' || header[1] != 'S' || header[2] != 'Z')
        {
            return false;
        }

        return true;
    }
    
    public Result<RSZData> Read(ReadOnlySpan<byte> data, string fileName) => Read(data, fileName, null);

    public Result<RSZData> Read(ReadOnlySpan<byte> data, string fileName, BinaryReader? orgBr = null)
    {
         try
        {
            var rsz = new RSZData();
            using var br = orgBr ?? new BinaryReader(new MemoryStream(data.ToArray()));
            int startPosition = (int)br.BaseStream.Position;
            
            br.ReadBytes(4);
            rsz.Version = br.ReadUInt32();
            rsz.ObjectCount = br.ReadInt32();
            rsz.InstanceCount = br.ReadInt32();
            rsz.UserDataCount = (int)br.ReadInt64();
            
            ulong instanceOffset = br.ReadUInt64();
            ulong dataOffset = br.ReadUInt64();
            ulong userDataOffset = br.ReadUInt64();
            
            for (int i = 0; i < rsz.ObjectCount; i++)
            {
                rsz.ObjectTable.Add(br.ReadInt32());
            }
            
            br.BaseStream.Seek((long)instanceOffset + startPosition, SeekOrigin.Begin);
            for (int i = 0; i < rsz.InstanceCount; i++)
            {
                var instance = new RszInstanceInfo
                {
                    TypeId = br.ReadUInt32(),
                    CRC = br.ReadUInt32(),
                };
                instance.Name = RszRegistry.Current.GetByHash(instance.TypeId)?.Name ?? "NULL";
                if (instance.Name == "")
                {
                    instance.Name = "NULL";
                }
                rsz.Instances.Add(instance);
            }
            
            br.BaseStream.Seek((long)userDataOffset + startPosition, SeekOrigin.Begin);
            for (int i = 0; i < rsz.UserDataCount; i++)
            {
                var userData = new RszUserDataInfo
                {
                    InstanceId = br.ReadUInt32(),
                    TypeId = br.ReadUInt32(),
                    Name = br.ReadNullTerminatedString(br.ReadInt64(), Encoding.Unicode)
                };
                rsz.UserData.Add(userData);
            }
            
            var userDataInstanceIds = new HashSet<uint>(rsz.UserData.Select(ud => ud.InstanceId));
            
            br.BaseStream.Seek((long)dataOffset + startPosition, SeekOrigin.Begin);
            rsz.Classes = new List<RszClass>(new RszClass[rsz.InstanceCount]);
            for (int i = 0; i < rsz.InstanceCount; i++)
            {
                if (rsz.Instances[i].Name == "NULL")
                {
                    continue;
                }

                try
                {
                    var isUserData = userDataInstanceIds.Contains((uint)i);
                    var rszClass = new RszClass
                    {
                        Name = rsz.Instances[i].Name,
                        Fields = isUserData 
                            ? new List<RszClassField>()
                            : RszRegistry.Current.GetFieldsByHash(rsz.Instances[i].TypeId)
                                  ?.Select(f => new RszClassField
                                  {
                                      Name = f.Name,
                                      Type = f.Type,
                                      Value = RszValueReader.ReadValue(br, f, rsz.Instances[i].Name)
                                  }).ToList() 
                              ?? new List<RszClassField>()
                    };
                    rsz.Classes.Insert(i, rszClass);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Failed to read RSZ instance {i} (TypeId: {rsz.Instances[i].TypeId}, Name: {rsz.Instances[i].Name}): {ex}");
                }
                
                if (rsz.Instances[i].Name.Equals("app.WwiseContainerApp"))
                {
                    //break;
                }
            }
            return Result<RSZData>.Success(rsz);
        }
        catch (Exception ex)
        {
            Console.WriteLine("MALL");
            return Result<RSZData>.Failure($"Failed to read rsz '{fileName}': {ex.Message}", ex);
        }
    }
    
    public bool ResolveDependencies(RSZData asset) => true;
}