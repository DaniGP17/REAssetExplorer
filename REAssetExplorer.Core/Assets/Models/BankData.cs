using System.Collections;
using System.Runtime.InteropServices;
using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets.Models;

public class BankData : AssetData
{
    public BankChunkHeader FirstChunkHeader;
    public BankHeader Header;
    public MediaHeader[]? MediaHeaders;
    public long DataOffset;
    public uint BnkDataSize;
    public Dictionary<uint, string> StringTable = new();
    public List<HircObject> HircObjects;
}

public struct BankChunkHeader
{
    public BankChunkHeader() { }
    public BankTags Tag { get; set; }
    public uint Size { get; set; }
}

public struct BankHeader
{
    public uint Version { get; set; }
    public uint BankID { get; set; }
    public uint LanguageID { get; set; }
    public UInt16 DeviceAllocated { get; set; }
    public uint ProjectID { get; set; }
}

public struct MediaHeader
{
    public uint Id;
    public uint Offset;
    public uint Size;
    public byte[] Data;
}

public struct HircObject
{
    public HircType Type;
    public uint ID;
    public byte[] Data;
}

public enum BankTags : uint
{
    BKHD = 0x44484B42,
    DATA = 0x41544144,
    DIDX = 0x58444944,
    HIRC = 0x43524948,
    STID = 0x44495453,
    INIT = 0x54494E49,
    STMG = 0x474D5453,
    ENVS = 0x53564E45,
    PLAT = 0x54414C50
}

public enum HircType : uint
{
    State = 1,
    Sound = 2,
    Action = 3,
    Event = 4,
    RanSeqCntr = 5,
    SwitchCntr = 6,
    ActorMixer = 7,
    Bus = 8,
    LayerCntr = 9,
    Segment = 10,
    Track = 11,
    MusicSwitch = 12,
    MusicRanSeq = 13,
    Attenuation = 14,
    DialogueEvent = 15,
    FxShareSet  = 16,
    FxCustom = 17,
    AuxBus = 18,
    Lfo = 19,
    Envelope = 20,
    AudioDevice = 21,
    TimeMod = 22,
};