using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using REAssetExplorer.UI.Enums;

namespace REAssetExplorer.App.Views;

public partial class StatusWindow : Window, INotifyPropertyChanged
{
    private string _message = string.Empty;

    public StatusType Type { get; }
    
    public string Message
    {
        get => _message;
        set
        {
            if (_message != value)
            {
                _message = value;
                OnPropertyChanged();
            }
        }
    }

    public bool CanClose { get; }

    public StatusWindow(StatusType type, string message = "")
    {
        Type = type;
        Message = message;
        CanClose = type != StatusType.Loading;
        
        InitializeComponent();
        DataContext = this;
    }

    public void UpdateMessage(string message)
    {
        Message = message;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}