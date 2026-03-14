using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace REAssetExplorer.RenderTest2;

internal static class Program
{
    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.SetHighDpiMode(HighDpiMode.SystemAware);

        var appThread = new Thread(() =>
        {
            using var window = new RenderWindow();
            Application.Run(window);
        });
        
        appThread.SetApartmentState(ApartmentState.STA);
        appThread.Start();
        appThread.Join();
    }
}
