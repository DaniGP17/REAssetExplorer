using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Games.RE7.Assets;
using REAssetExplorer.Testing.Examples;

namespace REAssetExplorer.Testing;

public class Program
{
    public static void Main()
    {
        var pakFile = AssetExplorerHelper.LoadRE7Pak();
        
        var result = AssetExplorerHelper.LoadAssetByPattern<RE7BankReader, BankData>(
            pakFile,
            "natives/stm/sound/wwise/event_chp4_pl2000_se_es.bnk.2.stm",
            new RE7BankReader()
        );
        
        if (result.IsSuccess && result.Value != null)
        {
            result.Value.PrintColored("Bank", ConsoleColor.Green, 1);
        }
        
        Console.WriteLine("\n\nPress any key to exit...");
        Console.ReadKey();
    }
}