using System.Windows;
using REAssetExplorer.UI.Helpers;
using REAssetExplorer.UI.Models;

namespace REAssetExplorer.App.Views;

public partial class FileExplorer : Window
{
    public TreeManager TreeManager => TreeManager.Instance;
    
    public FileExplorer()
    {
        InitializeComponent();

        DataContext = this;
    }
}