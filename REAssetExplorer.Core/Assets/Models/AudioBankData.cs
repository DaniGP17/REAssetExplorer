namespace REAssetExplorer.Core.Assets.Models;

public class AudioBankData
{
    public uint SoundBankId { get; set; }
    public Language LanguageId { get; set; }
    public BnkChunkInfo[] ChunksArray { get; set; } = Array.Empty<BnkChunkInfo>();
    public BnkDidxEntry[] DidxEntries { get; set; } = Array.Empty<BnkDidxEntry>();
    public BnkHircObject[] HircObjects { get; set; } = Array.Empty<BnkHircObject>();
    public byte[] DataChunk { get; set; } = Array.Empty<byte>();
    public bool HasMedia => DataChunk.Length > 0;
}

public class BnkChunkInfo
{
    public uint Tag { get; set; }
    public uint Size { get; set; }
    public long Offset { get; set; }
}

public class BnkDidxEntry
{
    public uint MediaId { get; set; }
    public uint Offset { get; set; }
    public uint Size { get; set; }
}

public class BnkHircObject
{
    public byte Type { get; set; }
    public uint Id { get; set; }
    public byte[] Data { get; set; } = Array.Empty<byte>();
}

public enum Language : uint
{
    LanguageJapanese            = 0x0,
    LanguageEnglish             = 0x1,
    LanguageFrench              = 0x2,
    LanguageItalian             = 0x3,
    LanguageGerman              = 0x4,
    LanguageSpanish             = 0x5,
    LanguageRussian             = 0x6,
    LanguagePolish              = 0x7,
    LanguageDutch               = 0x8,
    LanguagePortuguese          = 0x9,
    LanguagePortugueseBr        = 0xA,
    LanguageKorean              = 0xB,
    LanguageTransitionalChinese = 0xC,
    LanguageSimplelifiedChinese = 0xD,
    LanguageFinnish             = 0xE,
    LanguageSwedish             = 0xF,
    LanguageDanish              = 0x10,
    LanguageNorwegian           = 0x11,
    LanguageCzech               = 0x12,
    LanguageHungarian           = 0x13,
    LanguageSlovak              = 0x14,
    LanguageArabic              = 0x15,
    LanguageTurkish             = 0x16,
    LanguageBulgarian           = 0x17,
    LanguageGreek               = 0x18,
    LanguageRomanian            = 0x19,
    LanguageThai                = 0x1A,
    LanguageUkrainian           = 0x1B,
    LanguageVietnamese          = 0x1C,
    LanguageIndonesian          = 0x1D,
    LanguageFiction             = 0x1E,
    LanguageHindi               = 0x1F,
    LanguageMax                 = 0x20,
    LanguageUnknown             = 0x20,
};