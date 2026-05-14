using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Media;

namespace REAssetExplorer.TestUI;

public enum StatusType { Loading, Error, Success, Warning }

public partial class StatusWindow : Window, INotifyPropertyChanged
{
    private string _message = string.Empty;

    public StatusType Type     { get; }
    public bool       CanClose { get; }

    public Visibility LoadingVisibility
        => Type == StatusType.Loading ? Visibility.Visible : Visibility.Collapsed;

    public string TypeLabel => Type switch
    {
        StatusType.Error   => "Error",
        StatusType.Success => "Success",
        StatusType.Warning => "Warning",
        _                  => "Loading",
    };

    public string TypeIcon => Type switch
    {
        StatusType.Error   => "",   // StatusError (Segoe MDL2)
        StatusType.Success => "",   // Accept
        StatusType.Warning => "",   // StatusWarning
        _                  => "",   // Refresh/Sync
    };

    public SolidColorBrush TypeColor => Type switch
    {
        StatusType.Error   => new SolidColorBrush(Color.FromRgb(0xE8, 0x27, 0x2A)),
        StatusType.Success => new SolidColorBrush(Color.FromRgb(0x4C, 0xB8, 0x6A)),
        StatusType.Warning => new SolidColorBrush(Color.FromRgb(0xE8, 0x97, 0x2A)),
        _                  => new SolidColorBrush(Color.FromRgb(0x4D, 0x9F, 0xDB)),
    };

    public string Message
    {
        get => _message;
        set { _message = value; OnPropertyChanged(); }
    }

    public StatusWindow() : this(StatusType.Loading, "Loading...") { }

    public StatusWindow(StatusType type, string message = "")
    {
        Type     = type;
        Message  = message;
        CanClose = type != StatusType.Loading;
        InitializeComponent();
        DataContext = this;
    }

    public void UpdateMessage(string message) => Message = message;

    private void OnClose(object sender, RoutedEventArgs e) => Close();

    public event PropertyChangedEventHandler? PropertyChanged;

    protected virtual void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
